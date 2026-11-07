/* ============================================================================
 *  plain asserts, no framework.
 *
 *  Coverage:
 *    [1] alloc exhaust + LIFO reuse
 *    [2] construct / destroy
 *    [3] free(nullptr) is a no-op
 *    [4] construct returns nullptr when full
 *    [5] over-aligned T keeps its alignment
 *    [6] stress: 100k alloc/free cycles, no leak
 *    [7] non-trivial type (std::string) destructor runs
 *    [8] introspective counters stay in sync through mixed ops
 *    [9] pool with capacity == 1 edge case
 *   [10] freelist keeps all slots reachable (full cycle)
 * ==========================================================================*/

#include "objpool.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

/* ---------------------------------------------------------------------------
 *  [1] alloc exhaust + LIFO reuse
 * -------------------------------------------------------------------------*/
void test_alloc_exhaust() {
    objpool::pool<int, 4> p;
    assert(p.capacity() == 4);
    assert(p.empty());

    void* a = p.alloc();
    void* b = p.alloc();
    void* c = p.alloc();
    void* d = p.alloc();
    assert(a && b && c && d);
    assert(p.full());

    void* e = p.alloc();
    assert(e == nullptr);  // exhausted

    p.free(a);
    assert(p.available() == 1);
    void* f = p.alloc();
    assert(f == a);        // LIFO: last freed, first handed back

    p.free(b);
    p.free(c);
    p.free(d);
    p.free(f);
    assert(p.empty());

    std::printf("  ok: alloc / exhaust / LIFO reuse\n");
}

/* ---------------------------------------------------------------------------
 *  [2] construct / destroy
 * -------------------------------------------------------------------------*/
void test_construct_destroy() {
    objpool::pool<std::string, 8> p;
    auto* s = p.construct("hello, pool");
    assert(s && *s == "hello, pool");
    assert(p.in_use() == 1);

    p.destroy(s);
    assert(p.empty());
    std::printf("  ok: construct / destroy\n");
}

/* ---------------------------------------------------------------------------
 *  [3] free(nullptr) is a no-op
 * -------------------------------------------------------------------------*/
void test_free_null_is_safe() {
    objpool::pool<int, 2> p;
    p.free(nullptr);
    assert(p.empty());
    std::printf("  ok: free(nullptr) is a no-op\n");
}

/* ---------------------------------------------------------------------------
 *  [4] construct returns nullptr when full
 * -------------------------------------------------------------------------*/
void test_construct_when_full() {
    objpool::pool<int, 1> p;
    auto* a = p.construct(42);
    assert(a && *a == 42);
    auto* b = p.construct(99);
    assert(b == nullptr);  // full
    p.destroy(a);
    std::printf("  ok: construct returns nullptr when full\n");
}

/* ---------------------------------------------------------------------------
 *  [5] over-aligned T keeps its alignment
 * -------------------------------------------------------------------------*/
struct alignas(32) aligned_vec { float v[8]; };

void test_alignment() {
    objpool::pool<aligned_vec, 4> p;
    for (int i = 0; i < 4; ++i) {
        auto* a = p.construct();
        assert(a != nullptr);
        assert(reinterpret_cast<std::uintptr_t>(a) % 32 == 0);
        p.destroy(a);
    }
    std::printf("  ok: over-aligned T (32 B) stays aligned\n");
}

/* ---------------------------------------------------------------------------
 *  [6] stress: 100k alloc/free cycles, no leak
 * -------------------------------------------------------------------------*/
void test_stress_cycles() {
    objpool::pool<int, 16> p;
    for (int i = 0; i < 100'000; ++i) {
        void* a = p.alloc();
        void* b = p.alloc();
        assert(a && b);
        p.free(a);
        p.free(b);
        assert(p.empty());
    }
    std::printf("  ok: 100k alloc/free cycles, no leak\n");
}

/* ---------------------------------------------------------------------------
 *  [7] non-trivial type — destructor must run for each object
 * -------------------------------------------------------------------------*/
struct counted {
    static int live;
    counted()  { ++live; }
    ~counted() { --live; }
};
int counted::live = 0;

void test_nontrivial_destructor() {
    assert(counted::live == 0);
    {
        objpool::pool<counted, 4> p;
        auto* a = p.construct();
        auto* b = p.construct();
        auto* c = p.construct();
        (void)a; (void)b; (void)c;
        assert(counted::live == 3);

        p.destroy(b);
        assert(counted::live == 2);

        p.destroy(a);
        p.destroy(c);
        assert(counted::live == 0);
    }
    std::printf("  ok: non-trivial destructors run on destroy()\n");
}

/* ---------------------------------------------------------------------------
 *  [8] counters stay in sync through mixed ops
 * -------------------------------------------------------------------------*/
void test_counters() {
    objpool::pool<int, 8> p;
    std::vector<void*> held;

    for (int i = 0; i < 5; ++i) {
        void* x = p.alloc();
        assert(x);
        held.push_back(x);
    }
    assert(p.in_use() == 5);
    assert(p.available() == 3);

    p.free(held.back()); held.pop_back();
    assert(p.in_use() == 4);
    assert(p.available() == 4);

    while (!held.empty()) {
        p.free(held.back());
        held.pop_back();
    }
    assert(p.empty());
    std::printf("  ok: in_use / available stay in sync\n");
}

/* ---------------------------------------------------------------------------
 *  [9] capacity == 1 edge case
 * -------------------------------------------------------------------------*/
void test_capacity_one() {
    objpool::pool<int, 1> p;
    assert(p.capacity() == 1);
    void* a = p.alloc();
    assert(a);
    assert(p.full());
    assert(p.alloc() == nullptr);
    p.free(a);
    assert(p.empty());
    std::printf("  ok: capacity == 1 edge case\n");
}

/* ---------------------------------------------------------------------------
 *  [10] freelist keeps all slots reachable — full cycle every slot
 * -------------------------------------------------------------------------*/
void test_full_cycle_reachability() {
    constexpr std::size_t N = 64;
    objpool::pool<int, N> p;

    std::vector<void*> slots;
    for (std::size_t i = 0; i < N; ++i) {
        void* x = p.alloc();
        assert(x);
        slots.push_back(x);
    }
    assert(p.full());

    // All N pointers must be distinct (freelist didn't hand out duplicates).
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            assert(slots[i] != slots[j]);
        }
    }

    // Return them all.
    for (void* x : slots) p.free(x);
    assert(p.empty());

    // And they all come back out again.
    for (std::size_t i = 0; i < N; ++i) {
        assert(p.alloc() != nullptr);
    }
    assert(p.full());

    std::printf("  ok: freelist hands out every slot exactly once\n");
}

} // namespace

int main() {
    std::printf("objpool tests\n");

    test_alloc_exhaust();
    test_construct_destroy();
    test_free_null_is_safe();
    test_construct_when_full();
    test_alignment();
    test_stress_cycles();
    test_nontrivial_destructor();
    test_counters();
    test_capacity_one();
    test_full_cycle_reachability();

    std::printf("all tests passed\n");
    return 0;
}