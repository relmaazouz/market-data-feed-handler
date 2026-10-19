#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <itch/itch.hpp>

using namespace itch;

struct Counter : itch::NullHandler {
    std::vector<uint64_t> counts = std::vector<uint64_t>(65536,0);
    std::unordered_map<uint16_t, std::string> symbol_by_locate;
    
    void on_stock_directory(const StockDirectory& r) {
        symbol_by_locate[r.stock_locate] = trim_symbol(r.stock);
    }

    void on_add_order(const AddOrder& a)                      {++counts[a.stock_locate];}
    void on_order_delete(const OrderDelete& d)                {++counts[d.stock_locate];}
    void on_order_executed(const OrderExecuted& e)            {++counts[e.stock_locate];}
    void on_order_executed_price(const OrderExecutedPrice& c) {++counts[c.stock_locate];}
    void on_order_cancel(const OrderCancel& c)                {++counts[c.stock_locate];}
    void on_order_replace(const OrderReplace& r)              {++counts[r.stock_locate];}

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

    Counter counter;
    itch::Parser<Counter> parser{counter};


    parser.parse(data, size);

    std::vector<std::pair<uint64_t, uint16_t>> ranked;
    for(uint32_t loc = 0; loc <counter.counts.size(); loc++){
        if(counter.counts[loc] > 0){
            ranked.emplace_back(counter.counts[loc], static_cast<uint16_t>(loc));
        }
    }

    std::sort(ranked.begin(), ranked.end(), std::greater<>{});
    std::size_t n = std::min<std::size_t>(10, ranked.size());
    for(std::size_t i = 0; i<n; i++){
        auto [count, loc] = ranked[i];
        auto it = counter.symbol_by_locate.find(loc);
        std::string sym =  (it!= counter.symbol_by_locate.end()) ? it->second : ("locate#"+std::to_string(loc));
        std::printf("%-8s %lu\n", sym.c_str(), count);

    }

}