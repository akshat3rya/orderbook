# Orderbook

A lightweight electronic stock exchange simulator built in **C++17** featuring a **price-time priority matching engine**, **limit order book**, **custom TCP server**, and **binary communication protocol**. The project explores the core concepts behind modern trading systems while emphasizing clean architecture, deterministic execution, and concurrent network communication.

---

## Features

- Price-time priority order matching
- Limit and market orders
- Order cancellation and quantity modification
- Multi-level order book
- Custom binary TCP protocol
- Concurrent client handling
- Live market-data broadcasting
- Thread-safe command queues
- Comprehensive unit and protocol tests

---

## Architecture

```text
                    Clients
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼
 Trading Connections          Market Data Subscriber
         │                           ▲
         ▼                           │
   Connection Threads                │
         │                           │
         ▼                           │
     Command Queue                   │
         │                           │
         ▼                           │
  Matching Engine (Single Thread)    │
         │                           │
         ├────────► Order Book        │
         │                           │
         ▼                           │
   Market Data Queue                 │
         │                           │
         ▼                           │
   Broadcaster Thread ───────────────┘
```

The matching engine is intentionally **single-threaded** so that every order is processed in arrival order, guaranteeing deterministic execution and preserving strict **price-time priority**. Networking, order processing, and market-data broadcasting execute independently and communicate through thread-safe queues, preventing network latency from affecting matching performance.

---

## Project Structure

| Component | Description |
| :-------- | :---------- |
| **OrderBook** | Stores and manages resting buy/sell orders |
| **MatchingEngine** | Executes trades using price-time priority |
| **Server** | Handles TCP connections and client sessions |
| **Protocol** | Binary message serialization and framing |
| **GuardedQueue** | Thread-safe producer-consumer queue |
| **Client** | Trading client implementation |
| **Subscriber** | Receives live market-data broadcasts |
| **Tests** | Unit, protocol, and concurrency tests |

---

## Building

Requires **g++** with **C++17** support.

Build the project:

```bash
mkdir build
cd build
cmake ..
make 
./orderbook
```



---

## Testing

The project includes three independent test suites covering the matching engine, networking layer, and concurrent infrastructure.

Run all tests with:

```bash
ctest
```

or individually:

```bash
./run_tests
./run_protocol_tests
./run_queue_tests
```

The test suite validates:

- Partial fills and remaining order quantities
- Order cancellation after partial execution
- FIFO price-time priority after order modification
- Multi-level order book sweeps
- Empty-book edge cases
- Level change notifications used for market-data broadcasting
- Binary protocol framing and fragmented TCP reads
- Thread-safe producer-consumer queue behavior under concurrency

All tests are implemented using a lightweight custom test runner without external testing frameworks.
---

## Design Highlights

- Deterministic single-writer matching engine
- FIFO execution within each price level
- **O(1)** order lookup through indexed order IDs
- Custom binary protocol with exact-byte framing
- Asynchronous market-data broadcasting
- Modular architecture for future extensions
