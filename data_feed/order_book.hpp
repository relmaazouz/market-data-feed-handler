#pragma once


#include<unordered_map>
#include<map>
#include <stdexcept>

#include "parser.hpp"


namespace itch {

    struct OrderInfo {
        uint16_t stock_locate;
        char side;
        uint32_t price;
        uint32_t shares;

    };

    struct OrderBook: itch::NullHandler {

        std::unordered_map<uint64_t, OrderInfo> orders;
        std::size_t peak = 0;
        std::size_t filled = 0;

        struct Levels {
            std::map<uint32_t, uint64_t> bids;
            std::map<uint32_t, uint64_t> asks;
        };

        std::unordered_map<uint16_t, Levels> books;



        void on_add_order(const AddOrder& a){
            OrderInfo o_info;
            o_info.stock_locate = a.stock_locate;
            o_info.side = a.side;
            o_info.price = a.price;
            o_info.shares = a.shares;

            orders[a.order_ref] = o_info;
            add_level(a.stock_locate, a.side, a.price, a.shares);
            if(orders.size()>peak) {
                peak = orders.size();
            }
        }

        void on_order_delete(const OrderDelete& d){
            auto it = orders.find(d.order_ref);
            if(it == orders.end()) return;

            remove_level(it->second.stock_locate, it->second.side, it->second.price, it->second.shares);
            orders.erase(d.order_ref);
        }

        void on_order_executed(const OrderExecuted& e){
            auto it = orders.find(e.order_ref);
            if (it == orders.end()) return;

            remove_level(it->second.stock_locate, it->second.side, it->second.price, e.executed_shares);

            it->second.shares -= e.executed_shares;
            if(it->second.shares ==0) {orders.erase(it);}
        }


        void on_order_cancel(const OrderCancel& c){
            auto it = orders.find(c.order_ref);
            if(it== orders.end()) return;

            remove_level(it->second.stock_locate, it->second.side, it->second.price, c.cancelled_shares);

            it->second.shares -= c.cancelled_shares;
            if(it->second.shares ==0) {orders.erase(it);}
        }

        void on_order_replace(const OrderReplace& r){
            auto it = orders.find(r.old_order_ref);
            if(it == orders.end()) return;
            
            OrderInfo o = it->second;

            remove_level(o.stock_locate, o.side, o.price, o.shares);

            // erase befor inserting, as insert may trigger 
            // a map rehash (when it grows past its load factor)
            orders.erase(it);

            o.price = r.price;
            o.shares = r.shares;
            orders[r.new_order_ref] = o; //insert can rehash now — we no longer need it
            add_level(o.stock_locate, o.side, o.price, o.shares);
            
        }


        void add_level(uint16_t locate, char side, uint32_t price, uint32_t shares){
            auto& m = (side=='B') ? books[locate].bids : books[locate].asks;
            
            // increment the price level with the number of shares
            m[price] += shares;
        }


        void remove_level(uint16_t locate, char side, uint32_t price, uint32_t shares){
             auto& m = (side=='B') ? books[locate].bids : books[locate].asks;
            
             auto it = m.find(price);

             if(it == m.end()) return;

            it->second -= shares;
            
            if(it->second == 0) m.erase(it);
        }

        
    };




}