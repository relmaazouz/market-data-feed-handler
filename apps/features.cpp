#include <itch/order_book.hpp>
#include <vector>
#include <itch/features.h>
using namespace itch;


int main(int argc, char* argv[]){

    auto s = itch::extract_features(argv[1], argv[2]);
    std::FILE* out = std::fopen(argv[3], "w");
    std::fprintf(out, "ts,bid_px,bid_sz,ask_px,ask_sz\n");
    for (std::size_t i = 0; i < s.size(); ++i)
        std::fprintf(out, "%llu,%u,%llu,%u,%llu\n",
                     (unsigned long long)s.ts[i], s.bid_px[i], (unsigned long long)s.bid_sz[i],
                     s.ask_px[i], (unsigned long long)s.ask_sz[i]);
    std::fclose(out);
}