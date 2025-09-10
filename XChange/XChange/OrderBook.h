//
//  OrderBook.h
//  XChange
//
//  Created by Williams on 10/09/2025.
//
#pragma once
#include "Types.h"

// --- Order Book (single-threaded core) ---
class OrderBook {
public:
    // Add a limit order; match immediately; return generated trades.
    std::vector<Trade> add_order(Order order) {
        std::vector<Trade> trades;
        if (order.side == Side::Buy) {
            // Cross against best asks
            while (order.qty > 0 && !asks_.empty()) {
                auto best_it = asks_.begin(); // lowest ask
                Price best_px = best_it->first;
                if (order.price < best_px) break; // not crossable
                auto &queue = best_it->second; // FIFO at that level
                while (order.qty > 0 && !queue.empty()) {
                    auto &resting = queue.front();
                    Qty traded = std::min(order.qty, resting.qty);
                    trades.push_back({resting.id, order.id, resting.price, traded});
                    order.qty   -= traded;
                    resting.qty -= traded;
                    if (resting.qty == 0) {
                        id_index_.erase(resting.id);
                        queue.pop_front();
                    } else {
                        break; // partial on resting; remains in front
                    }
                }
                if (queue.empty()) asks_.erase(best_it);
            }
            if (order.qty > 0) enqueue(order);
        } else { // Sell
            // Cross against best bids
            while (order.qty > 0 && !bids_.empty()) {
                auto best_it = bids_.begin(); // highest bid (custom comparator)
                Price best_px = best_it->first;
                if (order.price > best_px) break; // not crossable
                auto &queue = best_it->second;
                while (order.qty > 0 && !queue.empty()) {
                    auto &resting = queue.front();
                    Qty traded = std::min(order.qty, resting.qty);
                    trades.push_back({resting.id, order.id, resting.price, traded});
                    order.qty   -= traded;
                    resting.qty -= traded;
                    if (resting.qty == 0) {
                        id_index_.erase(resting.id);
                        queue.pop_front();
                    } else {
                        break;
                    }
                }
                if (queue.empty()) bids_.erase(best_it);
            }
            if (order.qty > 0) enqueue(order);
        }
        return trades;
    }

    bool cancel(OrderId id) {
        auto it = id_index_.find(id);
        if (it == id_index_.end()) return false;
        auto [side, price] = it->second;
        if (side == Side::Buy) {
            auto lvl = bids_.find(price);
            if (lvl == bids_.end()) return false;
            auto &dq = lvl->second;
            for (auto itq = dq.begin(); itq != dq.end(); ++itq) {
                if (itq->id == id) {
                    dq.erase(itq);
                    id_index_.erase(it);
                    if (dq.empty()) bids_.erase(lvl);
                    return true;
                }
            }
        } else {
            auto lvl = asks_.find(price);
            if (lvl == asks_.end()) return false;
            auto &dq = lvl->second;
            for (auto itq = dq.begin(); itq != dq.end(); ++itq) {
                if (itq->id == id) {
                    dq.erase(itq);
                    id_index_.erase(it);
                    if (dq.empty()) asks_.erase(lvl);
                    return true;
                }
            }
        }
        return false;
    }

    std::optional<Price> best_bid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }
    std::optional<Price> best_ask() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    void print_book(std::ostream &os = std::cout) const {
        os << "\n===== ORDER BOOK =====\n";
        os << " Asks (low→high)\n";
        for (auto const & [px, q] : asks_) {
            os << "  " << px << " : ";
            for (auto const &o : q) os << o.id << "x" << o.qty << " ";
            os << "\n";
        }
        os << " Bids (high→low)\n";
        for (auto const & [px, q] : bids_) {
            os << "  " << px << " : ";
            for (auto const &o : q) os << o.id << "x" << o.qty << " ";
            os << "\n";
        }
        os << "======================\n";
    }

private:
    // Highest bid first
    using BidLevels = std::map<Price, std::deque<Order>, std::greater<>>;
    // Lowest ask first
    using AskLevels = std::map<Price, std::deque<Order>>;

    BidLevels bids_{};
    AskLevels asks_{};
    std::unordered_map<OrderId, std::pair<Side, Price>> id_index_{}; // id -> (side, price)

    void enqueue(const Order &order) {
        if (order.side == Side::Buy) {
            bids_[order.price].push_back(order);
        } else {
            asks_[order.price].push_back(order);
        }
        id_index_[order.id] = {order.side, order.price};
    }
};
