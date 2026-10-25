#include "Orderbook.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iterator>
#include <mutex>
#include <numeric>

Trades Orderbook::AddOrder(OrderPointer order)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	if (orders_.contains(order->GetOrderId()))
		return{};

	if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
		return{};

	if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
		return{};

	if (order->GetOrderType() == OrderType::Market)
	{
		if (order->GetSide() == Side::Buy && !asks_.empty())
		{
			const Price worstAsk = asks_.rbegin()->first;
			order->ToGoodTillCancel(worstAsk);
		}
		else if (order->GetSide() == Side::Sell && !bids_.empty())
		{
			const Price worstBid = bids_.rbegin()->first;
			order->ToGoodTillCancel(worstBid);
		}
		else
			return{};
	}

	OrderPointers::iterator iterator;
	if (order->GetSide() == Side::Buy)
	{
		auto& orders = bids_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}
	else
	{
		auto& orders = asks_[order->GetPrice()];
		orders.push_back(order);
		iterator = std::prev(orders.end());
	}
	orders_.insert({ order->GetOrderId(), OrderEntry{ order, iterator } });
	OnOrderAdded(order);
	return MatchTrades();
}

void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
	const auto it = orders_.find(orderId);
	if (it == orders_.end())
		return;

	const OrderPointer order = it->second.order_;
	const auto orderIterator = it->second.location_;
	orders_.erase(it);

	const Price price = order->GetPrice();
	if (order->GetSide() == Side::Sell)
	{
		auto& orders = asks_.at(price);
		orders.erase(orderIterator);
		if (orders.empty())
			asks_.erase(price);
	}
	else
	{
		auto& orders = bids_.at(price);
		orders.erase(orderIterator);
		if (orders.empty())
			bids_.erase(price);
	}
	// UpdateLevelData drops the level entry itself when its order count reaches 0.
	OnOrderCancelled(order);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
	OrderType orderType{};
	{
		std::scoped_lock ordersLock{ ordersMutex_ };
		const auto it = orders_.find(order.GetOrderId());
		if (it == orders_.end())
			return{};
		orderType = it->second.order_->GetOrderType();
	}
	CancelOrder(order.GetOrderId());
	return AddOrder(order.ToOrderPointer(orderType));
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return orders_.size();
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	LevelInfos bidInfos, askInfos;
	bidInfos.reserve(orders_.size());
	askInfos.reserve(orders_.size());

	auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
		return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
			[](Quantity runningSum, const OrderPointer& order)
			{ return runningSum + order->GetRemainingQuantity(); }
		) };
	};
	for (const auto& [price, orders] : bids_)
		bidInfos.push_back(CreateLevelInfos(price, orders));
	for (const auto& [price, orders] : asks_)
		askInfos.push_back(CreateLevelInfos(price, orders));

	return OrderbookLevelInfos{ bidInfos, askInfos };
}

bool Orderbook::CanMatch(Side side, Price price) const
{
	if (side == Side::Buy)
	{
		if (asks_.empty())
			return false;
		const Price bestAsk = asks_.begin()->first;
		return price >= bestAsk;
	}
	else
	{
		if (bids_.empty())
			return false;
		const Price bestBid = bids_.begin()->first;
		return price <= bestBid;
	}
}

Trades Orderbook::MatchTrades()
{
	Trades trades;
	trades.reserve(orders_.size());

	while (true)
	{
		if (bids_.empty() || asks_.empty())
			break;

		auto& [bidPrice, bids] = *bids_.begin();
		auto& [askPrice, asks] = *asks_.begin();

		if (bidPrice < askPrice)
			break;

		while (bids.size() && asks.size())
		{
			auto bid = bids.front();
			auto ask = asks.front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
			bid->Fill(quantity);
			ask->Fill(quantity);

			if (bid->IsFilled())
			{
				bids.pop_front();
				orders_.erase(bid->GetOrderId());
			}
			if (ask->IsFilled())
			{
				asks.pop_front();
				orders_.erase(ask->GetOrderId());
			}

			trades.push_back(Trade{
				TradeInfo{ bid->GetOrderId(), bid->GetPrice(), quantity },
				TradeInfo{ ask->GetOrderId(), ask->GetPrice(), quantity }
			});

			OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
			OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
		}

		// Copy the keys first: bidPrice/askPrice refer to the map nodes being erased.
		// Do NOT erase data_ here: other orders may still rest at that price, and
		// UpdateLevelData already removes a level when its count reaches 0.
		if (bids.empty())
		{
			const Price level = bidPrice;
			bids_.erase(level);
		}
		if (asks.empty())
		{
			const Price level = askPrice;
			asks_.erase(level);
		}
	}

	// Cancel what is left of a FillAndKill order. We already hold ordersMutex_
	// (we were called from AddOrder), so use the internal version.
	if (!bids_.empty())
	{
		const auto& order = bids_.begin()->second.front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}
	if (!asks_.empty())
	{
		const auto& order = asks_.begin()->second.front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}
	return trades;
}

void Orderbook::PruneGoodForDayOrders()
{
	// Cancels GoodForDay orders at 16:00 local time (the machine's time zone).
	using namespace std::chrono;
	const auto end = hours(16);

	while (true)
	{
		const auto now = system_clock::now();
		const auto now_c = system_clock::to_time_t(now);
		std::tm now_parts{};
#ifdef _WIN32
		localtime_s(&now_parts, &now_c);
#else
		localtime_r(&now_c, &now_parts);
#endif
		if (now_parts.tm_hour >= end.count())
			now_parts.tm_mday += 1;
		now_parts.tm_hour = static_cast<int>(end.count());
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;
		now_parts.tm_isdst = -1;   // let mktime decide about daylight saving

		const auto next = system_clock::from_time_t(std::mktime(&now_parts));
		const auto till = next - now + milliseconds(100);

		std::unique_lock ordersLock{ ordersMutex_ };

		// With a predicate, wait_for returns true if shutdown was requested and
		// false if it timed out; spurious wakeups are handled for us.
		if (shutdownConditionVariable_.wait_for(ordersLock, till,
				[this] { return shutdown_.load(std::memory_order_acquire); }))
			return;

		// Still holding the lock: collect and cancel in one critical section.
		OrderIds orderIds;
		for (const auto& [id, entry] : orders_)
			if (entry.order_->GetOrderType() == OrderType::GoodForDay)
				orderIds.push_back(id);

		for (const auto id : orderIds)
			CancelOrderInternal(id);
	}
}

Orderbook::~Orderbook()
{
	{
		// Set the flag while holding the mutex, so the pruning thread cannot
		// check it, miss the notification below, and go back to sleep.
		std::scoped_lock ordersLock{ ordersMutex_ };
		shutdown_.store(true, std::memory_order_release);
	}
	shutdownConditionVariable_.notify_all();
	ordersPruneThread_.join();
}

Orderbook::Orderbook() : ordersPruneThread_{ [this] { PruneGoodForDayOrders(); } } {}

void Orderbook::OnOrderCancelled(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
	UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
	auto& data = data_[price];
	data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;

	if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
		data.quantity_ -= quantity;
	else
		data.quantity_ += quantity;

	if (data.count_ == 0)
		data_.erase(price);
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
		return false;

	if (side == Side::Buy)
	{
		for (const auto& [askPrice, askOrders] : asks_)
		{
			if (askPrice > price)
				break; // Asks are sorted ascending; stop if price exceeds limit

			Quantity levelQuantity = std::accumulate(askOrders.begin(), askOrders.end(), (Quantity)0,
				[](Quantity sum, const OrderPointer& order) {
					return sum + order->GetRemainingQuantity();
				});

			if (quantity <= levelQuantity)
				return true;
			quantity -= levelQuantity;
		}
	}
	else
	{
		for (const auto& [bidPrice, bidOrders] : bids_)
		{
			if (bidPrice < price)
				break; // Bids are sorted descending; stop if price drops below limit

			Quantity levelQuantity = std::accumulate(bidOrders.begin(), bidOrders.end(), (Quantity)0,
				[](Quantity sum, const OrderPointer& order) {
					return sum + order->GetRemainingQuantity();
				});

			if (quantity <= levelQuantity)
				return true;
			quantity -= levelQuantity;
		}
	}
	return false;
}
