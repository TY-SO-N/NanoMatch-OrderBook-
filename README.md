# Welcome to NanoMatch! Let's Explore High-Frequency Trading.

Hello there! If you are a fresher, a student, or a software engineer looking to break into the fascinating world of Quantitative Finance and High-Frequency Trading (HFT), you are exactly in the right place. 

I'm going to walk you through **NanoMatch**, a C++20 Limit Order Book matching engine. 

Think of this repository not just as code, but as an engineering deep-dive. We are going to build an engine that runs at **13 Million operations per second** with a delay of just **50 CPU cycles**. 

To get that fast, we have to break almost every rule you were taught in traditional Computer Science classes. Let's explore exactly *why* standard programming fails in HFT, and how we engineered around it!

---

## 📖 Core Concepts
1. [Introduction: What is an Order Book?](#1-introduction-what-is-an-order-book)
2. [Zero-Allocation (Why `new` is too slow)](#2-zero-allocation-why-new-is-too-slow)
3. [CPU Caching (OOP vs. Data-Oriented Design)](#3-cpu-caching-oop-vs-data-oriented-design)
4. [Fast Discovery (Bitmaps & Hardware Magic)](#4-fast-discovery-bitmaps--hardware-magic)
5. [Concurrency (Lock-Free Queues)](#5-concurrency-lock-free-queues)
6. [Thread Pinning (Fighting the OS)](#6-thread-pinning-fighting-the-os)
7. [Networking (Strings and Floats are forbidden)](#7-networking-strings-and-floats-are-forbidden)
8. [Trade-Offs & How to Run](#8-trade-offs--how-to-run)

---

## 1. Introduction: What is an Order Book?
Imagine you open an app like Robinhood and want to buy a share of Apple. Where does that order actually go? 
It goes to a central Exchange (like Nasdaq). The exchange keeps a massive ledger called a **Limit Order Book (LOB)**. It has two lists:
*   **Bids (Buyers):** People who want to buy, sorted by who is willing to pay the most.
*   **Asks (Sellers):** People who want to sell, sorted by who will sell for the cheapest.

When a buyer's price matches a seller's price, the exchange executes a trade! In the HFT world, we need to do this matching process in less than 1 microsecond. Let's see how we do it.

---

## 2. Zero-Allocation (Why `new` is too slow)

### The Problem
In your college classes, you probably learned to use `new Order()` or `malloc` whenever you needed to create data. But think about what happens behind the scenes! Your program has to stop, ask the Operating System for memory, wait for the OS to lock a memory table, find a free spot, and return it. This takes thousands of nanoseconds. In HFT, that is an eternity.

### The Solution: Memory Pools
What if we *never* asked the OS for memory while the market is open? 
When NanoMatch boots up in the morning, we allocate one massive, continuous block of memory large enough to hold 1,000,000 orders. This is called a **Memory Pool**. 
When an order arrives, we just say: `give me index 0`. Next order? `give me index 1`. No OS required! If an order is canceled, we put that index into a "Free List" to recycle it later. 
**Takeaway:** Pre-allocate everything. Avoid the Operating System at all costs!

---

## 3. CPU Caching (OOP vs. Data-Oriented Design)

### The Problem
Object-Oriented Programming (OOP) teaches us to group data together into objects. You might write:
```cpp
struct Order { uint64_t price; uint32_t qty; uint64_t id; };
```
Here is the secret about hardware: when a CPU reads memory from RAM, it doesn't read one variable; it grabs a 64-byte chunk called a **Cache Line**. If our engine needs to scan thousands of orders just to find the best `price`, it is forced to load all the useless `qty` and `id` data into the ultra-fast L1 Cache too! This clogs up the cache and slows down the CPU.

### The Solution: Structure of Arrays (SoA)
Instead of grouping data by object, we split it by *type*:
```cpp
std::vector<uint64_t> prices;
std::vector<uint32_t> quantities;
```
Now, when the engine searches for the best price, the CPU loads a pure block of just prices. It can scan them instantly without loading any useless data.
**Takeaway:** Think about how the hardware reads memory, not just how the code looks on screen!

---

## 4. Fast Discovery (Bitmaps & Hardware Magic)

### The Problem
If you need to find the highest bid, how would you do it? A `for` loop, right? `for (int i = 0; i < max; i++)`. But loops require branching logic, which can confuse the CPU pipeline and cause delays.

### The Solution: Bitmaps
We created a 64-bit integer where **every single bit represents a price**. If someone bids at $100, we flip the 100th bit from a `0` to a `1`. 
To find the highest price, we don't loop! We use a special Intel hardware instruction called `__builtin_clzll` (Count Leading Zeros). It scans all 64 bits simultaneously at the hardware level in a single clock cycle.
**Takeaway:** Let the silicon do the heavy lifting!

---

## 5. Concurrency: The SPSC Lock-Free Queue (Deep Dive)

### The Problem: Mutexes and Deadlocks
When two threads need to share data, you are taught to use a `std::mutex` (a lock). But if Thread A holds the lock, Thread B gets "put to sleep" by the OS. Waking a thread back up takes 10,000 nanoseconds! In HFT, we cannot afford to put threads to sleep.

### The Solution: Single-Producer Single-Consumer (SPSC) Ring Buffer
To pass data from the Network Thread to the Matching Engine, we built a Lock-Free Ring Buffer. 
Think of it like a circular sushi conveyor belt:
*   **The Producer (Network Thread):** Only puts sushi on empty plates. It is the *only* thread that updates the `tail` index.
*   **The Consumer (Matching Engine):** Only eats sushi from full plates. It is the *only* thread that updates the `head` index.
Because they follow these strict rules, they never have to talk to each other (no Mutex required!).

### Atomic Memory Orders (The Magic)
Even without locks, modern CPUs are so fast they try to reorder instructions. A CPU might try to update the `tail` index *before* it finishes writing the actual data to RAM! To stop this, we use strict memory fences:
*   **`std::memory_order_release`:** Used by the Producer. It guarantees that the CPU finishes writing the data to the array *before* the `tail` index is updated.
*   **`std::memory_order_acquire`:** Used by the Consumer. It guarantees that the CPU sees the most up-to-date `tail` index *before* it attempts to read the data.

### The Hardware Hack: False Sharing & `alignas(64)`
A CPU reads memory in 64-byte chunks called **Cache Lines**. If you declare `head` and `tail` normally, the compiler puts them right next to each other. When the Producer updates `tail` on Core 1, the hardware invalidates the Consumer's L1 cache on Core 0, forcing Core 0 to reload the cache line even though `head` didn't change! This destroys performance. 
We fixed this by forcing `head` and `tail` onto separate physical cache lines using `alignas(64)`. The CPU cores are now completely isolated.

### The Math Hack: Bitwise Power of 2
To wrap a ring buffer around, you normally use the modulo operator `(index % capacity)`. However, hardware division takes **15 to 20 CPU cycles**. We bypassed this by forcing our queue capacity to be a strict **Power of Two**. This allows us to use a bitwise AND operator `(index & (Capacity - 1))`, which wraps the array perfectly in exactly **1 CPU cycle**.

---

## 6. Thread Pinning (Fighting the OS)

### The Problem
The Windows or Linux task scheduler is always trying to be helpful. It will randomly move your C++ program from CPU Core 0 to CPU Core 3 to balance the power load. But if it moves your program, all of your precious L1 cache data on Core 0 is erased!

### The Solution: Thread Affinity (NUMA)
We politely tell the OS to leave us alone using `SetThreadAffinityMask`. We permanently nail our Matching Engine to CPU Core 0. 
Furthermore, when there are no orders to process, we do not call `sleep()`. We use an Intel instruction called `_mm_pause()` to rest the CPU to prevent it from overheating, while staying awake enough to react in 1 nanosecond when a new order arrives.

---

## 7. Networking (Strings and Floats are forbidden)

### The Problem
Computers are terrible at processing text strings (like `"AAPL"`) and floating-point decimals (`$103.50`). Decimals cause precision rounding errors, and strings require slow letter-by-letter comparison.

### The Solution: Integers Only!
*   **Prices:** We multiply all prices by 100. A price of `$103.50` is sent over the network as the whole integer `103500`.
*   **Tickers:** We don't send `"AAPL"`. We send the integer `0`. The server knows that `0` means Apple. 
We pack this data into a perfectly tight 16-byte binary structure and send it over the TCP socket. We also enable `TCP_NODELAY` to force the OS to send the packet instantly rather than batching it up!

---

## 8. Trade-Offs & How to Run

As an engineer, you must always understand your trade-offs:
1. **Memory vs. Latency:** By using Bitmaps, we waste a lot of RAM to hold empty price slots. But in HFT, RAM is cheap, and CPU cycles are priceless!
2. **TCP vs UDP:** We used TCP for orders because if an order drops, we *must* know about it. Re-inventing guaranteed delivery over UDP is often slower than the OS TCP stack.

### Try it yourself!
You can run this full distributed exchange right now on your computer. You just need CMake and Python.

**1. Build the C++ Exchange:**
```powershell
mkdir build && cd build
cmake ..
cmake --build . --target NanoMatchServer
```

**2. Start the Engine (Terminal 1):**
```powershell
.\src\NanoMatchServer.exe
```

**3. Stream Live Trades (Terminal 2):**
Open a second terminal and run our algorithmic Python market-maker to see the color-coded Wall Street feed stream in real-time!
```powershell
cd scripts
python client.py
```

If you run the `NanoMatchTests.exe` in the `build/tests/` folder, you can watch the engine dynamically process 10,000 random orders perfectly. Have fun!
