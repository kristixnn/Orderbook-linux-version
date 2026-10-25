#pragma once
#include "Order.h"

class IntrusiveOrderList {
private:

	Order* head_ = nullptr;
	Order* tail_ = nullptr;

public:

	bool Empty() const { return head_==nullptr; }

	Order* Front() const { return head_; }
	Order* Back() const { return tail_; }

	void PushBack(Order* order) {
		order->next = nullptr;
		order->prev = tail_;
		if (tail_) {
			tail_->next = order;
		}
		else
		{
			head_ = order;
		}
		tail_ = order;
	}

	void Remove(Order* order) {
		if (order->prev) {
			order->prev->next = order->next;
		}
		else {
			head_ = order->next;
		}
		if (order->next) {
			order->next->prev = order->prev;
		}
		else {
			tail_ = order->prev;
		}
		order->next = nullptr;
		order->prev = nullptr;
	}
	Order* PopFront() {
		if (head_ == nullptr) {
			return nullptr;
		}
		Order* frontOrder = head_;
		Remove(frontOrder); 
		return frontOrder;
	}
}; 