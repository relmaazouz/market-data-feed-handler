// Benchmark harness. Isolates parse cost from I/O by reading the whole file
// into RAM first, then times repeated in-memory passes.
//
// Two correctness anchors travel with the timings:
//   * parser checksum   -- also stops the optimizer deleting the decode work
//   * book fingerprint  -- must be identical across any order-book rewrite
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <algorithm>

#include "parser.hpp"
#include "order_book.hpp"

// Force `v` to be materialized here, so the optimizer can't hoist/delete the
// work that produced it or merge identical timed passes. Emits no instructions.
template <class T>
inline void do_not_optimize(const T& v) {
    asm volatile("" : : "r,m"(v) : "memory");
}

// Cheap-but-observable handler: touches decoded fields so the parser can't be
// optimized to nothing. The accumulated `sum` doubles as a decode checksum.
struct Checksum : itch::NullHandler {
    uint64_t sum = 0;
    void on_add_order(const itch::AddOrder& a)              { sum ^= a.order_ref; sum += a.price; }
    void on_order_executed(const itch::OrderExecuted& e)    { sum ^= e.order_ref; sum += e.executed_shares; }
    void on_order_delete(const itch::OrderDelete& d)        { sum ^= d.order_ref; }
    void on_order_replace(const itch::OrderReplace& r)      { sum ^= r.old_order_ref; sum += r.shares; }
    void on_order_cancel(const itch::OrderCancel& c)        { sum ^= c.order_ref; sum -= c.cancelled_shares; }
};

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open file");
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        throw std::runtime_error("Failed to read file");
    return buffer;
}

void report(const char* label, std::vector<double> times, std::size_t n) {
    std::sort(times.begin(), times.end());
    const double best  = times.front();
    const double med   = times[times.size() / 2];
    const double worst = times.back();
    std::printf("%-12s min %.3fs  median %.3fs  max %.3fs  | %7.2f M msg/s\n",
                label, best, med, worst, static_cast<double>(n) / best / 1e6);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <itch-file>\n"
                     "  decompress first:  gunzip data/20190130.BX_ITCH_50.gz\n",
                     argv[0]);
        return 1;
    }

    const auto file_data = read_file(argv[1]);

    // Warm-up run (untimed): warms caches, and captures the message count.
    Checksum cs;
    itch::Parser<Checksum> parser(cs);
    const std::size_t n = parser.parse(file_data.data(), file_data.size());

    std::vector<double> times, book_times;

    // --- Parser-only timing ---
    for (int i = 0; i < 10; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        parser.parse(file_data.data(), file_data.size());
        do_not_optimize(cs.sum);                       // pin the work inside the window
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double>(t1 - t0).count());
    }

    // --- Book correctness fingerprint (untimed): snapshot a populated mid-day book. ---
    {
        itch::OrderBook book;
        itch::Parser<itch::OrderBook> p(book);
        p.parse(file_data.data(), file_data.size() / 4);
        uint64_t fp = book.orders.size();
        for (std::size_t loc = 0; loc < book.books.size(); ++loc) {
            const auto& L = book.books[loc];
            for (const auto& [price, sh] : L.bids) fp += uint64_t(price) * sh + loc;
            for (const auto& [price, sh] : L.asks) fp += uint64_t(price) * sh * 3 + loc;
        }
        std::printf("book fingerprint %llu  (orders %zu)\n",
                    (unsigned long long)fp, book.orders.size());
    }

    // --- Parser + order book timing (fresh book each run). ---
    for (int i = 0; i < 10; ++i) {
        itch::OrderBook book;
        itch::Parser<itch::OrderBook> bp(book);
        const auto t0 = std::chrono::steady_clock::now();
        bp.parse(file_data.data(), file_data.size());
        const auto t1 = std::chrono::steady_clock::now();
        book_times.push_back(std::chrono::duration<double>(t1 - t0).count());
    }

    report("Parser only", times, n);
    std::printf("checksum %llu\n", (unsigned long long)cs.sum);
    report("OrderBook", book_times, n);
    return 0;
}
