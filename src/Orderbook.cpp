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
	/*
	if (orders_.contains(id))
		return{};
	*/
	if (id>=orders_.size())
		return{};	
	
	if (orders_[id].order_ != nullptr)
		return{};

	if (price<0 || price >= MAX_PRICE)
		return{};

	if (type == OrderType::FillAndKill && !CanMatch(side, price))
		return{};

	if (type==OrderType::FillOrKill && !CanFullyFill(side,price , quantity))
		return{};

	if (type == OrderType::Market)
	{
		if (side == Side::Buy && !asks_.empty())
		{
			/*
			price = asks_.rbegin()->first;
			type=OrderType::GoodTillCancel;
			*/
			/*
			auto firstActiveAsk=std::ranges::find_if(asks_, [](const LevelData& level){
				return level.IsActive();
			});
			if (firstActiveAsk != asks_.end()){
				price=std::distance(asks_.begin(),firstActiveAsk);
				type=OrderType::GoodTillCancel;
			}*/
			if (bestAskPrice_ == -1)
				return{};
			//price = bestAskPrice_;
			Price worstAsk = bestAskPrice_;
			for (Price p = static_cast<Price>(asks_.size()) - 1; p >= bestAskPrice_; --p)
			{
				if (asks_[p].IsActive())
				{
					worstAsk = p;
					break;
				}
			}
			price = worstAsk;
			type=OrderType::GoodTillCancel;
		}
		else if (side == Side::Sell && !bids_.empty())
		{
			/*
			price = bids_.rbegin()->first;
			type=OrderType::GoodTillCancel;
			*/
			/*
			auto firstActiveBid = std::ranges::find_if(bids_, [](const LevelData& level){
				return level.IsActive();
			});
			if (firstActiveBid != bids_.end()){
				price = std::distance(bids_.begin(),firstActiveBid);
				type=OrderType::GoodTillCancel;
			}*/
			if(bestBidPrice_ == -1)
				return{};

			//price = bestBidPrice_;
			Price worstBid = bestBidPrice_;
			for (Price p = 0; p <= bestBidPrice_; ++p)
			{
				if (bids_[p].IsActive())
				{
					worstBid = p;
					break;
				}
			}
			price = worstBid;
			type = OrderType::GoodTillCancel;
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
		orders.orders.PushBack(order);
		orders.quantity_+=quantity;
		if(bestBidPrice_==-1 || price>bestBidPrice_)
			bestBidPrice_=price;
	}
	else
	{
		auto& orders = asks_[price];
		orders.orders.PushBack(order);
		orders.quantity_+=quantity;
		if(bestAskPrice_==-1 || price<bestAskPrice_)
			bestAskPrice_=price;
	}
	orders_[id].order_=order;
	activeOrderCount_++;
	//OnOrderAdded(order);
	return MatchTrades();
}

void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
	/*
	if(!orders_.contains(orderId))
	{
		return;
	}
	*/
	if (orderId>=orders_.size() || orders_[orderId].order_==nullptr)
		return;

	OrderPointer order = orders_[orderId].order_;
	orders_[orderId].order_=nullptr;

	//const OrderPointer order = it->second.order_;
	//const auto orderIterator = it->second.location_;
	//orders_.erase(it);

	const Price price = order->GetPrice();
	const Quantity quantity = order->GetRemainingQuantity();
	if (order->GetSide() == Side::Sell)
	{
		/*
		auto& orders = asks_.at(price);
		orders.Remove(order);
		if (orders.Empty())
			asks_.erase(price);
		*/
		asks_[price].orders.Remove(order);
		asks_[price].quantity_-=quantity;
		if (asks_[price].orders.Empty())
		{
			asks_[price].quantity_ = 0;
			// Amortized O(1) update if best ask was removed
			if (price == bestAskPrice_)
			{
				Price nextAsk = -1;
				for (Price p = price + 1; p < static_cast<Price>(asks_.size()); ++p)
				{
					if (asks_[p].IsActive()) { nextAsk = p; break; }
				}
				bestAskPrice_ = nextAsk;
			}
		}
	}
	else
	{
		/*
		auto& orders = bids_.at(price);
		orders.Remove(order);
		if (orders.Empty())
			bids_.erase(price);
		*/
		bids_[price].orders.Remove(order);
		bids_[price].quantity_-=quantity;
		if (bids_[price].orders.Empty())
		{
			bids_[price].quantity_ = 0;
			// Amortized O(1) update if best bid was removed
			if (price == bestBidPrice_)
			{
				Price nextBid = -1;
				for (Price p = price - 1; p >= 0; --p)
				{
					if (bids_[p].IsActive()) { nextBid = p; break; }
				}
				bestBidPrice_ = nextBid;
			}
		}
	}
	// UpdateLevelData drops the level entry itself when its order count reaches 0.
	//OnOrderCancelled(order);
	order->reset();
	orderPool_.deallocate(order);
	activeOrderCount_--;
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
	//const auto it = orders_.find(order.GetOrderId());
	auto id= order.GetOrderId();
	if(id>=orders_.size()||orders_[id].order_==nullptr)
	{
		return{};
	}
	OrderType orderType = orders_[id].order_->GetOrderType();
	CancelOrderInternal(order.GetOrderId());
	return AddOrderInternal(orderType, id,order.GetSide(),order.GetPrice(), order.GetQuantity());
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return activeOrderCount_;
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
	/*
	auto CreateLevelInfos = [](Price price, const IntrusiveOrderList& orders){
		Quantity total=0;
		const Order* current = orders.Front();
		while(current!=nullptr){
			total+=current->GetRemainingQuantity();
			current = current->next;
		}
		return LevelInfo{price,total};
	};
	*/
	/*
	for (const auto& [price, orders] : bids_)
		bidInfos.push_back(CreateLevelInfos(price, orders));
	for (const auto& [price, orders] : asks_)
		askInfos.push_back(CreateLevelInfos(price, orders));

	*/
	for (Price price=static_cast<Price>(bids_.size())-1;price>=0;--price){
		const auto& level=bids_[price];
		if(level.IsActive())
			bidInfos.push_back(LevelInfo{price,level.quantity_});
	}
	for (Price price = 0; price<static_cast<Price>(asks_.size());++price){
		const auto& level = asks_[price];
		if(level.IsActive())
			askInfos.push_back(LevelInfo{price,level.quantity_});
	}
	return OrderbookLevelInfos{ bidInfos, askInfos };
}

bool Orderbook::CanMatch(Side side, Price price) const
{
	if (side == Side::Buy)
	{
		if (bestAskPrice_ == -1)
			return false;
		/*
		Price bestAsk = -1;
		for(Price price = 0;price<static_cast<Price>(asks_.size());++price){
			if(asks_[price].IsActive()){
				bestAsk=price;
				break;
			}	
		}*/
		return price >= bestAskPrice_;
	}
	else
	{
		if (bestBidPrice_ == -1)
			return false;
		/*
		Price bestBid = -1;
		for(Price price = static_cast<Price>(bids_.size())-1; price >= 0; --price){
			if(bids_[price].IsActive()){
				bestBid=price;
				break;
			}
		}*/
		return price <= bestBidPrice_;
	}
}


Trades Orderbook::MatchTrades()
{
	Trades trades;
	trades.reserve(16);

	while (true)
	{
		Price bidPrice = bestBidPrice_;
		Price askPrice = bestAskPrice_;

		if (bidPrice == -1 || askPrice == -1 || bidPrice < askPrice)
			break;

		while (bidPrice != -1 && askPrice != -1 && bidPrice >= askPrice &&
		       bids_[bidPrice].IsActive() && asks_[askPrice].IsActive())
		{
			auto bid = bids_[bidPrice].orders.Front();
			auto ask = asks_[askPrice].orders.Front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
			bid->Fill(quantity);
			ask->Fill(quantity);
			
			bids_[bidPrice].quantity_ -= quantity;
			asks_[askPrice].quantity_ -= quantity;

			OrderId bidId = bid->GetOrderId();
			Price bidPriceVal = bid->GetPrice();
			bool bidFilled = bid->IsFilled();

			OrderId askId = ask->GetOrderId();
			Price askPriceVal = ask->GetPrice();
			bool askFilled = ask->IsFilled(); 
		
			if (bidFilled)
			{
				bids_[bidPrice].orders.PopFront();
				orders_[bidId].order_ = nullptr;
				activeOrderCount_--;
			}
			if (askFilled)
			{
				asks_[askPrice].orders.PopFront();
				orders_[askId].order_ = nullptr; // Correctly clears askId
				activeOrderCount_--;
			}

			trades.push_back(Trade{
				TradeInfo{ bidId, bidPriceVal, quantity },
				TradeInfo{ askId, askPriceVal, quantity }
			});
		
			if (bidFilled)
			{
				bid->reset();
				orderPool_.deallocate(bid);
			}
			if (askFilled)
			{
				ask->reset();
				orderPool_.deallocate(ask);
			}
		}
		if (activeOrderCount_ == 0)
		{
			bestBidPrice_ = -1;
			bestAskPrice_ = -1;
		}
		// Update or clean up empty price levels and adjust best price pointers
		if (bids_[bidPrice].orders.Empty())
		{
			bids_[bidPrice].quantity_ = 0;
			if (bidPrice == bestBidPrice_)
			{
				Price nextBid = -1;
				for (Price p = bidPrice - 1; p >= 0; --p)
				{
					if (bids_[p].IsActive()) { nextBid = p; break; }
				}
				bestBidPrice_ = nextBid;
			}
		}
		
		if (asks_[askPrice].orders.Empty())
		{
			asks_[askPrice].quantity_ = 0;
			if (askPrice == bestAskPrice_)
			{
				Price nextAsk = -1;
				for (Price p = askPrice + 1; p < static_cast<Price>(asks_.size()); ++p)
				{
					if (asks_[p].IsActive()) { nextAsk = p; break; }
				}
				bestAskPrice_ = nextAsk;
			}
		}
	}

	// Handle FillAndKill cancellation at top of book
	if (bestBidPrice_ != -1 && bids_[bestBidPrice_].IsActive()) {
		auto order = bids_[bestBidPrice_].orders.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}	

	if (bestAskPrice_ != -1 && asks_[bestAskPrice_].IsActive()) {
		auto order = asks_[bestAskPrice_].orders.Front();
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
		for (const auto& entry : orders_){
			auto order = entry.order_;
			if(order==nullptr)
				continue;
			
			if(order->GetOrderType() != OrderType::GoodForDay)
				continue;
			
			orderIds.push_back(order->GetOrderId());
		}
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

Orderbook::Orderbook() : bestBidPrice_(-1),bestAskPrice_(-1), bids_(MAX_PRICE),asks_(MAX_PRICE),orders_(MAX_ORDERS), ordersPruneThread_{ [this] { PruneGoodForDayOrders(); } } {}
/*
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
*/

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

	//std::optional<Price> threshold;
	Quantity accumulatedQuantity = 0;
	if (side == Side::Buy)
	{
		//Price askPrice = -1;
		/*
		const auto [askPrice, _] = *asks_.begin();
		threshold = askPrice;
		*/
		/*
		for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price){
			if(asks_[price].IsActive()){
				askPrice = price;
				break;
			}
		}*/
		if(bestAskPrice_ == -1)		
			return false;
		for (Price priceCmp = bestAskPrice_; priceCmp <= price && priceCmp < static_cast<Price>(asks_.size()); ++priceCmp){
			if(asks_[priceCmp].IsActive()){
				accumulatedQuantity += asks_[priceCmp].quantity_;
				if (accumulatedQuantity >= quantity)
					return true;
			}	
		}
	}
	else
	{
		/*
		const auto [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
		*/
		//Price bidPrice = -1;
		/*
		for (Price price = static_cast<Price>(bids_.size())-1; price >= 0; --price){
			if(bids_[price].IsActive()){
				bidPrice = price;
				break;
			}
		}*/
		if(bestBidPrice_ == -1)
			return false;
		for (Price priceCmp = bestBidPrice_; priceCmp >= price && priceCmp >= 0; --priceCmp){
			if(bids_[priceCmp].IsActive()){
				accumulatedQuantity += bids_[priceCmp].quantity_;
				if (accumulatedQuantity >= quantity)
					return true;
			}
		}
	}
	/*
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
	*/
	return false;
}
