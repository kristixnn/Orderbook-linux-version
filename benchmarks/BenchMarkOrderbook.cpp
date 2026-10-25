#include <benchmark/benchmark.h>
#include "Orderbook.h"
#include <vector>

static OrderPointer MakeOrder(OrderId id, Side side, Price price) {
    return std::make_shared<Order>(OrderType::GoodTillCancel, id, side, price, 10);
}

// 1. Add resting buys (no asks, so nothing matches). Unique IDs.
static void BM_OrderbookAdd(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.max_iterations);
    std::vector<OrderPointer> orders;
    orders.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        orders.push_back(MakeOrder(i + 1, Side::Buy, 100 - static_cast<Price>(i % 50)));

    Orderbook book;
    std::size_t i = 0;
    for (auto _ : state)
        book.AddOrder(orders[i++]);

    if (book.Size() != n) state.SkipWithError("some adds were rejected");
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderbookAdd)->Iterations(200'000);

// 2. Resting sell + crossing buy, fresh orders every iteration.
static void BM_OrderbookMatch(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.max_iterations);
    std::vector<OrderPointer> sells, buys;
    sells.reserve(n); buys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        sells.push_back(MakeOrder(2 * i + 1, Side::Sell, 100));
        buys.push_back(MakeOrder(2 * i + 2, Side::Buy, 100));
    }

    Orderbook book;
    std::size_t i = 0;
    for (auto _ : state) {
        book.AddOrder(sells[i]);
        auto trades = book.AddOrder(buys[i]);
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
    for (std::size_t i = 0; i < n; ++i)
        book.AddOrder(MakeOrder(i + 1, Side::Buy, 100 - static_cast<Price>(i % 50)));

    OrderId id = 1;
    for (auto _ : state)
        book.CancelOrder(id++);

    if (book.Size() != 0) state.SkipWithError("some cancels failed");
}
BENCHMARK(BM_OrderbookCancel)->Iterations(200'000);

BENCHMARK_MAIN();