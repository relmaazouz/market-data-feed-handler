#include<cstdio>
#include<unistd.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<chrono>
#include<unordered_map>
#include<string>
#include<cstddef>
#include<cstdint>
#include<array>

#include "myparser.hpp"

using namespace itch;

namespace {

    std::string trim_symbol(const Symbol& s){
        std::size_t n = s.size();
        while(n>0 && s[n-1] == ' '){
            n--;
        }
        return std::string(s.data(), n);
    }

    struct StatsHandler: NullHandler {
        std::array<uint64_t, 128> counts{};
        std::unordered_map<uint16_t, std::string> symbol_by_locate;
        int sample_left = 15;
        
        void bump(char t) { ++counts[static_cast<unsigned char>(t) & 0x7f]; }
        void on_system_event(const SystemEvent&) {bump('S');}
        void on_stock_directory(const StockDirectory& r) {
            bump('R');
            symbol_by_locate.emplace(r.stock_locate, trim_symbol(r.stock));
        }

        void on_add_order(const AddOrder& a){
            bump(a.attributed? 'F':'A');
            if(sample_left-- > 0){
                std::printf("ADD  ts=%015llu ref=%llu  %c %7u %-8s @ %.4f\n",
                        (unsigned long long)a.timestamp,
                        (unsigned long long)a.order_ref, a.side, a.shares,
                        trim_symbol(a.stock).c_str(), a.price / 10000.0);
            }
            // std::fflush(stdout);

        }
        void on_order_executed(const OrderExecuted&)            { bump('E'); }
        void on_order_executed_price(const OrderExecutedPrice&) { bump('C'); }
        void on_order_cancel(const OrderCancel&)                { bump('X'); }
        void on_order_delete(const OrderDelete&)                { bump('D'); }
        void on_order_replace(const OrderReplace&)              { bump('U'); }
        void on_trade(const Trade&)                             { bump('P'); }
        void on_other(char t, const uint8_t*, uint16_t)         { bump(t); }

    };
}

int main(int argc, char** argv) {
    // check if the launch is done correctly
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <itch-file>\n"
                            "  decompress first:  gunzip data/20190130.BX_ITCH_50.gz\n",
                    argv[0]);
        return 1;
    }
    // open the file
    int fd = ::open(argv[1],O_RDONLY);
    //check if the files can be opened
    if(fd < 0) {std::perror("open"); return 1;}

    struct stat st{};
    if(::fstat(fd,&st)) {std::perror("fstat"); ::close(fd); return 1;}
    const std::size_t size  = static_cast<std::size_t> (st.st_size);
    void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(addr == MAP_FAILED) {std::perror("mmap"); ::close(fd); return 1;}
    ::madvise(addr, size, MADV_SEQUENTIAL);

    StatsHandler h;
    MyParser<StatsHandler> myParser(h);

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t n = myParser.parse(static_cast<const uint8_t*>(addr), size);
    const auto t1 = std::chrono::steady_clock::now();

    const double secs = std::chrono::duration<double>(t1-t0).count();

    std::printf("\nparsed %zu messages from %.1f MB in %.3f s  (%.2f M msg/s)\n",
            n, size / 1e6, secs, secs > 0 ? n / secs / 1e6 : 0.0);
    std::printf("symbols seen: %zu\n\nmessage counts:\n", h.symbol_by_locate.size());

    struct Row { char key; const char* name; };

    static const Row rows[] = {
        {'S',"system-event"},
        {'R',"stock-directory"},
        {'A',"add-order"},
        {'F',"add-order-mpid"},
        {'E',"executed"},
        {'C',"executed-w-price"},
        {'X',"cancel"},{'D',"delete"},
        {'U',"replace"},
        {'P',"trade"},
        {'Q',"cross-trade"},
        {'B',"broken-trade"},
    };

    for (const auto& r : rows) {
        uint64_t c = h.counts[static_cast<unsigned char>(r.key) & 0x7f];
        if (c) std::printf("  %-18s %12llu\n", r.name, (unsigned long long)c);
    }

    ::munmap(addr, size);
    ::close(fd);
    return 0;

    
}