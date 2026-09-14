# market-data-feed-handler

A small C++20 library and toolset for parsing Nasdaq TotalView-ITCH 5.0
market-data feeds and reconstructing a live limit order book from them.

- `include/itch/parser.hpp` — wire-format decoder (compile-time, no-vtable
  dispatch to a `Handler` you provide).
- `include/itch/order_book.hpp` — `itch::OrderBook`, a `Handler` that
  reconstructs per-symbol bid/ask depth from the decoded message stream.
- `include/itch/itch.hpp` — umbrella header pulling in both of the above.

## Using this as a library

The library is header-only. To use it from another project:

1. Add this repo's `include/` and `third_party/` directories to your
   include path (e.g. `-Iinclude -Ithird_party`, or just `-Iinclude` if you
   keep `third_party/` alongside it at the relative location it's at here).
2. `#include <itch/itch.hpp>`.
3. Implement a handler (derive from `itch::NullHandler` and override the
   `on_*` callbacks you care about, or use `itch::OrderBook` directly), then
   drive it with `itch::Parser<YourHandler>`.

No linking is required — everything is templates/inline functions.

## Building this repo's binaries

```sh
make            # builds output/itch_feed, output/itch_bench, output/itch_test
make test       # builds and runs the unit tests
./scripts/run.sh              # build + run the benchmark (itch_bench)
./scripts/run.sh feed <file>  # build + run the demo feed handler (itch_feed)
```

Sample data: `./scripts/download.sh` fetches a sample BX ITCH 5.0 day into
`data/`.
