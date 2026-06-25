#include <benchmark/benchmark.h>
#include "OrderBook.h"
#include "RdtscTimer.h"
#include <vector>
#include <random>
#include <iostream>

using namespace NanoMatch;

// Global pre-generated orders so we don't benchmark the random number generator
struct BenchmarkOrder {
    Side side;
    Price price;
    Quantity qty;
};

std::vector<BenchmarkOrder> orders;
constexpr size_t NUM_ORDERS = 5000000;

void SetupOrders() {
    if (!orders.empty()) return;
    orders.reserve(NUM_ORDERS);
    
    // We want a realistic distribution: mostly deep limit orders, some aggressive crossing orders
    std::mt19937 gen(42); // deterministic seed
    std::uniform_int_distribution<Price> price_dist(900, 1100);
    std::uniform_int_distribution<Quantity> qty_dist(10, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (size_t i = 0; i < NUM_ORDERS; ++i) {
        orders.push_back({
            side_dist(gen) == 0 ? Side::Buy : Side::Sell,
            price_dist(gen),
            qty_dist(gen)
        });
    }
}

static void BM_EngineThroughput(benchmark::State& state) {
    SetupOrders();
    
    for (auto _ : state) {
        OrderBook book;
        book.warmup(); // Pinned to current thread, zero-allocated
        
        for (const auto& o : orders) {
            OrderId id = book.addLimitOrder(o.side, o.price, o.qty);
            benchmark::DoNotOptimize(id);
        }
    }
    state.SetItemsProcessed(state.iterations() * NUM_ORDERS);
}
BENCHMARK(BM_EngineThroughput)->Unit(benchmark::kMillisecond)->Iterations(5);

// Custom main to run RDTSC manually before Google Benchmark
int main(int argc, char** argv) {
    std::cout << "[RDTSC] Starting Hardware Telemetry..." << std::endl;
    SetupOrders();
    OrderBook book;
    book.warmup();
    
    // Warm up the CPU caches with first 1M orders
    for (size_t i = 0; i < 1000000; ++i) {
        book.addLimitOrder(orders[i].side, orders[i].price, orders[i].qty);
    }

    // Measure the next 1M orders purely in CPU cycles
    uint64_t total_cycles = 0;
    for (size_t i = 1000000; i < 2000000; ++i) {
        uint64_t start = RdtscTimer::now();
        OrderId id = book.addLimitOrder(orders[i].side, orders[i].price, orders[i].qty);
        uint64_t end = RdtscTimer::now();
        benchmark::DoNotOptimize(id);
        total_cycles += (end - start);
    }
    
    std::cout << "[RDTSC] Average CPU Clock Cycles per Order: " 
              << (total_cycles / 1000000.0) << " cycles" << std::endl;
    std::cout << "--------------------------------------------------------\n";

    // Run Google Benchmark
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    
    return 0;
}
