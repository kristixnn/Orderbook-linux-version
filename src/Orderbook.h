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
#include "IntrusiveOrderList.h"
#include "OrderPool.h"

class Orderbook {
public:
	Orderbook();
	~Orderbook();
	Orderbook(const Orderbook&) = delete;
	Orderbook& operator=(const Orderbook&) = delete;
	Orderbook(const Orderbook&&) = delete;
	Orderbook& operator=(Orderbook&&) = delete;

	Trades AddOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
	void CancelOrder(OrderId orderId);
	Trades ModifyOrder(OrderModify order);

	std::size_t Size() const;
	OrderbookLevelInfos GetOrderInfos() const;

private:
	struct OrderEntry {
		OrderPointer order_{ nullptr };
		//OrderPointers::iterator location_;
	};

	struct LevelData {
		Quantity quantity_{0};
		//Quantity count_{};
		IntrusiveOrderList orders;
		enum class Action {
			Add,
			Remove,
			Match
		};
		bool IsActive() const {
			return !orders.Empty();
		}
	};
	Price bestBidPrice_{-1};
	Price bestAskPrice_{-1};
	// Data members are initialized in declaration order.
	// ordersPruneThread_ MUST stay last: it starts running inside the constructor,
	// so everything it touches has to exist before it.
	//std::unordered_map<Price, LevelData> data_;
	//std::map<Price, IntrusiveOrderList, std::greater<Price>> bids_;  // best (highest) bid first
	//std::map<Price, IntrusiveOrderList, std::less<Price>> asks_;     // best (lowest) ask first
	//std::unordered_map<OrderId, OrderEntry> orders_;
	
	std::vector<LevelData> bids_;
	std::vector<LevelData> asks_;
	
	static constexpr std::size_t MAX_ORDERS=500000;
	static constexpr int MAX_PRICE =100000;	
	std::vector<OrderEntry> orders_;
	std::size_t activeOrderCount_{0};
	OrderPool<500000> orderPool_;

	mutable std::mutex ordersMutex_;
	std::condition_variable shutdownConditionVariable_;
	std::atomic<bool> shutdown_{ false };
	std::thread ordersPruneThread_;

	void PruneGoodForDayOrders();

	// Internal functions assume ordersMutex_ is ALREADY held. Only the public functions lock.
	Trades AddOrderInternal(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
	void CancelOrderInternal(OrderId orderId);

	//void OnOrderCancelled(OrderPointer order);
	//void OnOrderAdded(OrderPointer order);
	//void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
	//void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

	bool CanMatch(Side side, Price price) const;
	bool CanFullyFill(Side side, Price price, Quantity quantity) const;

	Trades MatchTrades();
};
