#pragma once


#include<array>
#include<cstddef>
#include<cstdint>
#include <cstring>

namespace itch {

    inline uint16_t load_be16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
    }
    inline uint32_t load_be32(const uint8_t* p) noexcept {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
    }
    // 6-byte timestamp (ns since midnight). No native 48-bit type, so we widen.
    inline uint64_t load_be48(const uint8_t* p) noexcept {
        return (uint64_t(p[0]) << 40) | (uint64_t(p[1]) << 32) |
            (uint64_t(p[2]) << 24) | (uint64_t(p[3]) << 16) |
            (uint64_t(p[4]) <<  8) |  uint64_t(p[5]);
    }
    inline uint64_t load_be64(const uint8_t* p) noexcept {
        return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) |
            (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
            (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) |
            (uint64_t(p[6]) <<  8) |  uint64_t(p[7]);
    }



    using Symbol = std::array<char,8>; // _Bytes ascii ticker, space padded


    // Messages:

    struct SystemEvent {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        char event_code;

    };

    struct StockDirectory {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        Symbol stock;
    };

    struct AddOrder {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
        char side;
        uint32_t shares;
        Symbol stock;
        uint32_t price;
        bool attributed;
    };

    struct OrderExecuted {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
        uint32_t executed_shares;
        uint64_t match_number;
    };

    struct OrderExecutedPrice {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
        uint32_t executed_shares;
        uint64_t match_number;
        char printable;
        uint32_t execution_price;
    };

    struct OrderCancel {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
        uint32_t cancelled_shares;
    };

    struct OrderDelete {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
    };

    struct OrderReplace {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t old_order_ref;
        uint64_t new_order_ref;
        uint32_t shares;
        uint32_t price;
    };

    struct Trade {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
        uint64_t order_ref;
        char side;
        uint32_t shares;
        Symbol stock;
        uint32_t price;
        uint64_t match_order;
    };


    struct NullHandler {
        void on_system_event(const SystemEvent& ){}
        void on_stock_directory(const StockDirectory&){}
        void on_add_order(const AddOrder&){}
        void on_order_executed(const OrderExecuted&){}
        void on_order_executed_price(const OrderExecutedPrice&){}
        void on_order_canceled(const OrderCancel&){}
        void on_order_delete(const OrderDelete&){}
        void on_order_replace(const OrderReplace&){}
        void on_trade(const Trade&){}
    };



    template <class Handler>
    inline void dispatch(Handler& h, const uint8_t* m, uint16_t len){

        switch (static_cast<char> (m[0])){
            case 'S': {
                SystemEvent e;
                e.stock_locate = load_be16(m+1);
                e.tracking_number = load_be16(m+3);
                e.timestamp = load_be48(m+5);
                e.event_code = (char) m[11];

                h.on_system_event(e);
                break;
            }

            case 'R': {
                StockDirectory r;
                r.stock_locate = load_be16(m+1);
                r.tracking_number = load_be16(m+3);
                r.timestamp = load_be48(m+5);
                std::memcpy(r.stock.data(),m+11,8);

                h.on_stock_directory(r);
                break;
            }

            case 'A': case 'F': {
                AddOrder a;
                a.stock_locate = load_be16(m+1);
                a.tracking_number = load_be16(m+3);
                a.timestamp = load_be48(m+5);
                a.order_ref = load_be64(m+11);
                a.side = (char)m[19];
                a.shares = load_be32(m+20);
                std::memcpy(a.stock.data(),m+24,8);
                a.price = load_be32(m+32);
                a.attributed = (m[0] == 'F');
                h.on_add_order(a);
                 break;
            }

            case 'E': { 
                OrderExecuted e; 
                e.stock_locate=load_be16(m+1); 
                e.tracking_number=load_be16(m+3);
                e.timestamp=load_be48(m+5); 
                e.order_ref=load_be64(m+11); 
                e.executed_shares=load_be32(m+19);
                e.match_number=load_be64(m+23); 
                h.on_order_executed(e);break;
            }

            case 'C': { 
                OrderExecutedPrice c; 
                c.stock_locate=load_be16(m+1); 
                c.tracking_number=load_be16(m+3);
                c.timestamp=load_be48(m+5); 
                c.order_ref=load_be64(m+11); 
                c.executed_shares=load_be32(m+19);
                c.match_number=load_be64(m+23); 
                c.printable=(char)m[31]; 
                c.execution_price=load_be32(m+32);
                h.on_order_executed_price(c); break; 
            }

            case 'X': { 
                OrderCancel x; 
                x.stock_locate=load_be16(m+1); 
                x.tracking_number=load_be16(m+3);
                x.timestamp=load_be48(m+5); 
                x.order_ref=load_be64(m+11); 
                x.cancelled_shares=load_be32(m+19);
                h.on_order_cancel(x); 
                break; 
            }

            case 'D': { 
                OrderDelete d; 
                d.stock_locate=load_be16(m+1); 
                d.tracking_number=load_be16(m+3);
                d.timestamp=load_be48(m+5); 
                d.order_ref=load_be64(m+11); 
                h.on_order_delete(d); 
                break; 
            }

            case 'U': { 
                OrderReplace u; 
                u.stock_locate=load_be16(m+1); 
                u.tracking_number=load_be16(m+3);
                u.timestamp=load_be48(m+5); 
                u.old_order_ref=load_be64(m+11); 
                u.new_order_ref=load_be64(m+19);
                u.shares=load_be32(m+27); 
                u.price=load_be32(m+31); 
                h.on_order_replace(u); 
                break; 
            }

            case 'P': { 
                Trade p; 
                p.stock_locate=load_be16(m+1); 
                p.tracking_number=load_be16(m+3);
                p.timestamp=load_be48(m+5); 
                p.order_ref=load_be64(m+11); 
                p.side=(char)m[19];
                p.shares=load_be32(m+20); 
                std::memcpy(p.stock.data(),m+24,8); 
                p.price=load_be32(m+32);
                p.match_order=load_be64(m+36); 
                h.on_trade(p); 
                break; 
            }

            default: h.on_other(static_cast<char>(m[0]), m, len); 
            break;

        }

    };


    template <class Handler>
    class MyParser{
        private:
            Handler& h_;

        public:
            explicit MyParser(Handler& h) : h_(h){}


            std::size_t parse(const uint8_t* data, std::size_t size) {
                std::size_t pos = 0, count = 0;

                while(pos+2 <= size){
                    const uint16_t len = load_be16(data + pos);
                    if(len==0){break;}
                    if(len + pos +2 > size) {break;}

                    dispatch(h_,data+pos+2,len);
                    pos += 2 + static_cast<std::size_t>(len);
                    count++;
                }
                return count;
            }

    };

}