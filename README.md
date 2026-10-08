# market-data-feed-handler

A C++20 library and toolset for parsing Nasdaq TotalView-ITCH 5.0 market-data
feeds, reconstructing a live limit order book, and delivering that feed over
UDP multicast with **MoldUDP64** framing and sequence-number gap detection.

Built as a learning and portfolio project, working toward low-latency / HFT
systems development. Every line is hand-written; the emphasis is correctness
first, measure-before-optimize throughout — including optimizations that were
tried, measured, and *reverted* because the data said so.

---

## Library

The core is header-only:

- `include/itch/parser.hpp` — wire-format decoder with compile-time,
  no-vtable dispatch to a `Handler` you provide.
- `include/itch/order_book.hpp` — `itch::OrderBook`, a `Handler` that
  reconstructs per-symbol bid/ask depth from the decoded message stream.
- `include/itch/itch.hpp` — umbrella header pulling in both of the above.

### Using it from another project

Header-only, no linking required:

1. Add `include/` and `third_party/` to your include path
   (`-Iinclude -Ithird_party`).
2. `#include <itch/itch.hpp>`.
3. Implement a handler (derive from `itch::NullHandler` and override the
   `on_*` callbacks you need, or use `itch::OrderBook` directly), then drive
   it with `itch::Parser<YourHandler>`.

## The order book — locality over cleverness

The book is built for cache locality: flat, contiguous structures instead of
node-based trees and hash maps wherever the access pattern allows.

- **`books`** is a flat `std::vector<Levels>` indexed **directly** by
  `stock_locate` (a dense 16-bit symbol id) — a direct array index, no hashing.
- **Price levels** are a sorted `std::vector` per side (descending by price),
  not a `std::map`. Updates are a binary search + in-place merge/insert; best
  bid is `front()`, best ask is `back()`.
- **`orders`** is an open-addressing hash map (`ankerl::unordered_dense`),
  `reserve()`d once past peak — no per-node allocation, lookups stay in cache.

## The optimization story

The parser runs at ~110–130 M msg/s (~8 ns/message) once I/O is isolated, and
was never the bottleneck; the order book is ~90% of the pipeline, so that is
where the work went. Progress is tracked as the ratio **book-time ÷
parser-time**, which cancels out the machine-speed drift of a shared cloud VM
(raw seconds aren't comparable across runs; the ratio is).

| Change to the order book                              | book ÷ parser |
|-------------------------------------------------------|:-------------:|
| `std::map` levels, hash-map `books`  (baseline)       | ~16.9         |
| flat `std::vector` `books`, indexed by locate         | ~11.0         |
| + sorted-vector price levels                          |  ~9.2         |
| + `reserve()`d orders map                             |  ~8.9         |
| + open-addressing (`unordered_dense`) orders map      |  **~7.9**     |

**The order book's cost was more than halved (16.9 → 7.9), entirely by
replacing node-chasing containers with flat, contiguous ones.**

The most useful finding is a negative one: a polymorphic-memory-resource
**object pool** for the orders map, and jemalloc as a permanent allocator, were
both implemented, measured, and *rejected* — they lost to the system allocator,
which caches fixed-size nodes well. Every win came from attacking
**pointer-chasing** (cache misses), not allocation. Each change above was
A/B-tested in a single session on the same machine and kept only if it
measurably won.

## Networking (MoldUDP64)

A replayer streams the ITCH file as UDP multicast, and a receiver rebuilds the
book live from the socket:

- **`itch_replay`** reads the file and sends each message wrapped in a 20-byte
  MoldUDP64 header — a 10-byte session id, an 8-byte big-endian **sequence
  number**, and a 2-byte message count — the transport Nasdaq actually delivers
  ITCH in.
- **`itch_recv`** joins the multicast group, reads the header, and reconstructs
  the order book live — the same `dispatch`/`OrderBook` code as the file path,
  now sourced from the network.
- **Gap detection**: the receiver tracks the next expected sequence number and,
  when a packet's sequence jumps ahead, computes exactly which messages were
  lost (`missing = seq − expected`). Verified on real data by deliberately
  dropping every Nth packet in the replayer (`drop_every` argument) and
  confirming the receiver reports exactly those gaps.

Because loopback applies backpressure (the sender blocks rather than dropping),
packet loss is **injected deliberately** to exercise gap detection — the same
way you'd test a real handler, rather than waiting for the network to drop a
packet.

## Correctness

Two invariants travel with every build and must not move across any refactor:

- **Parser checksum** — a fold of decoded fields; also stops the optimizer
  deleting the decode work being benchmarked.
- **Book fingerprint** — a fold of every price level in a *populated
  mid-session* book (the end-of-day book drains empty, so it's snapshotted
  mid-day). Any order-book rewrite must reproduce it **exactly**.

On top of the invariants, `tests/` holds a dependency-free unit suite that
drives the handlers directly and asserts on book state — one test per class of
bug the project has hit. The suite is verified to *fail* when a check is
deliberately broken, so a green result means something.

## Build

Requires a C++20 compiler and CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure     # run the unit tests
```

## Data

Sample ITCH files are published by Nasdaq at <https://emi.nasdaq.com/ITCH/>.
The data is gitignored (each file is ~0.4–1.7 GB); fetch one with the helper:

```sh
./scripts/download.sh            # 20190130.BX_ITCH_50 (~1.1 GB gz → ~2.4 GB)
```

## Run

```sh
# benchmark (parser vs. book, with correctness anchors) and file demo:
./build/itch_bench data/20190130.BX_ITCH_50
./build/itch_feed  data/20190130.BX_ITCH_50

# networked path — two terminals, receiver first:
./build/itch_recv                                     # join multicast, build book, detect gaps
./build/itch_replay data/20190130.BX_ITCH_50          # clean multicast stream
./build/itch_replay data/20190130.BX_ITCH_50 1000     # drop every 1000th packet (tests gap detection)
```

`scripts/run.sh` wraps the benchmark and prints which allocator it used, making
jemalloc an explicit, verified opt-in rather than a silent `LD_PRELOAD`.

## Roadmap

- **Gap recovery** — on a detected gap, request retransmission of the missing
  sequence range, buffer out-of-order packets, and apply them in order so the
  book stays correct despite loss.
- **Per-message latency histogram** (p50 / p99 / p99.9 / max) — HFT cares about
  the worst message, not the average.
- **Full message coverage** — the imbalance / auction / admin message types
  beyond the ~10 book-relevant ones decoded today.
- **Multi-message MoldUDP packets** — batch several messages per datagram under
  one header, as the real protocol does.

## Layout

```
include/itch/       parser.hpp, order_book.hpp, itch.hpp  (header-only library)
apps/               feed.cpp, bench.cpp, replay.cpp, recv.cpp
tests/              test_order_book.cpp
third_party/ankerl/ unordered_dense.h (vendored, pinned)
scripts/            run.sh, download.sh
CMakeLists.txt
```