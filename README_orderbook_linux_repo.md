

## Benchmarks

Baseline for **v1**: `std::shared_ptr<Order>`, one `std::list` per price level, `std::map` price levels, `std::unordered_map` order lookup, one mutex. (`git checkout v1-baseline` to rebuild it.)

### Setup

| | |
|---|---|
| CPU | `<lscpu model name>`, 16 logical cores at about 3.2 GHz, L1d 32 KiB x8, L2 512 KiB x8, L3 16 MiB |
| OS | `<Ubuntu version>` on WSL2 (kernel `<uname -r>`) |
| Compiler | GCC 15 (`<g++ --version>`), C++20, CMake `Release` build (`-O3`) |
| Framework | Google Benchmark v1.8.3 |
| Method | single thread, pinned to one core (`taskset -c 3`), 15 repetitions, median reported |

### Results (v1)

| Benchmark | Median (Oct 25) | Throughput | Noise (CV) | Earlier run (Oct 18) |
|---|---|---|---|---|
| `BM_OrderbookAdd` | 225 ns | about 4.5M orders/s | 11% | 239 ns |
| `BM_OrderbookMatch` | 334 ns | n/a (two `AddOrder` calls per iteration) | 2% | 335 ns |
| `BM_OrderbookCancel` | 140 ns | about 7M cancels/s | 2% | 124 ns |

Add and Match agree between the two runs within noise. Cancel is about 13% slower in the second run. I have not isolated the cause (day-to-day machine state on WSL2 is a candidate), so from Phase 2 on, old and new code are compared side by side in the same session, never against numbers from another day.

What each benchmark measures:

- **Add:** one resting buy order per iteration, unique order ID, spread over 50 price levels, 200,000 iterations. No order matches. The book grows to 200k orders.
- **Match:** each iteration adds a resting sell and then a crossing buy at the same price, so one iteration is two `AddOrder` calls plus one trade. The book stays almost empty.
- **Cancel:** the book is pre-filled with 200k orders (not timed), then orders are cancelled oldest first.

All numbers include an uncontended mutex lock and unlock per call. Each `Orderbook` also owns a background thread (GoodForDay pruning), so the benchmarks create the book once, outside the timed loop.

### Caveats

- WSL2 adds noise. Treat differences under about 10% as noise.
- Cancel removes orders in the order they were inserted, which is easy on the CPU cache. Real cancels arrive in random order. A shuffled-cancel variant is planned.
- Single thread only. Latency percentiles (p99, p99.9) are not measured yet.

### Measurement and workflow mistakes I made and fixed

These produced believable results, so they are worth recording:

1. **Reusing one `OrderId`.** The first Add benchmark added the same order every iteration. Only the first add was real; the rest were rejected as duplicates, giving 17 ns per call. Fixed by pre-generating unique orders outside the timed loop and checking `Size()` after the run.
2. **Constructing an `Orderbook` inside the loop.** The constructor starts the pruning thread and the destructor joins it, so each iteration measured thread start and join: about 85 µs of CPU time and 165 µs of wall time per iteration. This is consistent with thread start and join dominating, though I did not isolate it with a separate experiment. Fixed by creating the book once, outside the loop.
3. **Running old binaries after a failed build.** I chained `cmake --build` and the test or benchmark command with a newline instead of `&&`. The builds failed on a compile error, and the old test, Valgrind, sanitizer and benchmark binaries kept running and looked like passes. The test count gave it away: 7 tests instead of 9. Fixed by chaining with `&&` and checking that the number of tests matches.

### Run it yourself

The build type is chosen when you configure, not at build time (`--config Release` is ignored by Makefile and Ninja generators):

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench && taskset -c 3 ./build-bench/benchmarks/OrderbookBenchmarks \
    --benchmark_repetitions=15 --benchmark_report_aggregates_only=true
```

### Next (Phase 2)

- **Hypothesis, not yet measured:** `MatchTrades` starts with `trades.reserve(orders_.size())`, so every `AddOrder` requests memory proportional to the number of resting orders even when nothing matches. I will test this against v1.
- **Planned:** a pre-allocated object pool, an intrusive doubly-linked list per price level, raw pointers instead of `shared_ptr`, then flat price levels. A prototype of the pool and list exists in a separate Visual Studio copy of the project. It will be ported to this repository in small steps on a `phase2` branch, with the tests, sanitizers and a same-session benchmark comparison after each step.
- **Known risk:** pooled memory is never returned to the allocator, so AddressSanitizer and Valgrind cannot detect use-after-release of an order. Tests that check values (trade IDs, prices, quantities) and an assert in the pool's release function have to catch that class of bug instead.

---

## Correctness tooling and bugs found

### Tools

- **GoogleTest:** 9 file-driven scenarios (`tests/*.txt`).
- **Valgrind memcheck:** 9 tests, 750 allocations and 750 frees, 0 errors.
- **ThreadSanitizer, AddressSanitizer + UndefinedBehaviorSanitizer:** clean on all 9 tests.
- **GCC and Clang** with `-Wall -Wextra -Wpedantic`: 0 warnings in project code. (Clang reports one `-Wcharacter-conversion` warning inside GoogleTest's own `gtest-printers.h`; that is third-party code and is not counted.)

### Reproduce

```bash
# ThreadSanitizer
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
cmake --build build-tsan && ./build-tsan/tests/OrderbookTests

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
cmake --build build-asan && ./build-asan/tests/OrderbookTests

# Valgrind (plain Debug build; do not combine with a sanitizer build)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build && valgrind --leak-check=full --track-origins=yes --error-exitcode=1 \
      ./build/tests/OrderbookTests

# Clang with strict warnings
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang && ./build-clang/tests/OrderbookTests
```

### Bugs found and fixed

| # | Bug | How it was found | Effect | Fix |
|---|---|---|---|---|
| 1 | **Startup race.** `ordersPruneThread_` was declared before `shutdown_` and the condition variable. Members are initialized in declaration order, so the thread started before they existed. | ThreadSanitizer | Data race in the constructor; the pruning thread could read an uninitialized flag. | Declare the thread last among the data members. |
| 2 | **`wait_for` without a predicate** in `PruneGoodForDayOrders`. A spurious wakeup was treated as a shutdown request. | Code review | The pruning thread could exit for good, so GoodForDay orders would never be cancelled. | Use the predicate overload and check `shutdown_` under the mutex. |
| 3 | **Lost wakeup in the destructor.** `shutdown_` was set without holding the mutex. | Code review | A notification could be missed, and `join()` could block until the next 16:00 prune. | Set the flag while holding the mutex, then `notify_all()`. |
| 4 | **Self-deadlock in `MatchTrades`.** `AddOrder` holds the mutex and `MatchTrades` called the public `CancelOrder`, which locks it again. | New regression test (partial FillAndKill) | The test hung until killed (exit 124). | Call `CancelOrderInternal`, which assumes the lock is already held. |
| 5 | **Level data erased while orders still rest.** `MatchTrades` erased the per-price entry in `data_` even when another order remained at that price. | New regression test (FillOrKill after a partial FillAndKill) | Unsigned counters underflowed, and a FillOrKill order that should fill was rejected. | Removed the redundant erases; `UpdateLevelData` already drops empty levels. |
| 6 | **Public `CancelOrder` skipped level bookkeeping.** | Code review | Stale `data_` fed wrong numbers into the FillOrKill check. | `CancelOrder` now locks and calls `CancelOrderInternal`. |
| 7 | **`GetOrderInfos()` read the book without the mutex.** | Code review | Unsynchronized reads while the pruning thread could modify the maps. | Added a scoped lock. |
| 8 | **Linux port:** `localtime_s` is Windows-only and `<mutex>`, `<thread>`, `<condition_variable>`, `<chrono>` were not included explicitly. | Compile errors on GCC | Did not build on Linux. | `#ifdef _WIN32` around the time call; explicit includes. |

Bugs 4 and 5 were invisible to the original 7 tests, which all passed. They were found by writing tests for specific paths (partial fills of FillAndKill orders).

### Known gaps

- The 16:00 pruning path is not exercised by any test, because the cutoff time is fixed. Making it injectable would allow a test that adds a GoodForDay order and watches it disappear.
- No test calls the book from several threads at once. ThreadSanitizer only reports races that actually happen during a run.
- The file-driven tests compare order and level counts only. They do not check the contents of the returned trades (IDs, prices, quantities).
