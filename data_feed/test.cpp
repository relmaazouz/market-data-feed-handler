#include "parser.hpp"
#include "order_book.hpp"
#include <cstdint>

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do {                                        \
    ++g_checks;                                                 \
    if (!(cond)) { ++g_failures;                                \
        std::printf("  FAIL  line %d:  %s\n", __LINE__, #cond); }\
} while (0)



using itch::OrderBook;


static uint64_t level_shares(OrderBook& b, uint16_t loc, char side, uint32_t price){
    const auto& v = (side=='B') ? b.books[loc].bids : b.books[loc].asks;
    for (const auto& lv: v){
        if(lv.price == price){
            return lv.shares;
        }
    }
    return 0;
}

static std::size_t level_count(OrderBook& b, uint16_t loc, char side, uint32_t price){
    const auto& v = (side == 'B') ? b.books[loc].bids : b.books[loc].asks;
    for(const auto& lv: v) {
        if (lv.price == price) return lv.shares;
    }
    return 0;
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

static itch::OrderCancel mk_cancel(uint64_t ref, uint64_t sh){
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
    CHECK(level_count(b,1,'B',101)==0);
}

static void test_execute_with_price() {             // the 'C' message (zombie-bug guard)
    OrderBook b;
    b.on_add_order(mk_add(1, 1, 'S', 500, 200));
    b.on_order_executed_price(mk_exec_px(1, 500));
    CHECK(!has_order(b, 1));
    CHECK(level_count(b, 1, 'S',200) == 0);
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
    CHECK(level_count(b, 1, 'B',101) == 0);             // full remaining shares removed
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

    return g_failures == 0 ? 0:1 ;
}
