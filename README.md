# Low-Latency Multi-Instrument Matching Engine

Author: Rahul Singh

A C++20 matching engine with separate instrument books, price-time priority,
intrusive data structures, reusable object pools, and a Nasdaq TotalView-ITCH
5.0 decoding and reconstruction pipeline.

The engine processes commands synchronously with a single writer. Its focus is
matching correctness, explicit object ownership, and reproducible performance
measurements.

## Features

- Market, limit, stop, stop-limit, trailing-stop, and trailing-stop-limit orders.
- GTC, IOC, FOK, and AON instructions, partial fills, quantity reductions,
  modification, replacement, cancellation, and externally supplied executions.
- Hidden and display-capped liquidity. Displayed quantity is
  `min(remaining quantity, display cap)`; the model preserves FIFO rather than
  implementing an exchange-specific iceberg refresh policy.
- Separate bid, ask, buy-stop, sell-stop, trailing-buy-stop, and
  trailing-sell-stop indexes for each instrument.
- Intrusive FIFO order queues, AVL price indexes, cached best-price pointers,
  and an active-order hash table using linear probing and backward-shift deletion.
- Typed block pools for symbols, books, levels, and orders, with stable object
  addresses and reusable free slots.
- Synchronous callbacks for successful order, execution, level, and book changes.
- Streaming ITCH framing and field decoding, including fragmented input.

## Architecture

[MarketManager](include/matching/manager/market_manager.h) owns the instrument
registry, active-order index, and four typed pools. It validates commands,
routes them by symbol, and coordinates matching and order lifecycles.

Each [OrderBook](include/matching/book/order_book.h) maintains one instrument's
price levels, FIFO queues, aggregates, and best-price pointers. `OrderNode`
objects link into their containing `LevelNode`; levels link into their
respective AVL trees.

[EventHandler](include/matching/events/event_handler.h) provides the
application's event interface. Callbacks run on the calling thread, must not
re-enter mutating manager operations, and must not retain the supplied object
references after the callback returns.

These are source-level components within one matching library, not separate
runtime layers. Shared order, level, symbol, and update types remain directly
under `include/matching/`; containers and memory pools remain reusable support
code. The original top-level component headers forward to the new locations,
so existing public includes continue to work.

New integrations use `EventHandler`. The original `MarketHandler` name remains
a compatibility alias for existing applications; both names refer to the same
callback interface.

The ITCH decoder and `MarketAdapter` are separate from the matching library.
Historical reconstruction applies executions recorded in the feed, with
automatic matching disabled.

```text
include/matching/manager/   MarketManager API and inline helpers
source/matching/manager/    Command processing, matching, and lifecycle operations
include/matching/book/      OrderBook API and inline helpers
source/matching/book/       Per-instrument book-state implementation
include/matching/events/    EventHandler callback interface (header-only)
include/matching/           Shared domain types and compatibility headers
source/matching/order.cpp   Shared order validation
include/matching/containers/ Intrusive lists, AVL trees, and hash table
include/matching/memory/    Reusable object pools
include/providers/         ITCH messages, decoder, and adapter API
source/providers/          ITCH decoding and adapter implementation
examples/                  Interactive CLI, scenarios, and feed replay
tests/                     Catch2 tests and integration checks
benchmarks/                Matching, container, layout, and ITCH benchmarks
fuzz/                      Malformed-input decoder harness
third_party/Catch2/        Vendored test framework and its license
```

To read the engine, start with the manager's public interface, inspect the
book's stored state, and then review the event callbacks. Follow
`MarketManager::add_order()` to see the three components interact.

## Build and test

Requirements: CMake 3.20 or newer and a C++20 compiler. Catch2 is vendored, so
building the test suite does not require downloading dependencies. Python 3
is only needed for the benchmark runner.

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DMATCHING_BUILD_BENCHMARKS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The suite covers FIFO priority, matching and advanced-order rules,
multi-instrument isolation, container invariants, pool reuse and alignment,
randomized container comparisons, parser fragmentation, and CLI scenarios.

For an AddressSanitizer/UndefinedBehaviorSanitizer build with a supported
Clang or GCC toolchain:

```sh
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMATCHING_ENABLE_SANITIZERS=ON \
  -DMATCHING_BUILD_BENCHMARKS=OFF
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

## Run an example

```sh
./build/examples/matching_cli < tests/data/cli_smoke.txt
./build/examples/matching_cli < examples/scenarios/scenario-06.txt
```

The CLI also accepts interactive commands. This example registers an
instrument, rests a sell order, then partially executes it with a buy:

```text
enable matching
add symbol 7 TEST
add book 7
add order limit sell 1 7 18530 10
add order limit buy 2 7 18535 6
show book 7
show order 1
quit
```

Prices are integer cents: `18530` represents `185.30`. Quantities represent
current open quantity. Active order IDs are unique across a manager, and zero
is reserved as an invalid order ID. Automatic matching starts disabled;
enable it explicitly for matching incoming orders.

## Replay Nasdaq ITCH

Supply an uncompressed TotalView-ITCH 5.0 file locally; market datasets are not
included in the repository.

```sh
./build/examples/itch_replay /path/to/feed.itch --round-cent --chunk-size 65536
```

ITCH prices have four implied decimal places; the engine uses two. Choose
`--exact-cent` to reject prices that cannot be represented exactly, or
`--round-cent` for half-up rounding. Positive prices below half a cent map to
the minimum valid one-cent level. Rounded reconstruction can merge distinct
sub-cent levels and is not an exchange-exact sub-cent book.

## Benchmarks

The recorded portable Release baseline was measured on an Apple M4 Pro with
24 GiB RAM, macOS 26.6.2, and Apple Clang 21 using `-O3 -DNDEBUG` on
September 12, 2026. The engine was single-threaded and was not core-pinned.

| Workload | Recorded median throughput |
| --- | ---: |
| Mixed manager commands with counting callbacks | 46.048 million commands/s |
| Full-day streaming ITCH parsing | 76.493 million messages/s |
| Full-day ITCH reconstruction with counting callbacks | 5.781 million messages/s |

The reconstruction processed 368,366,634 messages from the January 30, 2019
feed. It includes framing, decoding, cent conversion, routing, book updates,
and counting callbacks. It applies recorded feed events and is a different
workload from automatic matching of submitted orders.

The mixed workload includes adds, cancellations, quantity and price changes,
and one-to-one matches. Matching latency samples average 64 commands per batch;
they are not individually timed order-latency percentiles. Measurements exclude
network gateways and client round trips. Results describe the recorded machine
and workload rather than a hardware-independent latency guarantee.

Run the synthetic benchmarks and object-layout report:

```sh
./build/benchmarks/layout_report
./build/benchmarks/matching_latency_benchmark --output benchmark-results/matching
./build/benchmarks/structure_benchmark --output benchmark-results/structures
```

To reproduce the combined measurement protocol with local datasets:

```sh
python3 benchmarks/run_phase19.py \
  --build-dir build \
  --sample /path/to/sample.itch \
  --full-day /path/to/01302019.NASDAQ_ITCH50 \
  --output benchmark-results/baseline
```

The runner retains raw samples, summaries, source and binary hashes, dataset
hashes, and machine metadata. Generated results and profiling files remain
local and are ignored by Git.

## Scope

This is an in-process matching library and replay tool. Networking, persistent
recovery, risk checks, trading sessions, and concurrent instrument sharding
are outside the current implementation. Callers must serialize mutations.
Hash lookup is average O(1), while price-level operations use balanced-tree
traversal; cancelling the last order at a level also removes that tree node.

The vendored Catch2 dependency retains its
[Boost Software License](third_party/Catch2/LICENSE.txt).
