#include <benchmark/benchmark.h>
#include "Orderbook.h"
#include <vector>

// 1. Add resting buys (no asks, so nothing matches). Unique IDs.
static void BM_OrderbookAdd(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.max_iterations);
    Orderbook book;
    std::size_t i = 0;
    for (auto _ : state) {
        book.AddOrder(OrderType::GoodTillCancel, static_cast<OrderId>(i + 1), Side::Buy, 100 - static_cast<Price>(i % 50), 10);
        ++i;
    }

    if (book.Size() != n) state.SkipWithError("some adds were rejected");
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderbookAdd)->Iterations(200'000);

// 2. Resting sell + crossing buy, fresh orders every iteration.
static void BM_OrderbookMatch(benchmark::State& state) {
    //const std::size_t n = static_cast<std::size_t>(state.max_iterations);
    Orderbook book;
    std::size_t i = 0;
    for (auto _ : state) {
        book.AddOrder(OrderType::GoodTillCancel, static_cast<OrderId>(2 * i + 1), Side::Sell, 100, 10);
        auto trades = book.AddOrder(OrderType::GoodTillCancel, static_cast<OrderId>(2 * i + 2), Side::Buy, 100, 10);
        benchmark::DoNotOptimize(trades);
        ++i;
    }

    if (book.Size() != 0) state.SkipWithError("orders did not fully match");
}
BENCHMARK(BM_OrderbookMatch)->Iterations(200'000);

// 3. Cancel only: fill the book first (not timed), then time the cancels.
static void BM_OrderbookCancel(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.max_iterations);
    Orderbook book;
    for (std::size_t i = 0; i < n; ++i) {
        book.AddOrder(OrderType::GoodTillCancel, static_cast<OrderId>(i + 1), Side::Buy, 100 - static_cast<Price>(i % 50), 10);
    }

    OrderId id = 1;
    for (auto _ : state)
        book.CancelOrder(id++);

    if (book.Size() != 0) state.SkipWithError("some cancels failed");
}
BENCHMARK(BM_OrderbookCancel)->Iterations(200'000);

BENCHMARK_MAIN();
