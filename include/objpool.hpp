/* ============================================================================
 *  fixed-size object pool with an intrusive freelist
 * ----------------------------------------------------------------------------
 *  by HadeShiki  <https://github.com/HadeShiki>
 *
 *  Header-only.  #include this file, make an objpool::pool<T, N>, and get
 *  O(1) alloc/free with zero locks and zero per-object overhead.
 *
 *  HOW IT WORKS (one paragraph):
 *
 *      A pool owns one contiguous block of storage big enough for N slots.
 *      Each slot is big enough to hold either a T or a freelist link,
 *      whichever is larger.  At construction we thread every slot onto a
 *      singly-linked freelist by writing a pointer into each slot's own
 *      memory.  Allocating pops the head; freeing pushes it back.  That's
 *      it — two pointer assignments per operation, no search, no
 *      coalescing, no locks.
 *
 *
 *  MEMORY LAYOUT
 *  -------------
 *
 *      pool storage (one contiguous array of N slots)
 *      +---------+---------+---------+---------+---------+
 *      | slot 0  | slot 1  | slot 2  | slot 3  |  ...    |
 *      +---------+---------+---------+---------+---------+
 *           |
 *           |  each slot is max(sizeof(T), sizeof(FreeNode)) bytes,
 *           |  aligned to max(alignof(T), alignof(FreeNode)).
 *           v
 *      while free, the slot holds a "next" pointer:
 *
 *      +---------+
 *      | next*   |   <- overlays the slot; the object is NOT constructed here
 *      +---------+
 *
 *
 *      freelist state (after a few alloc/free cycles):
 *
 *          free_head
 *             |
 *             v
 *          +-----+    +-----+    +-----+
 *          |  #2 |--->|  #0 |--->|  #4 |---> nullptr
 *          +-----+    +-----+    +-----+
 *          (slot 3 is in use, not on the list)
 *
 *
 *      slot lifecycle:
 *
 *          [free] --alloc()--> [in use] --free()--> [free]
 *             |                   |                    |
 *        holds next*         holds a T            holds next* again
 *
 *
 *  THINGS YOU SHOULD KNOW
 *  ----------------------
 *    * NOT thread-safe.  One pool per thread, or wrap it in a mutex.
 *      (Thread-local pooling belongs in a different project — see fastalloc.)
 *
 *    * Capacity is fixed at compile time.  If you need growth, use a
 *      different structure.  The whole point here is that it never grows.
 *
 *    * Objects are NOT constructed automatically.  alloc() returns raw
 *      storage; you place an object with placement new.  free() does NOT
 *      run destructors — call ~T() yourself first.  The pool is a
 *      storage manager, not an object manager.
 *
 *    * T of any size works — including int, char, and empty structs.
 *      If T is smaller than a pointer, the slot is padded up to pointer
 *      size so the freelist link fits.  You pay that padding; nothing else.
 *
 *    * Over-aligned T is supported: slots are aligned to at least
 *      alignof(T) and alignof(FreeNode).
 *
 *
 *  BUILD
 *  -----
 *      make          # build demo
 *      make test     # build + run tests
 *      make clean
 *
 *  LICENSE: MIT  See LICENSE file
 * ==========================================================================*/

#ifndef OBJPOOL_HPP
#define OBJPOOL_HPP

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace objpool {

/* ---------------------------------------------------------------------------
 *  FreeNode — the intrusive freelist link.
 *
 *  While a slot is free we stash a pointer to the next free slot right there
 *  in the slot's memory.  No side table, no per-slot overhead.
 * -------------------------------------------------------------------------*/
struct FreeNode {
    FreeNode* next;
};

/* ---------------------------------------------------------------------------
 *  pool<T, N>
 *
 *  A fixed-capacity storage manager for N objects of type T.
 *
 *  T         — the object type (any size; T need not fit a pointer)
 *  N         — capacity, fixed at compile time
 * -------------------------------------------------------------------------*/
template <typename T, std::size_t N>
class pool {
    static_assert(N > 0, "pool capacity must be > 0");

    // Each slot must be big enough for either a T or a FreeNode.
    static constexpr std::size_t slot_size =
        sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);

    static constexpr std::size_t slot_align =
        alignof(T) > alignof(FreeNode) ? alignof(T) : alignof(FreeNode);

    struct alignas(slot_align) slot {
        std::byte bytes[slot_size];
    };

public:
    using value_type = T;

    pool() noexcept {
        // Thread every slot onto the freelist, front to back.
        for (std::size_t i = 0; i < N; ++i) {
            auto* node = reinterpret_cast<FreeNode*>(slot_ptr(i));
            node->next = free_head_;
            free_head_ = node;
        }
    }

    pool(const pool&)            = delete;
    pool& operator=(const pool&) = delete;

    /* -----------------------------------------------------------------------
     *  alloc — pop the head of the freelist.
     *
     *  Returns nullptr when the pool is exhausted.  The returned storage is
     *  NOT a constructed T; use placement new if you want an object.
     * ---------------------------------------------------------------------*/
    [[nodiscard]] void* alloc() noexcept {
        if (free_head_ == nullptr) {
            return nullptr;
        }
        FreeNode* node = free_head_;
        free_head_ = node->next;
        ++in_use_;
        return static_cast<void*>(node);
    }

    /* -----------------------------------------------------------------------
     *  free — push a slot back onto the freelist.
     *
     *  Caller must have obtained `p` from alloc() on this same pool, and must
     *  have already run any destructor.  This only reclaims the storage.
     * ---------------------------------------------------------------------*/
    void free(void* p) noexcept {
        if (p == nullptr) {
            return;
        }
        auto* node = static_cast<FreeNode*>(p);
        node->next = free_head_;
        free_head_ = node;
        --in_use_;
    }

    /* -----------------------------------------------------------------------
     *  Convenience helpers: construct / destroy through the pool.
     *
     *  These are what you'll use 95% of the time.
     * ---------------------------------------------------------------------*/
    template <typename... Args>
    [[nodiscard]] T* construct(Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>)
    {
        void* raw = alloc();
        if (raw == nullptr) {
            return nullptr;
        }
        return ::new (raw) T(std::forward<Args>(args)...);
    }

    void destroy(T* obj) noexcept {
        if (obj == nullptr) {
            return;
        }
        obj->~T();
        free(obj);
    }

    /* -----------------------------------------------------------------------
     *  Introspection
     * ---------------------------------------------------------------------*/
    [[nodiscard]] std::size_t capacity() const noexcept { return N; }
    [[nodiscard]] std::size_t in_use()   const noexcept { return in_use_; }
    [[nodiscard]] std::size_t available() const noexcept { return N - in_use_; }
    [[nodiscard]] bool        empty()    const noexcept { return in_use_ == 0; }
    [[nodiscard]] bool        full()     const noexcept { return in_use_ == N; }

private:
    slot        storage_[N];
    FreeNode*   free_head_ = nullptr;
    std::size_t in_use_    = 0;

    [[nodiscard]] void* slot_ptr(std::size_t i) noexcept {
        return static_cast<void*>(storage_[i].bytes);
    }
};

}

#endif