# market-data-feed-handler

[![CI](https://github.com/relmaazouz/market-data-feed-handler/actions/workflows/ci.yml/badge.svg)](https://github.com/relmaazouz/market-data-feed-handler/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)

A from-scratch C++20 Nasdaq TotalView-ITCH 5.0 feed handler, limit order book,
and MoldUDP64 network layer with retransmission-based gap recovery.

**Highlights**

- **Full ITCH 5.0** — all 22 message types decoded into typed structs, each
  with per-message round-trip tests.
- **Cache-optimized order book** — book cost more than halved
  (16.9 → 7.9 book ÷ parser) by replacing node-chasing containers with flat,
  contiguous ones; rejected optimizations (jemalloc, a PMR object pool) were
  measured and documented, not assumed.
- **Gap recovery** — detect → request-once → reorder-buffer → drain, verified
  to rebuild a lossy feed **bit-identical** to the clean, loss-free run.
- **Microstructure study** — order-book imbalance predicts the next mid-price
  move (information coefficient ≈ 0.25, stable out-of-sample, sub-half-spread);
  see **[MICROSTRUCTURE.md](MICROSTRUCTURE.md)**.
- **Python bindings** — feature extraction exposed to Python/NumPy zero-copy via
  pybind11, so the research layer calls straight into the C++ book.

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

## Protocol coverage

All 22 Nasdaq TotalView-ITCH 5.0 message types are decoded into typed structs —
not just the book-relevant subset:

- **System & admin** — system event, stock directory, trading action, Reg SHO,
  market-participant position, MWCB (circuit-breaker) decline/status, IPO
  quoting period, LULD auction collar, operational halt.
- **Order** — add (with/without MPID), execute (with/without price), cancel,
  delete, replace.
- **Trade** — non-cross, cross, broken.
- **Price discovery** — NOII (net order imbalance), RPII, direct-listing
  capital raise.

Each message decodes in a single `switch` in `dispatch`. The `OrderBook`
handler reacts to the five book-mutating types and ignores the rest via
`NullHandler`'s no-op defaults — so unused message types compile away to
nothing. A different handler (say, an auction-imbalance consumer) subscribes to
whichever `on_*` callbacks it needs.

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
  when a packet's sequence jumps ahead, knows exactly which messages were lost.

Because loopback applies backpressure (the sender blocks rather than dropping),
packet loss is **injected deliberately** to exercise detection and recovery —
the same way you'd test a real handler, rather than waiting for the network to
drop a packet.

## Gap recovery

Detection is only half the job; the receiver also *heals* the book when
packets are lost, reconstructing state bit-identical to a clean run.

- **Detect:** the receiver tracks the next expected sequence number; a jump
  means a hole.
- **Request once:** on a new hole it unicasts a retransmission request to the
  sender (first-missing sequence + count), and does *not* re-request while that
  hole is open — avoiding a retransmission storm where a slightly-behind
  receiver re-asks for a growing range on every packet.
- **Buffer, don't advance:** packets that arrive *past* the hole are held in a
  `std::map<seq, bytes>` reorder buffer instead of being applied out of order,
  and `expected` stays parked on the hole.
- **Drain in order:** when the missing messages arrive they are applied and the
  buffer is drained in sequence until the next hole, or until the stream has
  caught up.

The sender is bidirectional: a second UDP socket receives requests, serviced by
a non-blocking poll between sends plus a post-loop drain, so a gap at the very
end of the stream still gets answered before the sender exits.

**Verified:** replaying with `drop_every 1000` (≈5,000 injected losses), the
receiver reports `buffered 0` and a final book **identical** to the clean,
loss-free run — loss happened, and the book is exactly as if it hadn't.

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

The suite also includes **round-trip parser tests**: a byte buffer is built
with distinct known values at each field offset, run through `dispatch`, and
every decoded field is asserted against what went in. Buffers are sized to the
exact message length, so an over-read or over-write is caught as a crash rather
than silently absorbed — which is how the suite caught a buffer overflow in a
big-endian serializer on its first run.

## Python bindings

The feature-extraction path is exposed to Python via **pybind11**, so a research
layer can call straight into the C++ order book — no CSV round-trip. The module
is off by default; enable it at configure time:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DITCH_BUILD_PYTHON=ON
cmake --build build
```

Then, from Python:

```python
import itch_py, pandas as pd   # (or polars — the binding returns plain NumPy columns)
cols = itch_py.extract_features("data/20190130.BX_ITCH_50", "SMH")
df = pd.DataFrame(cols)         # columns: ts, bid_px, bid_sz, ask_px, ask_sz
```

`extract_features` replays the whole file in C++, reconstructs the book, and
returns the per-event top-of-book as columnar NumPy arrays. The C++
`std::vector`s are handed to NumPy **zero-copy** — moved to the heap and owned
by a capsule that frees them on garbage collection — so hundreds of thousands
of rows cross into Python without being copied. It's the fast-core /
Python-research split used in production quant stacks: the research layer and
the CLI `itch_features` call the *same* `itch::extract_features`, so there is
one feature-extraction code path, not two.

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
./build/itch_recv                                     # join multicast, build book, recover gaps
./build/itch_replay data/20190130.BX_ITCH_50          # clean multicast stream
./build/itch_replay data/20190130.BX_ITCH_50 1000     # drop every 1000th packet (tests recovery)
```

With `drop_every`, the receiver should still finish with `buffered 0` and the
same final book as the clean run — proof the retransmit/reorder path heals the
injected loss.

`scripts/run.sh` wraps the benchmark and prints which allocator it used, making
jemalloc an explicit, verified opt-in rather than a silent `LD_PRELOAD`.

## Roadmap

- **End-of-Session marker** — loss of the *final* message is currently
  undetectable (no successor packet reveals the gap); a real feed signals
  end-of-session so trailing loss can be recovered.
- **Re-request timer** — requests fire once; if a request or its retransmit is
  itself lost, the hole never re-asks. Production handlers re-request on a timer.
- **Request coalescing** — with many receivers, identical requests should be
  deduplicated and the retransmit sent once to the group.
- **Per-message latency histogram** (p50 / p99 / p99.9 / max) — HFT cares about
  the worst message, not the average.
- **Multi-message MoldUDP packets** — batch several messages per datagram under
  one header, as the real protocol does.

## Layout

```
include/itch/       parser.hpp, order_book.hpp, itch.hpp, features.h  (header-only library)
apps/               feed.cpp, bench.cpp, replay.cpp, recv.cpp, topsym.cpp, features.cpp
tests/              test_order_book.cpp
python/             itch_py.cpp  (pybind11 module, build with -DITCH_BUILD_PYTHON=ON)
analysis/           microstructure study (Python)
third_party/ankerl/ unordered_dense.h (vendored, pinned)
scripts/            run.sh, download.sh
CMakeLists.txt
```
