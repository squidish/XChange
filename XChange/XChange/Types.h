//
//  Types.h
//  XChange
//
//  Created by Williams on 10/09/2025.
//
#pragma once

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


// --- Common types ---
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

using OrderId = std::uint64_t;
using Price   = std::int64_t;  // integer ticks
using Qty     = std::int64_t;  // positive quantity

enum class Side { Buy = 0, Sell = 1 };

struct Order {
    OrderId    id{};
    Side       side{};
    Price      price{};
    Qty        qty{};
    TimePoint  ts{Clock::now()};
};

struct Trade {
    OrderId maker_id{}; // resting
    OrderId taker_id{}; // incoming
    Price   price{};
    Qty     qty{};
};
