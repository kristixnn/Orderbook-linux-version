#include"Orderbook.h"

int main() {
	Orderbook orderbook;
	/*
	const OrderId orderId = 1;

	orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
	std::cout << orderbook.Size() << std::endl;

	orderbook.CancelOrder(orderId);
	std::cout << orderbook.Size() << std::endl;
	*/
	Trades trades = orderbook.AddOrder(OrderType::GoodTillCancel,1,Side::Buy,100,10);
	std::cout<<"Buy Trades executed: "<<trades.size()<<"\n";

	Trades trades2 = orderbook.AddOrder(OrderType::GoodTillCancel,2,Side::Sell,100,5);
	std::cout<<"Sell Trades executed: "<<trades2.size()<<"\n";
	
	orderbook.CancelOrder(1);
	
	return 0;
}
