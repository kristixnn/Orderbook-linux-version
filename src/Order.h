#pragma once
#include "OrderType.h"
#include "Side.h"
#include "LevelInfo.h"
#include "Usings.h"
#include "Constants.h"


class Order {
public:
	//{} prevents accidental data loss from implicit conversions
	/*
	Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
		: orderType_{ orderType }, orderId_{ orderId }, side_{ side }, price_{ price }, initialQuantity_{ quantity }, remainingQuantity_{quantity}
	{ }

	Order(OrderId orderId, Side side, Quantity quantity)
		: Order(OrderType::Market,orderId,side,Constants::InvalidPrice,quantity)
	{}
	*/
	Order() = default;
	Order* next = nullptr;
	Order* prev = nullptr;

	void init(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity){
	orderType_ = orderType;
	orderId_=orderId;
	side_=side;
	price_=price;
	initialQuantity_=quantity;
	remainingQuantity_=quantity;
	inUse=true;
	next=nullptr;
	prev=nullptr;
	}
	
	void reset(){
	orderId_=0;
	price_=0;
	initialQuantity_=0;
	remainingQuantity_=0;
	inUse=false;
	next=nullptr;
	prev=nullptr;
	}

	OrderType GetOrderType() const { return orderType_; }
	OrderId GetOrderId() const { return orderId_; }
	Side GetSide() const { return side_; }
	Price GetPrice() const { return price_; }
	Quantity GetInitialQuantity() const { return initialQuantity_; }
	Quantity GetRemainingQuantity() const { return remainingQuantity_; }
	Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
	bool IsFilled() const { return GetRemainingQuantity() == 0; }
	bool isInUse() const {return inUse;}
	void Fill(Quantity quantity) {
		if (quantity>GetRemainingQuantity())
		{
			throw std::logic_error(std::format("Order ({}) cannot be filled for more than remaining quantity", GetOrderId()));
		}
		remainingQuantity_ -= quantity;
	}


	void ToGoodTillCancel(Price price) {
		if (GetOrderType() != OrderType::Market) {
			throw std::logic_error(std::format("Order ({}) cannot have its price adjusted, only market orders can", GetOrderId()));
		}

		price_ = price;
		orderType_ = OrderType::GoodTillCancel;
	}
private:
	/*
	OrderType orderType_;
	OrderId orderId_;
	Side side_;
	Price price_;
	Quantity initialQuantity_;
	Quantity remainingQuantity_;
	*/
	OrderType orderType_{OrderType::NoType};
	OrderId orderId_{0};
	Side side_{Side::NoSide};
	Price price_{0};
	Quantity initialQuantity_{0};
	Quantity remainingQuantity_{0};
	bool inUse = false;
};

using OrderPointer = Order*;
//using OrderPointers = std::list<OrderPointer>;
using OrderIds = std::vector<OrderId>;
