//
// Created by Rachid EL MAAZOUZ on 01/11/2026.
//

#ifndef MARKET_DATA_FEED_HANDLER_FEATURES_H
#define MARKET_DATA_FEED_HANDLER_FEATURES_H
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "itch/order_book.hpp"
#include "itch/itch.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


namespace itch {
    struct FeatureSeries {
        std::vector<uint64_t> ts;
        std::vector<uint32_t> bid_px;
        std::vector<uint64_t> bid_sz;
        std::vector<uint32_t> ask_px;
        std::vector<uint64_t> ask_sz;
        std::size_t size() const {return ts.size();}
    };


    struct FeatureEmitter: NullHandler {
        OrderBook book;
        std::string want;
        uint16_t target = 0;
        bool target_set = false;
        FeatureSeries out;

        uint32_t pbid_px = 0, pask_px = 0;
        uint64_t pbid_sz = 0, pask_sz = 0;
        bool have_prev = false;

        void on_stock_directory(const StockDirectory& r) {
            book.on_stock_directory(r);
            if (!target_set && trim_symbol(r.stock) == want ) {
                target = r.stock_locate;
                target_set = true;
            }
        }


        void maybe_emit(uint64_t ts) {
            auto b = book.best_bid(target);
            auto a = book.best_ask(target);
            if (b.size == 0 || a.size == 0) return;
            if (have_prev && b.price == pbid_px && a.price == pask_px && b.size == pbid_sz && a.size == pask_sz) return;

            out.ts.push_back(ts);
            out.bid_px.push_back(b.price);
            out.bid_sz.push_back(b.size);
            out.ask_px.push_back(a.price);
            out.ask_sz.push_back(a.size);


            pbid_px = b.price;
            pask_px = a.price;
            pbid_sz = b.size;
            pask_sz = a.size;
            have_prev = true;
        }

        void on_add_order(const AddOrder& a) {
            book.on_add_order(a);
            if(target_set && a.stock_locate==target) maybe_emit(a.timestamp);
        }

        void on_order_delete(const OrderDelete& d) {
            book.on_order_delete(d);
            if(target_set && d.stock_locate==target) maybe_emit(d.timestamp);
        }

        void on_order_executed(const OrderExecuted& e) {
            book.on_order_executed(e);
            if(target_set && e.stock_locate==target) maybe_emit(e.timestamp);
        }

        void on_order_executed_price(const OrderExecutedPrice& c) {
            book.on_order_executed_price(c);
            if(target_set && c.stock_locate==target) maybe_emit(c.timestamp);
        }

        void on_order_cancel(const OrderCancel& c) {
            book.on_order_cancel(c);
            if(target_set && c.stock_locate==target) maybe_emit(c.timestamp);
        }

        void on_order_replace(const OrderReplace& r) {
            book.on_order_replace(r);
            if(target_set && r.stock_locate==target) maybe_emit(r.timestamp);
        }

    };
    inline FeatureSeries extract_features(const std::string& path, const std::string& symbol) {

        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return {};

        struct stat st{};
        if (::fstat(fd, &st)) { ::close(fd); return {}; }
        const std::size_t size = static_cast<std::size_t>(st.st_size);

        void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) { ::close(fd); return {}; }
        ::madvise(addr, size, MADV_SEQUENTIAL);
        const auto* data = static_cast<const uint8_t*>(addr);

        FeatureEmitter f;
        f.want = symbol;
        itch::Parser<FeatureEmitter> parser{f};
        parser.parse(data, size);

        ::munmap(addr, size);
        ::close(fd);
        return f.out;
    };

}



#endif //MARKET_DATA_FEED_HANDLER_FEATURES_H
