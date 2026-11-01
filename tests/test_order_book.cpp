#include <cstdio>
#include "itch/parser.hpp"
#include "itch/order_book.hpp"
#include <cstdint>


static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do {                                        \
    ++g_checks;                                                 \
    if (!(cond)) { ++g_failures;                                \
        std::printf("  FAIL  line %d:  %s\n", __LINE__, #cond); }\
} while (0)



using itch::OrderBook;


struct Recorder : itch::NullHandler {
    itch::SystemEvent sysev{};  bool got_sysev=false;
    itch::Noii        noii{};   bool got_noii=false;
    // ... one pair per message type you test
    bool got_other=false;
    void on_system_event(const itch::SystemEvent& e){ sysev=e; got_sysev=true; }
    void on_noii(const itch::Noii& n){ noii=n; got_noii=true; }
    void on_other(char,const uint8_t*,uint16_t){ got_other=true; }
};

static uint64_t level_shares(OrderBook& b, uint16_t loc, char side, uint32_t price){
    const auto& v = (side=='B') ? b.books[loc].bids : b.books[loc].asks;
    for (const auto& lv: v){
        if(lv.price == price){
            return lv.shares;
        }
    }
    return 0;
}

static std::size_t level_count(OrderBook& b, uint16_t loc, char side){
    return (side == 'B') ? b.books[loc].bids.size() : b.books[loc].asks.size();
}

static bool has_order(OrderBook& b, uint64_t ref ){
    return b.orders.find(ref) != b.orders.end();
}

static uint32_t order_shares(OrderBook& b, uint64_t r){
    return b.orders.find(r)->second.shares;
}

static itch::AddOrder mk_add(uint16_t loc, uint64_t ref, char s, uint32_t sh, uint32_t px){
    itch::AddOrder m{};
    m.stock_locate = loc;
    m.order_ref = ref;
    m.side = s;
    m.shares = sh;
    m.price = px;
    return m;
}

static itch::OrderExecuted mk_exec(uint64_t ref, uint32_t sh){
    itch::OrderExecuted m{};
    m.order_ref = ref;
    m.executed_shares = sh;
    return m;
}


static itch::OrderExecutedPrice mk_exec_px(uint64_t ref, uint32_t sh){
    itch::OrderExecutedPrice m{};
    m.order_ref = ref;
    m.executed_shares = sh;
    return m;
}

static itch::OrderCancel mk_cancel(uint64_t ref, uint32_t sh){
    itch::OrderCancel m{};
    m.order_ref = ref;
    m.cancelled_shares = sh;
    return m;
}

static itch::OrderDelete mk_delete(uint64_t ref){
    itch::OrderDelete m{};
    m.order_ref = ref;
    return m;
}


static itch::OrderReplace mk_replace(uint64_t oldref, uint64_t newref, uint32_t sh, uint32_t px){
    itch::OrderReplace m{};

    m.old_order_ref = oldref;
    m.new_order_ref = newref;
    m.shares = sh;
    m.price = px;
    return m;
}


// ----- Tests

static void test_add(){
    OrderBook b;
    b.on_add_order(mk_add(1,100, 'B', 500, 101));
    CHECK(has_order(b, 100));
    CHECK(level_shares(b,1,'B',101)==500);
    CHECK(b.books[1].bids.front().price==101); // .front to get the best bid
}


static void test_merge_same_price(){
    OrderBook b;
    b.on_add_order(mk_add(1,100, 'B', 500, 101));
    b.on_add_order(mk_add(1,101, 'B', 400, 101));
    CHECK(has_order(b, 100));
    CHECK(has_order(b, 101));
    CHECK(level_shares(b,1,'B',101)==900);
}


static void test_partial_execute() {
    OrderBook b;
    b.on_add_order(mk_add(1,100, 'B', 500, 101));
    b.on_order_executed(mk_exec(100,200));
    CHECK(has_order(b,100));
    CHECK(order_shares(b,100) == 300);
    CHECK(level_shares(b,1,'B',101) ==300);
}


static void test_full_execution(){
    OrderBook b;
    b.on_add_order(mk_add(1,100,'B',500,101));
    b.on_order_executed(mk_exec(100,500));
    CHECK(!has_order(b, 100));
    CHECK(level_count(b,1,'B')==0);
}

static void test_execute_with_price() {             // the 'C' message (zombie-bug guard)
    OrderBook b;
    b.on_add_order(mk_add(1, 1, 'S', 500, 200));
    b.on_order_executed_price(mk_exec_px(1, 500));
    CHECK(!has_order(b, 1));
    CHECK(level_count(b, 1, 'S') == 0);
}

static void test_cancel() {
    OrderBook b;
    b.on_add_order(mk_add(1, 100, 'B', 500, 101));
    b.on_order_cancel(mk_cancel(100, 200));
    CHECK(order_shares(b, 100) == 300);
    CHECK(level_shares(b, 1, 'B', 101) == 300);
}

static void test_delete() {
    OrderBook b;
    b.on_add_order(mk_add(1, 100, 'B', 500, 101));
    b.on_order_delete(mk_delete(100));
    CHECK(!has_order(b, 100));
    CHECK(level_count(b, 1, 'B') == 0);             // full remaining shares removed
}

static void test_replace() {
    OrderBook b;
    b.on_add_order(mk_add(1, 100, 'B', 500, 101));
    b.on_order_replace(mk_replace(100, 200, 400, 102)); // 100@101 -> 200: 400@102
    CHECK(!has_order(b, 100));                        // old ref gone
    CHECK(has_order(b, 200));                         // new ref present
    CHECK(level_shares(b, 1, 'B', 101) == 0);         // old level cleared
    CHECK(level_shares(b, 1, 'B', 102) == 400);       // new level
}

static void test_sorting() {
    OrderBook b;
    b.on_add_order(mk_add(1, 10, 'B', 100, 101));
    b.on_add_order(mk_add(1, 11, 'B', 100, 103));
    b.on_add_order(mk_add(1, 12, 'B', 100, 102));
    CHECK(b.books[1].bids.front().price == 103);      // best bid = highest
    b.on_add_order(mk_add(1, 20, 'S', 100, 205));
    b.on_add_order(mk_add(1, 21, 'S', 100, 203));
    CHECK(b.books[1].asks.back().price == 203);       // best ask = lowest
}

static void test_symbol_lookup() {
    OrderBook b;
    itch::StockDirectory r{};
    r.stock_locate = 7;
    r.stock = {'A','A','P','L',' ',' ',' ',' '};   // 8 bytes, space-padded
    b.on_stock_directory(r);
    CHECK(b.symbol_by_locate.at(7) == "AAPL");     // trim_symbol stripped the padding
}

void test_system_event() {
    uint8_t m[12] = {};
    m[0] = 'S';
    itch::store_be16(m + 1, 7);           // stock_locate
    itch::store_be16(m + 3, 42);          // tracking_number
    itch::store_be48(m + 5, 123456789);   // timestamp
    m[11] = 'O';                          // event_code

    Recorder r;
    itch::dispatch(r, m, sizeof(m));

    CHECK(r.got_sysev);                  // was it dispatched at all?
    CHECK(!r.got_other);                 // did NOT fall through to on_other
    CHECK(r.sysev.stock_locate == 7);
    CHECK(r.sysev.tracking_number == 42);
    CHECK(r.sysev.timestamp == 123456789);
    CHECK(r.sysev.event_code == 'O');
}

void test_noii() {
    uint8_t m[50] = {};
    m[0] = 'I';
    itch::store_be16(m + 1, 9);
    itch::store_be16(m + 3, 10);
    itch::store_be48(m + 5, 777);
    itch::store_be64(m + 11, 1000);        // paired_shares
    itch::store_be64(m + 19, 2000);        // imbalance_shares
    m[27] = 'B';                           // imbalance_direction (before stock!)
    std::memcpy(m + 28, "AAPL    ", 8);    // stock
    itch::store_be32(m + 36, 111);         // far_price
    itch::store_be32(m + 40, 222);         // near_price
    itch::store_be32(m + 44, 333);         // current_reference_price
    m[48] = 'O';                           // cross_type
    m[49] = 'L';                           // price_variation_indicator

    Recorder r;
    itch::dispatch(r, m, sizeof(m));

    CHECK(r.got_noii && !r.got_other);
    CHECK(r.noii.paired_shares == 1000);
    CHECK(r.noii.imbalance_shares == 2000);
    CHECK(r.noii.imbalance_direction == 'B');
    CHECK(itch::trim_symbol(r.noii.stock) == "AAPL");
    CHECK(r.noii.far_price == 111);
    CHECK(r.noii.near_price == 222);       // if far/near were swapped in dispatch, THIS fails
    CHECK(r.noii.current_reference_price == 333);
    CHECK(r.noii.cross_type == 'O');
    CHECK(r.noii.price_variation_indicator == 'L');
}

int main() {
    test_add();
    test_cancel();
    test_delete();
    test_replace();
    test_sorting();
    test_merge_same_price();
    test_partial_execute();
    test_full_execution();
    test_execute_with_price();
    test_symbol_lookup();
    test_system_event();
    test_noii();

    if(g_failures==0){
        printf("All tests passed!\n");
    }
    return g_failures == 0 ? 0:1 ;
}
