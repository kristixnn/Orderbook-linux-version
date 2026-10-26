#pragma once
enum class OrderType {
	GoodTillCancel,
	FillAndKill,
	FillOrKill,//doesnt change the quantity in the orderbook, doesnt add when executed
	GoodForDay,
	Market,
	NoType
};
