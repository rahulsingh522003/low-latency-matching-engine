# C++ Limit Order Book & Matching Engine

A single-instrument limit order book and matching engine implemented in C++.  
Supports limit/market orders, price-time priority matching, cancellation, and depth queries.

---

## Features

- Limit and market orders (buy/sell)
- Price-time priority matching (FIFO within price level)
- Partial fills and multi-level matching
- Order cancellation by ID
- Trade generation
- Top-of-book (best bid/ask, spread)
- Depth snapshot via `levels()` API
- Unit tests + verbose test runner
- Interactive CLI and demo program
- Benchmarking (throughput measurement)

---

## Order Types

### Limit Order
- Executes at specified price or better
- May rest in the book if not fully matched

### Market Order
- Executes immediately against best available prices
- Never rests in the book

---

## Example

Add Sell: 101 x 40  
Add Buy:  103 x 40  

→ Trade: 101 x 40

---

## Project Structure

include/        # Header files  
src/            # Core matching engine  
tests/          # Unit tests (compact + verbose)  
examples/       # Demo and interactive CLI  
benchmarks/     # Performance benchmarking  
CMakeLists.txt  

---

## Build

cmake -S . -B build  
cmake --build build  

---

## Run

### Run tests (compact)
./build/test_order_book

### Verbose tests (detailed output)
./build/test_order_book_verbose

### Demo (scripted scenario)
./build/order_book_demo

### Interactive CLI
./build/order_book_interactive

### Benchmark
./build/order_book_benchmark

---

## Design

### Data Structures

- std::map  
  Maintains sorted price levels  
  Bids → descending  
  Asks → ascending  

- std::list  
  FIFO queue for orders at each price level  
  Ensures time priority  

- std::unordered_map  
  O(1) average lookup for order ID  
  Enables fast cancellation  

---

### Matching Logic

- Price priority:
  Buy → lowest ask  
  Sell → highest bid  

- Time priority:
  FIFO within each price level  

- Execution price:
  Trade occurs at resting order price  

- Market orders:
  Sweep across multiple price levels  
  Do not rest in the book  

---

## Performance

Benchmark performed using 1,000,000 synthetic orders (mixed workload).

Results (multiple runs):

- Time taken: ~0.60–0.62 seconds  
- Throughput: ~1.6M–1.67M orders/sec (single-threaded)

Example run:
- Time taken: 0.598 seconds  
- Throughput: 1,671,018 orders/sec  

---

## Complexity (High-Level)

Add order       → O(log P)  
Cancel order    → O(log P)  
Match execution → O(K)  

P = number of price levels  
K = number of matched orders  

---

## Depth Snapshot Example

Bids:
  100 -> 100
   99 -> 25

Asks:
  101 -> 50
  102 -> 15

---

## Author

Rahul Singh