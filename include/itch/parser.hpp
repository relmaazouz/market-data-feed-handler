#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>   // std::memcpy
#include <string>    // std::string

namespace itch {

    struct Header {
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp;
    };

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

    inline void store_be16(uint8_t* p, uint16_t v) noexcept{
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    }

    inline void store_be32(uint8_t* p, uint32_t v) noexcept {
        p[0] = uint8_t(v>>24);
        p[1] = uint8_t(v>>16);
        p[2] = uint8_t(v>>8);
        p[3] = uint8_t(v);
    }

    inline void store_be48(uint8_t* p, uint64_t v) noexcept {
        p[0] = static_cast<uint8_t>(v >> 40);
        p[1] = static_cast<uint8_t>(v >> 32);
        p[2] = static_cast<uint8_t>(v >> 24);
        p[3] = static_cast<uint8_t>(v >> 16);
        p[4] = static_cast<uint8_t>(v >> 8);
        p[5] = static_cast<uint8_t>(v);
    }

    inline void store_be64(uint8_t* p, uint64_t v) noexcept{
        p[0] = static_cast<uint8_t>(v >> 56);
        p[1] = static_cast<uint8_t>(v >> 48);     
        p[2] = static_cast<uint8_t>(v >> 40);
        p[3] = static_cast<uint8_t>(v >> 32);
        p[4] = static_cast<uint8_t>(v >> 24);
        p[5] = static_cast<uint8_t>(v >> 16);
        p[6] = static_cast<uint8_t>(v >> 8);
        p[7] = static_cast<uint8_t>(v);
    }

    inline void load_header(Header& h, const uint8_t* m) noexcept {
        h.stock_locate = load_be16(m+1);
        h.tracking_number = load_be16(m + 3);
        h.timestamp = load_be48(m + 5);
    }

    using Symbol = std::array<char, 8>; // 8-byte ascii ticker, space padded

    inline std::string trim_symbol(const Symbol& s) {
        std::size_t n = s.size();
        while (n > 0 && s[n - 1] == ' ') --n;
        return std::string(s.data(), n);
    }

    // ---- Message structs (decoded, host-endian) ----

    struct SystemEvent: Header {
        char     event_code;
    };

    struct StockDirectory: Header {
        Symbol   stock;
    };

    struct AddOrder: Header {
        uint64_t order_ref;
        char     side;
        uint32_t shares;
        Symbol   stock;
        uint32_t price;
        bool     attributed;
    };

    struct OrderExecuted: Header {
        uint64_t order_ref;
        uint32_t executed_shares;
        uint64_t match_number;
    };

    struct OrderExecutedPrice: Header {
        uint64_t order_ref;
        uint32_t executed_shares;
        uint64_t match_number;
        char     printable;
        uint32_t execution_price;
    };

    struct OrderCancel: Header {
        uint64_t order_ref;
        uint32_t cancelled_shares;
    };

    struct OrderDelete: Header {
        uint64_t order_ref;
    };

    struct OrderReplace: Header {
        uint64_t old_order_ref;
        uint64_t new_order_ref;
        uint32_t shares;
        uint32_t price;
    };

    struct Trade: Header {
        uint64_t order_ref;
        char     side;
        uint32_t shares;
        Symbol   stock;
        uint32_t price;
        uint64_t match_order;
    };

    struct StockTradingAction: Header {
        Symbol   stock;
        char     trading_state;        // 'H' halted, 'P' paused, 'Q' quote-only, 'T' trading
        char     reserved;
        std::array<char, 4> reason;
    };


    struct RegShoRestriction: Header {
        Symbol   stock;
        char reg_sho_action;
    };


    struct MarketParticipantPosition: Header {
        std::array<char, 4> mpid;
        Symbol   stock;
        char primary_market_maker;
        char market_maker_mode;
        char state;
    };


    struct MwcbDeclineLevel: Header {
        uint64_t level1;
        uint64_t level2;
        uint64_t level3;
    };

    struct MwcbStatusMessage: Header {
        char breached_level;
    };

    struct QuotationPeriodUpdate: Header {
        Symbol stock;
        uint32_t ipo_quotation_release_time;
        char ipo_quotation_release_qualifier;
        uint32_t ipo_price;
    };


    struct LuldAuctionCollar: Header {
        Symbol stock;
        uint32_t auction_collar_reference_price;
        uint32_t upper_auction_collar_price;
        uint32_t lower_auction_collar_price;
        uint32_t auction_collar_extension;
    };

    struct OperationHalt: Header {
        Symbol stock;
        char market_code;
        char operational_halt_action;
    };

    struct CrossTrade: Header {
        uint64_t shares;              // 8-byte shares
        Symbol   stock;
        uint32_t cross_price;
        uint64_t match_number;
        char     cross_type;
    };

    struct BrokenTrade: Header {
        uint64_t match_number;
    };

    struct Noii: Header {
        uint64_t paired_shares;
        uint64_t imbalance_shares;
        char     imbalance_direction;
        Symbol   stock;
        uint32_t far_price;
        uint32_t near_price;
        uint32_t current_reference_price;
        char     cross_type;
        char     price_variation_indicator;
    };

    struct Rpii: Header {
        Symbol stock;
        char   interest_flag;
    };

    struct DirectListingCapitalRaise: Header {
        Symbol   stock;
        char     open_eligibility_status;
        uint32_t minimum_allowable_price;
        uint32_t maximum_allowable_price;
        uint32_t near_execution_price;
        uint64_t near_execution_time;
        uint32_t lower_price_range_collar;
        uint32_t upper_price_range_collar;
    };



    // Base handler: empty callbacks. Derive and override the ones you need.
    struct NullHandler {
        void on_system_event(const SystemEvent&) {}
        void on_stock_directory(const StockDirectory&) {}
        void on_add_order(const AddOrder&) {}
        void on_order_executed(const OrderExecuted&) {}
        void on_order_executed_price(const OrderExecutedPrice&) {}
        void on_order_cancel(const OrderCancel&) {}
        void on_order_delete(const OrderDelete&) {}
        void on_order_replace(const OrderReplace&) {}
        void on_trade(const Trade&) {}
        void on_other(char, const uint8_t*, uint16_t) {}
        void on_stock_trading_action(const StockTradingAction&) {}
        void on_reg_sho_restriction(const RegShoRestriction&) {}
        void on_market_participant_position(const MarketParticipantPosition&) {}
        void on_mwcb_decline_level(const MwcbDeclineLevel&) {}
        void on_mwcb_status_message(const MwcbStatusMessage&) {}
        void on_quotation_period_update(const QuotationPeriodUpdate&) {}
        void on_luld_auction_collar(const LuldAuctionCollar&) {}
        void on_operational_halt(const OperationHalt&) {}
        void on_cross_trade(const CrossTrade&) {}
        void on_broken_trade(const BrokenTrade&) {}
        void on_noii(const Noii&) {}
        void on_rpii(const Rpii&) {}
        void on_direct_listing_capital_raise(const DirectListingCapitalRaise&) {}
    };

    // Decode one message body `m` (length `len`) and dispatch to the handler.
    // Compile-time (static) polymorphism: the handler type is a template param,
    // so callbacks inline with no vtable.
    template <class Handler>
    inline void dispatch(Handler& h, const uint8_t* m, uint16_t len) {
        switch (static_cast<char>(m[0])) {
            case 'S': {
                SystemEvent e;
                load_header(e, m);
                e.event_code = static_cast<char>(m[11]);
                h.on_system_event(e);
                break;
            }
            case 'R': {
                StockDirectory r;
                load_header(r, m);
                std::memcpy(r.stock.data(), m + 11, 8);
                h.on_stock_directory(r);
                break;
            }
            case 'A': case 'F': {
                AddOrder a;
                load_header(a, m);
                a.order_ref = load_be64(m + 11);
                a.side = static_cast<char>(m[19]);
                a.shares = load_be32(m + 20);
                std::memcpy(a.stock.data(), m + 24, 8);
                a.price = load_be32(m + 32);
                a.attributed = (m[0] == 'F');
                h.on_add_order(a);
                break;
            }
            case 'E': {
                OrderExecuted e;
                load_header(e, m);
                e.order_ref = load_be64(m + 11);
                e.executed_shares = load_be32(m + 19);
                e.match_number = load_be64(m + 23);
                h.on_order_executed(e);
                break;
            }
            case 'C': {
                OrderExecutedPrice c;
                load_header(c, m);
                c.order_ref = load_be64(m + 11);
                c.executed_shares = load_be32(m + 19);
                c.match_number = load_be64(m + 23);
                c.printable = static_cast<char>(m[31]);
                c.execution_price = load_be32(m + 32);
                h.on_order_executed_price(c);
                break;
            }
            case 'X': {
                OrderCancel x;
                load_header(x, m);
                x.order_ref = load_be64(m + 11);
                x.cancelled_shares = load_be32(m + 19);
                h.on_order_cancel(x);
                break;
            }
            case 'D': {
                OrderDelete d;
                load_header(d, m);
                d.order_ref = load_be64(m + 11);
                h.on_order_delete(d);
                break;
            }
            case 'U': {
                OrderReplace u;
                load_header(u, m);
                u.old_order_ref = load_be64(m + 11);
                u.new_order_ref = load_be64(m + 19);
                u.shares = load_be32(m + 27);
                u.price = load_be32(m + 31);
                h.on_order_replace(u);
                break;
            }
            case 'P': {
                Trade p;
                load_header(p, m);
                p.order_ref = load_be64(m + 11);
                p.side = static_cast<char>(m[19]);
                p.shares = load_be32(m + 20);
                std::memcpy(p.stock.data(), m + 24, 8);
                p.price = load_be32(m + 32);
                p.match_order = load_be64(m + 36);
                h.on_trade(p);
                break;
            }

            case 'H': {
                StockTradingAction a;
                load_header(a, m);
                std::memcpy(a.stock.data(), m + 11, 8);
                a.trading_state = static_cast<char>(m[19]);
                a.reserved = static_cast<char>(m[20]);
                std::memcpy(a.reason.data(),m+21, 4);
                h.on_stock_trading_action(a);
                break;
            }

            case 'Y': {
                RegShoRestriction r;
                load_header(r, m);
                std::memcpy(r.stock.data(), m + 11, 8);
                r.reg_sho_action = static_cast<char>(m[19]);
                h.on_reg_sho_restriction(r);
                break;
            }

            case 'L': {
                MarketParticipantPosition l;
                load_header(l,m);
                std::memcpy(l.mpid.data(), m+11, 4);
                std::memcpy(l.stock.data(), m+15, 8);
                l.primary_market_maker  = static_cast<char>(m[23]);
                l.market_maker_mode     = static_cast<char>(m[24]);
                l.state                 = static_cast<char>(m[25]);
                h.on_market_participant_position(l);
                break;
            }

            case 'V': {
                MwcbDeclineLevel v;
                load_header(v,m);
                v.level1 = load_be64(m+11);
                v.level2 = load_be64(m+19);
                v.level3 = load_be64(m+27);
                h.on_mwcb_decline_level(v);
                break;
            }

            case 'W': {
                MwcbStatusMessage w;
                load_header(w, m);
                w.breached_level = static_cast<char>(m[11]);
                h.on_mwcb_status_message(w);
                break;
            }

            case 'K': {
                QuotationPeriodUpdate q;
                load_header(q,m);
                std::memcpy(q.stock.data(),m+11, 8);
                q.ipo_quotation_release_time = load_be32((m+19));
                q.ipo_quotation_release_qualifier = static_cast<char>(m[23]);
                q.ipo_price = load_be32((m+24));
                h.on_quotation_period_update(q);
                break;
            }

            case 'J' : {
                LuldAuctionCollar a;
                load_header(a,m);
                std::memcpy(a.stock.data(),m+11,8);
                a.auction_collar_reference_price = load_be32(m+19);
                a.upper_auction_collar_price = load_be32(m+23);
                a.lower_auction_collar_price = load_be32(m+27);
                a.auction_collar_extension = load_be32(m+31);
                h.on_luld_auction_collar(a);
                break;
            }

            case 'h': {
                OperationHalt a;
                load_header(a,m);
                std::memcpy(a.stock.data(),m+11, 8);
                a.market_code = static_cast<char>(m[19]);
                a.operational_halt_action = static_cast<char>(m[20]);
                h.on_operational_halt(a);
                break;
            }


                        case 'Q': {
                CrossTrade q;
                load_header(q, m);
                q.shares       = load_be64(m + 11);   // 8-byte shares
                std::memcpy(q.stock.data(), m + 19, 8);
                q.cross_price  = load_be32(m + 27);
                q.match_number = load_be64(m + 31);
                q.cross_type   = static_cast<char>(m[39]);
                h.on_cross_trade(q);
                break;
            }

            case 'B': {
                BrokenTrade b;
                load_header(b, m);
                b.match_number = load_be64(m + 11);
                h.on_broken_trade(b);
                break;
            }

            case 'I': {
                Noii n;
                load_header(n, m);
                n.paired_shares            = load_be64(m + 11);
                n.imbalance_shares         = load_be64(m + 19);
                n.imbalance_direction      = static_cast<char>(m[27]);   // before stock
                std::memcpy(n.stock.data(), m + 28, 8);
                n.far_price                = load_be32(m + 36);
                n.near_price               = load_be32(m + 40);
                n.current_reference_price  = load_be32(m + 44);
                n.cross_type               = static_cast<char>(m[48]);
                n.price_variation_indicator = static_cast<char>(m[49]);
                h.on_noii(n);
                break;
            }

            case 'N': {
                Rpii r;
                load_header(r, m);
                std::memcpy(r.stock.data(), m + 11, 8);
                r.interest_flag = static_cast<char>(m[19]);
                h.on_rpii(r);
                break;
            }

            case 'O': {
                DirectListingCapitalRaise o;
                load_header(o, m);
                std::memcpy(o.stock.data(), m + 11, 8);
                o.open_eligibility_status   = static_cast<char>(m[19]);
                o.minimum_allowable_price   = load_be32(m + 20);
                o.maximum_allowable_price   = load_be32(m + 24);
                o.near_execution_price      = load_be32(m + 28);
                o.near_execution_time       = load_be64(m + 32);
                o.lower_price_range_collar  = load_be32(m + 40);
                o.upper_price_range_collar  = load_be32(m + 44);
                h.on_direct_listing_capital_raise(o);
                break;
            }


            default:
                h.on_other(static_cast<char>(m[0]), m, len);
                break;
        }
    }

    // Frames the length-prefixed ITCH stream and dispatches each message.
    template <class Handler>
    class Parser {
    public:
        explicit Parser(Handler& h) : h_(h) {}

        std::size_t parse(const uint8_t* data, std::size_t size) {
            std::size_t pos = 0, count = 0;
            while (pos + 2 <= size) {
                const uint16_t len = load_be16(data + pos);
                if (len == 0) break;
                if (pos + 2 + len > size) break;   // truncated tail
                dispatch(h_, data + pos + 2, len);
                pos += 2 + static_cast<std::size_t>(len);
                ++count;
            }
            return count;
        }

    private:
        Handler& h_;
    };

}  // namespace itch
