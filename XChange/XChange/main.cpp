//
//  main.cpp
//  ConcurrentExchange
//
//  Created by Williams on 08/09/2025.
//

// Minimal Matching Engine (single-file) with optional async ingress
// Patched to remove std::jthread/std::stop_token for broader toolchain support.
// Uses std::thread + atomic<bool> and a queue close() for clean shutdown.
// Build: g++ -std=c++20 engine.cpp -pthread

#include "OrderBook.h"

// --- ConcurrentQueue for async ingress/egress (MPMC, mutex+cv) ---
template <typename T>
class ConcurrentQueue {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (closed_) return; // drop if closed
            q_.push(std::move(v));
        }
        cv_.notify_one();
    }

    // Blocking pop; returns false if queue closed and empty
    bool pop(T &out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return closed_ || !q_.empty(); });
        if (q_.empty()) return false; // closed and drained
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    bool try_pop(T &out) {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    bool closed_ = false;
};

struct EngineEvent {
    enum class Type { TradeBatch, BookSnapshot } type{Type::TradeBatch};
    std::vector<Trade> trades; // for TradeBatch
};

// --- Async wrapper around OrderBook ---
class AsyncMatchingEngine {
public:
    AsyncMatchingEngine() : running_(true), worker_([this]{ run(); }) {}
    ~AsyncMatchingEngine() {
        shutdown();
    }

    void submit(Order o) { inq_.push(std::move(o)); }

    bool poll_event(EngineEvent &ev) { return outq_.try_pop(ev); }

    // Optional blocking wait (not used in this demo)
    bool wait_event(EngineEvent &ev) { return outq_.pop(ev); }

    std::optional<Price> best_bid() const { return book_.best_bid(); }
    std::optional<Price> best_ask() const { return book_.best_ask(); }

    void shutdown() {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false)) {
            inq_.close();   // wake worker waiting on pop
            if (worker_.joinable()) worker_.join();
            outq_.close();  // wake any consumers
        }
    }

private:
    void run() {
        while (running_) {
            Order o;
            if (!inq_.pop(o)) break; // queue closed & drained
            auto trades = book_.add_order(std::move(o));
            if (!trades.empty()) outq_.push(EngineEvent{EngineEvent::Type::TradeBatch, std::move(trades)});
        }
    }

    OrderBook book_{};
    ConcurrentQueue<Order> inq_{};
    ConcurrentQueue<EngineEvent> outq_{};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

// --- Demos ---
int main_async_demo() {
    AsyncMatchingEngine eng;

    std::atomic<OrderId> next_id{100};
    auto mk = [&](Side s, Price p, Qty q){ return Order{ next_id++, s, p, q, Clock::now() }; };

    // Two producers seeding both sides
    std::thread t1([&]{ for (int i=0;i<10;++i) eng.submit(mk(Side::Buy, 100 + (i%2), 10 + 5*(i%3))); });
    std::thread t2([&]{ for (int i=0;i<10;++i) eng.submit(mk(Side::Sell, 101 - (i%2), 10 + 5*(i%3))); });

    // Aggressive taker
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    eng.submit(mk(Side::Buy, 102, 120));

    // Drain events briefly
    auto start = Clock::now();
    while (Clock::now() - start < std::chrono::milliseconds(300)) {
        EngineEvent ev;
        while (eng.poll_event(ev)) {
            if (ev.type == EngineEvent::Type::TradeBatch) {
                for (auto &t : ev.trades) {
                    std::cout << "TRADE maker=" << t.maker_id
                              << " taker=" << t.taker_id
                              << " px=" << t.price
                              << " qty=" << t.qty << "\n";
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    t1.join();
    t2.join();
    eng.shutdown();
    return 0;
}

int main_sync_demo() {
    OrderBook ob;
    auto now = Clock::now();
    auto mk = [&](OrderId id, Side s, Price p, Qty q){ return Order{ id, s, p, q, now }; };

    ob.add_order(mk(1, Side::Sell, 101, 50));
    ob.add_order(mk(2, Side::Sell, 102, 40));
    ob.add_order(mk(3, Side::Buy,  100, 70));

    ob.print_book();

    auto trades1 = ob.add_order(mk(4, Side::Buy, 102, 80));
    std::cout << "Trades from order 4:\n";
    for (auto const &t : trades1) {
        std::cout << " maker=" << t.maker_id
                  << " taker=" << t.taker_id
                  << " px=" << t.price
                  << " qty=" << t.qty << "\n";
    }

    ob.print_book();

    return 0;
}

int main() {
    std::cout << "=== SYNC DEMO ===\n";
    main_sync_demo();

    std::cout << "\n=== ASYNC DEMO ===\n";
    return main_async_demo();
}
