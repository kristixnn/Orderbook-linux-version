#pragma once
#include <vector>
#include "Order.h"
#include <cstddef>

template<size_t Capacity>
class OrderPool {
private:
	std::vector<Order>pool_;
	Order* freeListHead_ = nullptr;
public:
	OrderPool() {
		static_assert(Capacity > 0);
		pool_.resize(Capacity);
		for (size_t i = 0; i < Capacity - 1; ++i) {
			pool_[i].next = &pool_[i + 1];
		}
		pool_[Capacity - 1].next = nullptr;
		freeListHead_ = &pool_[0];
	}
	OrderPool(const OrderPool&) = delete;
	OrderPool& operator=(const OrderPool&) = delete;

	Order* allocate() {
		if (!freeListHead_) {
			return nullptr;
		}
		Order* order = freeListHead_;
		freeListHead_ = freeListHead_->next;

		order->next = nullptr;
		order->prev = nullptr;
		return order;
	}
	void deallocate(Order* order) {
		if (order == nullptr) return;

		order->next = freeListHead_;
		order->prev = nullptr;
		freeListHead_ = order;
	}
};