#include "Orderbook.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iterator>
#include <mutex>
#include <numeric>

Trades Orderbook::AddOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return AddOrderInternal(type, id ,side, price, quantity);
}
Trades Orderbook::AddOrderInternal(OrderType type, OrderId id, Side side, Price price, Quantity quantity){
	if (orders_.contains(id))
		return{};

	if (type == OrderType::FillAndKill && !CanMatch(side, price))
		return{};

	if (type==OrderType::FillOrKill && !CanFullyFill(side,price , quantity))
		return{};

	if (type == OrderType::Market)
	{
		if (side == Side::Buy && !asks_.empty())
		{
			price = asks_.rbegin()->first;
			type=OrderType::GoodTillCancel;
		}
		else if (side == Side::Sell && !bids_.empty())
		{
			price = bids_.rbegin()->first;
			type=OrderType::GoodTillCancel;
		}
		else
			return{};
	}

	/*
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
	*/
	
	Order* order = orderPool_.allocate();
	if(order==nullptr)
	{
		return {};
	}
	order->init(type,id,side,price,quantity);
	if(side == Side::Buy)
	{
		auto& orders = bids_[price];
		orders.PushBack(order);
	}
	else
	{
		auto& orders = asks_[price];
		orders.PushBack(order);
	}
	orders_.insert({id,OrderEntry{order}});
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
	if(!orders_.contains(orderId))
	{
		return;
	}
	
	auto order = orders_.at(orderId).order_;
	orders_.erase(orderId);

	//const OrderPointer order = it->second.order_;
	//const auto orderIterator = it->second.location_;
	//orders_.erase(it);

	const Price price = order->GetPrice();
	if (order->GetSide() == Side::Sell)
	{
		auto& orders = asks_.at(price);
		orders.Remove(order);
		if (orders.Empty())
			asks_.erase(price);
	}
	else
	{
		auto& orders = bids_.at(price);
		orders.Remove(order);
		if (orders.Empty())
			bids_.erase(price);
	}
	// UpdateLevelData drops the level entry itself when its order count reaches 0.
	OnOrderCancelled(order);
	order->reset();
	orderPool_.deallocate(order);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
	/*
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
	*/
	std::scoped_lock ordersLock{ordersMutex_};
	const auto it = orders_.find(order.GetOrderId());
	if(it == orders_.end())
	{
		return{};
	}
	OrderType orderType = it->second.order_->GetOrderType();
	CancelOrderInternal(order.GetOrderId());
	return AddOrderInternal(orderType, order.GetOrderId(),order.GetSide(),order.GetPrice(), order.GetQuantity());
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
	
	/*
	auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
		return LevelInfo{ price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
			[](Quantity runningSum, const OrderPointer& order)
			{ return runningSum + order->GetRemainingQuantity(); }
		) };
	};
	*/

	auto CreateLevelInfos = [](Price price, const IntrusiveOrderList& orders){
		Quantity total=0;
		const Order* current = orders.Front();
		while(current!=nullptr){
			total+=current->GetRemainingQuantity();
			current = current->next;
		}
		return LevelInfo{price,total};
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

		while (!bids.Empty() && !asks.Empty())
		{
			auto bid = bids.Front();
			auto ask = asks.Front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
			bid->Fill(quantity);
			ask->Fill(quantity);
			
			OrderId bidId = bid->GetOrderId();
			Price bidPrice = bid->GetPrice();
			bool bidFilled = bid->IsFilled();

			OrderId askId = ask->GetOrderId();
			Price askPrice = ask->GetPrice();
			bool askFilled = ask->IsFilled(); 
		
			if (bid->IsFilled())
			{
				bids.PopFront();
				orders_.erase(bidId);
			}
			if (ask->IsFilled())
			{
				asks.PopFront();
				orders_.erase(askId);
			}

			trades.push_back(Trade{
				TradeInfo{ bidId, bidPrice, quantity },
				TradeInfo{ askId, askPrice, quantity }
			});

			OnOrderMatched(bidPrice, quantity, bidFilled);
			OnOrderMatched(askPrice, quantity, askFilled);
		
			if(bidFilled)
			{
				bid->reset();
				orderPool_.deallocate(bid);
			}
			if(askFilled)
			{
				ask->reset();
				orderPool_.deallocate(ask);
			}
		}

		// Copy the keys first: bidPrice/askPrice refer to the map nodes being erased.
		// Do NOT erase data_ here: other orders may still rest at that price, and
		// UpdateLevelData already removes a level when its count reaches 0.
		/*
		if (bids.empty())
		{
			const Price level = bidPrice;
			bids_.erase(level);
		}
		if (asks.empty())
		{
			const Price level = askPrice;
			asks_.erase(level);
		}*/
		if (bids.Empty())
		{
			bids_.erase(bidPrice);
		}
		if (asks.Empty())
		{
			asks_.erase(askPrice);
		}
	}

	// Cancel what is left of a FillAndKill order. We already hold ordersMutex_
	// (we were called from AddOrder), so use the internal version.
	if (!bids_.empty())
	{	
		auto& [_,bidsList]=*bids_.begin();
		auto order = bidsList.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}
	if (!asks_.empty())
	{
		auto& [_,asksList]=*asks_.begin();
		auto order = asksList.Front();
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

/*
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
*/
bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
		return false;

	std::optional<Price> threshold;

	if (side == Side::Buy)
	{
		const auto [askPrice, _] = *asks_.begin();
		threshold = askPrice;
	}
	else
	{
		const auto [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
	}

	for (const auto& [levelPrice, levelData] : data_)
	{
		if (threshold.has_value() && ((side == Side::Buy && threshold.value() > levelPrice) || (side == Side::Sell && threshold.value() < levelPrice)))
			continue;
		if ((side == Side::Buy && levelPrice > price) || (side == Side::Sell && levelPrice < price))
			continue;
		if (quantity <= levelData.quantity_)
			return true;
		quantity -= levelData.quantity_;
	}
	return false;
}
