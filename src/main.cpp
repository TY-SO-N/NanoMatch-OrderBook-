#include <iostream>
#include <iomanip>
#include <thread>
#include "OrderBook.h"
#include "TcpServer.h"
#include "RingBuffer.h"
#include "ThreadUtils.h"

// Intel pause intrinsic
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

using namespace NanoMatch;

// Top 20 Tech Tickers mapping array
const char* TICKERS[] = {
    "AAPL", "MSFT", "GOOG", "AMZN", "NVDA", "META", "TSLA", "NFLX", 
    "INTC", "AMD",  "CSCO", "ADBE", "CRM",  "ORCL", "IBM",  "QCOM", 
    "TXN",  "AVGO", "NOW",  "UBER"
};

int main() {
    std::cout << "\033[96m======================================\033[0m\n";
    std::cout << "\033[96m  NanoMatch Exchange Server Booting   \033[0m\n";
    std::cout << "\033[96m======================================\033[0m\n";

    // 1 Million capacity Lock-Free Queue
    static RingBuffer<ClientMessage, 1048576> order_queue;
    static OrderBook engine;

    // Start TCP Server on Core 1
    std::thread network_thread([&]() {
        pinThreadToCore(1);
        std::cout << "\033[96m[NUMA] Network Thread pinned to Core 1.\033[0m" << std::endl;

        TcpServer server(order_queue);
        if (!server.start(8080)) {
            return;
        }

        while (true) {
            server.poll();
            // Network thread can afford to yield briefly if idle to not burn CPU 1
            // But for pure HFT, we keep it spinning.
        }
    });

    // Start Matching Engine on Core 0
    pinThreadToCore(0);
    std::cout << "\033[96m[NUMA] Matching Engine pinned to Core 0.\033[0m" << std::endl;
    
    // Warm up memory pool (this MUST be called by the matching thread for NUMA locality)
    engine.warmup();

    std::cout << "\033[92m[SYSTEM] Exchange is Live and Ready for Orders!\033[0m\n" << std::endl;

    ClientMessage msg;
    while (true) {
        // Poll Lock-Free Queue
        if (order_queue.pop(msg)) {
            if (msg.type == 1) { // Add Limit Order
                Side side = (msg.side == 0) ? Side::Buy : Side::Sell;
                engine.addLimitOrder(side, msg.price, msg.qty);
                
                // Convert Fixed-Point back to Decimal (Multiplier: 100)
                double price_decimal = msg.price / 100.0;
                
                // Resolve Instrument ID to Ticker String
                const char* ticker = "UNKNOWN";
                if (msg.instrument_id < 20) {
                    ticker = TICKERS[msg.instrument_id];
                }
                
                if (side == Side::Buy) {
                    std::cout << "[\033[92mMATCHING CORE\033[0m] \033[92mBUY  " << std::left << std::setw(4) << msg.qty << " " << std::setw(5) << ticker << " @ $" << std::fixed << std::setprecision(2) << price_decimal << "\033[0m\n";
                } else {
                    std::cout << "[\033[91mMATCHING CORE\033[0m] \033[91mSELL " << std::left << std::setw(4) << msg.qty << " " << std::setw(5) << ticker << " @ $" << std::fixed << std::setprecision(2) << price_decimal << "\033[0m\n";
                }
            }
        } else {
            // Queue is empty, pause CPU to prevent thermal throttling
            // This is a ~40 cycle instruction that doesn't yield to the OS
            _mm_pause();
        }
    }

    network_thread.join();
    return 0;
}
