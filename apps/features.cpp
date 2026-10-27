#include <itch/order_book.hpp>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

using namespace itch;

struct Features: NullHandler {
    OrderBook   book;
    std::string want;
    uint16_t    target = 0;
    bool        target_set = false;
    std::FILE*  out = nullptr;

    // last emitted top-of-book, to detect changes
    uint32_t pbid_px = 0, pask_px = 0;
    uint64_t pbid_sz = 0, pask_sz = 0;
    bool     have_prev = false;


    void on_stock_directory(const StockDirectory& r) {
        book.on_stock_directory(r);
        if (trim_symbol(r.stock) == want) {
            target = r.stock_locate;
            target_set = true;
        }
    }


    void on_add_order(const AddOrder& a) {
        book.on_add_order(a);
        if (target_set && a.stock_locate == target) {
            maybe_emit(a.timestamp);
        }
    }

    void on_order_delete(const OrderDelete& d) {
        book.on_order_delete(d);
        if (target_set && d.stock_locate == target) {
            maybe_emit(d.timestamp);
        }
        
    }

    void on_order_executed(const OrderExecuted& e) {
        book.on_order_executed(e);
        if (target_set && e.stock_locate == target) {
            maybe_emit(e.timestamp);
        }
    }

    void on_order_executed_price(const OrderExecutedPrice& c) {
        book.on_order_executed_price(c);
        if (target_set && c.stock_locate == target) {
            maybe_emit(c.timestamp);
        }
    }

    void on_order_cancel(const OrderCancel& c) {
        book.on_order_cancel(c);
        if (target_set && c.stock_locate == target) {
            maybe_emit(c.timestamp);
        }
    }

    void on_order_replace(const OrderReplace& r) {
        book.on_order_replace(r);
        if (target_set && r.stock_locate == target) {
            maybe_emit(r.timestamp);
        }
    }

    void maybe_emit(uint64_t ts) {
        auto b = book.best_bid(target);
        auto a = book.best_ask(target);
        if (b.size == 0 || a.size == 0) return;

        if (!target_set) return;
        const auto& levels = book.books[target];
        if (levels.bids.empty() || levels.asks.empty()) return;

        // emit if the top of the book actually changed (price or size) since last emit
        if(have_prev 
            && b.price == pbid_px 
            && a.price == pask_px 
            && b.size == pbid_sz 
            && a.size == pask_sz) return;


        if (!have_prev || b.price != pbid_px || a.price != pask_px || b.size != pbid_sz || a.size != pask_sz) {
            pbid_px = b.price;
            pbid_sz = b.size;
            pask_px = a.price;
            pask_sz = a.size;
            have_prev = true;
        }

        std::fprintf(out, "%llu,%u,%lu,%u,%lu\n", (unsigned long long)ts, b.price, (unsigned long long)b.size, a.price, (unsigned long long)a.size);

        pbid_px = b.price; pbid_sz = b.size;
        pask_px = a.price; pask_sz = a.size;
        have_prev = true;
    }
};



int main(int argc, char* argv[]){

    //load the file bytes
    int fd = ::open(argv[1], O_RDONLY);
    if (fd < 0) { std::perror("open"); return 1; }

    struct stat st{};
    if (::fstat(fd, &st)) { std::perror("fstat"); ::close(fd); return 1; }
    const std::size_t size = static_cast<std::size_t>(st.st_size);

    void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) { std::perror("mmap"); ::close(fd); return 1; }
    ::madvise(addr, size, MADV_SEQUENTIAL);
    const auto* data = static_cast<const uint8_t*>(addr);

    std::FILE* out = std::fopen(argv[3], "w");
    if (!out) { std::perror("fopen"); return 1; }
    std::fprintf(out, "ts,bid_px,bid_sz,ask_px,ask_sz\n");

    Features f;
    f.want = argv[2];
    f.out  = out;

    itch::Parser<Features> parser{f};
    parser.parse(data, size);

    std::fclose(out);
    ::munmap(addr, size);
    ::close(fd);

    return 0;
}