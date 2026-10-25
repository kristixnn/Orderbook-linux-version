#pragma once
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "Usings.h"
#include "Trade.h"
#include "Order.h"
#include "OrderType.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"

class Orderbook {
public:
	Orderbook();
	~Orderbook();
	Orderbook(const Orderbook&) = delete;
	Orderbook& operator=(const Orderbook&) = delete;
	Orderbook(Orderbook&&) = delete;
	Orderbook& operator=(Orderbook&&) = delete;

	Trades AddOrder(OrderPointer order);
	void CancelOrder(OrderId orderId);
	Trades ModifyOrder(OrderModify order);

	std::size_t Size() const;
	OrderbookLevelInfos GetOrderInfos() const;

private:
	struct OrderEntry {
		OrderPointer order_{ nullptr };
		OrderPointers::iterator location_;
	};

	struct LevelData {
		Quantity quantity_{};
		Quantity count_{};
		enum class Action {
			Add,
			Remove,
			Match
		};
	};

	// Data members are initialized in declaration order.
	// ordersPruneThread_ MUST stay last: it starts running inside the constructor,
	// so everything it touches has to exist before it.
	std::unordered_map<Price, LevelData> data_;
	std::map<Price, OrderPointers, std::greater<Price>> bids_;  // best (highest) bid first
	std::map<Price, OrderPointers, std::less<Price>> asks_;     // best (lowest) ask first
	std::unordered_map<OrderId, OrderEntry> orders_;
	mutable std::mutex ordersMutex_;
	std::condition_variable shutdownConditionVariable_;
	std::atomic<bool> shutdown_{ false };
	std::thread ordersPruneThread_;

	void PruneGoodForDayOrders();

	// Internal functions assume ordersMutex_ is ALREADY held. Only the public functions lock.
	void CancelOrderInternal(OrderId orderId);

	void OnOrderCancelled(OrderPointer order);
	void OnOrderAdded(OrderPointer order);
	void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
	void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

	bool CanMatch(Side side, Price price) const;
	bool CanFullyFill(Side side, Price price, Quantity quantity) const;

	Trades MatchTrades();
};
