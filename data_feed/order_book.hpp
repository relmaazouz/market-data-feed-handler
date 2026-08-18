#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>

#include "parser.hpp"

namespace itch {

    struct OrderInfo {
        uint16_t stock_locate;
        char     side;
        uint32_t price;
        uint32_t shares;
    };

    // A full limit order book reconstructed from the ITCH stream.
    //
    // Two indexes, kept in sync:
    //   orders : order_ref -> OrderInfo         (per-order state, for O(1) lookup on execute/cancel/delete/replace)
    //   books  : stock_locate -> price levels   (aggregate depth per symbol)
    //
    // Both are laid out for cache locality:
    //   * books is a flat vector indexed directly by stock_locate (dense, [0,65535]).
    //   * each side's levels are a sorted vector (descending by price), not a tree.
    struct OrderBook : itch::NullHandler {

        OrderBook() { orders.reserve(100000); }   // size past peak (~73k) once; avoid rehashing

        std::unordered_map<uint64_t, OrderInfo> orders;
        std::size_t peak = 0;

        struct Level {
            uint32_t price;
            uint64_t shares;   // aggregate shares resting at this price (can exceed 32 bits)
        };
        struct Levels {
            std::vector<Level> bids;   // sorted descending by price: best bid = front()
            std::vector<Level> asks;   // sorted descending by price: best ask = back()
        };

        std::vector<Levels> books = std::vector<Levels>(65536);   // indexed by stock_locate

        std::unordered_map<uint16_t, std::string> symbol_by_locate;

        void on_stock_directory(const StockDirectory& r) {
            symbol_by_locate[r.stock_locate] = trim_symbol(r.stock);
        }

        void on_add_order(const AddOrder& a) {
            orders[a.order_ref] = OrderInfo{a.stock_locate, a.side, a.price, a.shares};
            add_level(a.stock_locate, a.side, a.price, a.shares);
            if (orders.size() > peak) peak = orders.size();
        }

        void on_order_delete(const OrderDelete& d) {
            auto it = orders.find(d.order_ref);
            if (it == orders.end()) return;
            remove_level(it->second.stock_locate, it->second.side, it->second.price, it->second.shares);
            orders.erase(it);
        }

        void on_order_executed(const OrderExecuted& e) {
            auto it = orders.find(e.order_ref);
            if (it == orders.end()) return;
            remove_level(it->second.stock_locate, it->second.side, it->second.price, e.executed_shares);
            it->second.shares -= e.executed_shares;
            if (it->second.shares == 0) orders.erase(it);
        }

        // Execute-with-price: shares leave the book at the order's DISPLAY price.
        void on_order_executed_price(const OrderExecutedPrice& c) {
            auto it = orders.find(c.order_ref);
            if (it == orders.end()) return;
            remove_level(it->second.stock_locate, it->second.side, it->second.price, c.executed_shares);
            it->second.shares -= c.executed_shares;
            if (it->second.shares == 0) orders.erase(it);
        }

        void on_order_cancel(const OrderCancel& c) {
            auto it = orders.find(c.order_ref);
            if (it == orders.end()) return;
            remove_level(it->second.stock_locate, it->second.side, it->second.price, c.cancelled_shares);
            it->second.shares -= c.cancelled_shares;
            if (it->second.shares == 0) orders.erase(it);
        }

        void on_order_replace(const OrderReplace& r) {
            auto it = orders.find(r.old_order_ref);
            if (it == orders.end()) return;

            OrderInfo o = it->second;                 // copy out BEFORE erase (no dangling ref across insert)
            remove_level(o.stock_locate, o.side, o.price, o.shares);
            orders.erase(it);

            o.price = r.price;
            o.shares = r.shares;
            orders[r.new_order_ref] = o;
            add_level(o.stock_locate, o.side, o.price, o.shares);
        }

        // ---- level maintenance on the sorted vectors ----

        void add_level(uint16_t locate, char side, uint32_t price, uint32_t shares) {
            auto& m = (side == 'B') ? books[locate].bids : books[locate].asks;
            auto it = std::lower_bound(m.begin(), m.end(), price,
                                       [](const Level& l, uint32_t p) { return l.price > p; });
            if (it != m.end() && it->price == price) {
                it->shares += shares;                 // merge into existing level
            } else {
                m.insert(it, Level{price, static_cast<uint64_t>(shares)});  // keep sorted
            }
        }

        void remove_level(uint16_t locate, char side, uint32_t price, uint32_t shares) {
            auto& m = (side == 'B') ? books[locate].bids : books[locate].asks;
            auto it = std::lower_bound(m.begin(), m.end(), price,
                                       [](const Level& l, uint32_t p) { return l.price > p; });
            if (it == m.end() || it->price != price) return;
            it->shares -= shares;
            if (it->shares == 0) m.erase(it);
        }
    };

}  // namespace itch
