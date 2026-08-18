// Feed handler demo: mmap an ITCH 5.0 file, reconstruct the book, print
// throughput and a mid-session top-of-book snapshot.
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "parser.hpp"
#include "order_book.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <itch-file>\n"
                     "  decompress first:  gunzip data/20190130.BX_ITCH_50.gz\n",
                     argv[0]);
        return 1;
    }

    int fd = ::open(argv[1], O_RDONLY);
    if (fd < 0) { std::perror("open"); return 1; }

    struct stat st{};
    if (::fstat(fd, &st)) { std::perror("fstat"); ::close(fd); return 1; }
    const std::size_t size = static_cast<std::size_t>(st.st_size);

    void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) { std::perror("mmap"); ::close(fd); return 1; }
    ::madvise(addr, size, MADV_SEQUENTIAL);
    const auto* data = static_cast<const uint8_t*>(addr);

    // --- Full-day pass: throughput + peak depth. ---
    itch::OrderBook book;
    itch::Parser<itch::OrderBook> parser(book);

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t n = parser.parse(data, size);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    std::printf("parsed %zu messages from %.1f MB in %.3f s  (%.2f M msg/s)\n",
                n, static_cast<double>(size) / 1e6, secs,
                secs > 0 ? static_cast<double>(n) / secs / 1e6 : 0.0);
    std::printf("peak resting orders: %zu   final: %zu  (books drain at the close)\n",
                book.peak, book.orders.size());

    // --- Mid-session snapshot for a live top-of-book. ---
    // The end-of-day book is empty, so snapshot a populated mid-morning state.
    itch::OrderBook mid;
    itch::Parser<itch::OrderBook> midp(mid);
    midp.parse(data, size / 4);

    std::printf("\ntop of book (mid-session snapshot):\n");
    int shown = 0;
    for (std::size_t locate = 0; locate < mid.books.size(); ++locate) {
        const auto& L = mid.books[locate];
        if (L.bids.empty() || L.asks.empty()) continue;

        const double bid = L.bids.front().price / 10000.0;   // best bid = highest
        const double ask = L.asks.back().price  / 10000.0;   // best ask = lowest

        auto sit = mid.symbol_by_locate.find(static_cast<uint16_t>(locate));
        const char* sym = (sit != mid.symbol_by_locate.end()) ? sit->second.c_str() : "?";

        std::printf("  %-8s bid %.4f  ask %.4f\n", sym, bid, ask);
        if (++shown == 10) break;
    }

    ::munmap(addr, size);
    ::close(fd);
    return 0;
}
