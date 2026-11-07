#include "objpool.hpp"

#include <cstdio>

struct particle {
    float x, y;
    float vx, vy;
    int   id;

    particle(float x_, float y_, int id_) : x(x_), y(y_), vx(0), vy(0), id(id_) {}
    ~particle() { std::printf("  ~particle(%d)\n", id); }
};

int main() {
    // 256 particles max, pool lives on the stack.
    objpool::pool<particle, 256> pp;

    std::printf("capacity = %zu\n", pp.capacity());

    // Spawn a few.
    auto* a = pp.construct(1.0f, 2.0f, 1);
    auto* b = pp.construct(3.0f, 4.0f, 2);
    auto* c = pp.construct(5.0f, 6.0f, 3);

    std::printf("in_use = %zu, available = %zu\n", pp.in_use(), pp.available());

    // Kill one — destructor prints, then slot returns to the pool.
    pp.destroy(b);
    std::printf("after destroy: in_use = %zu\n", pp.in_use());

    // Reuse the freed slot.
    auto* d = pp.construct(7.0f, 8.0f, 4);
    std::printf("reused slot for id 4? %s\n", (d == b) ? "yes" : "no");

    // Clean up whatever's left.
    pp.destroy(a);
    pp.destroy(c);
    pp.destroy(d);

    std::printf("final: empty = %s\n", pp.empty() ? "true" : "false");
    return 0;
}