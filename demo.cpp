#include "lock_based_rb_tree.hpp"

#include <atomic>

int main()
{
    constexpr int NKEYS = 20'000; // key space
    constexpr int WRITERS = 8;    // mixed insert/erase
    constexpr int READERS = 8;    // heavy lookups
    constexpr int UPDATERS = 4;   // duplicate inserts
    constexpr auto TEST_DURATION = std::chrono::seconds(3);

    rbt::RBTree<int, int> tree;

    // ── 1. bulk parallel insert ──────────────────────────────────────────
    std::vector<int> keys(NKEYS);
    std::iota(keys.begin(), keys.end(), 0);
    std::shuffle(keys.begin(), keys.end(),
                 std::mt19937{std::random_device{}()});

    auto ceil_div = [](size_t a, size_t b)
    { return (a + b - 1) / b; };

    std::vector<std::thread> threads;
    for (int w = 0; w < WRITERS; ++w)
    {
        size_t beg = w * ceil_div(NKEYS, WRITERS);
        size_t end = std::min<size_t>(beg + ceil_div(NKEYS, WRITERS), NKEYS);
        threads.emplace_back([&, beg, end]
                             {
            for (size_t i = beg; i < end; ++i)
                tree.insert(keys[i], keys[i]); });
    }
    for (auto &t : threads)
        t.join();
    threads.clear();
    std::cout << "[phase-1] bulk insert done\n";

    // ── 2. verify content ────────────────────────────────────────────────
    for (int k : keys)
    {
        auto v = tree.lookup(k);
        assert(v && *v == k);
    }
    std::cout << "  ✔ all " << NKEYS << " keys present\n";

    // ── 3. 3-second mixed stress workload ────────────────────────────────
    const auto stop_time = std::chrono::steady_clock::now() + TEST_DURATION;
    std::atomic<bool> stop{false};

    // 3-a watchdog validating invariants every 50 ms
    std::thread watchdog([&]
                         {
    while (!stop.load(std::memory_order_acquire)) {
        {   // 🔒 hold writers_mutex so no writer mutates during validation
            std::lock_guard<std::mutex> g(tree.writer_mutex());
            assert(tree.validate());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } });

    // Prepare unique seeds (avoid shared RNG)
    std::vector<uint32_t> seeds;
    {
        std::random_device rd;
        seeds.resize(WRITERS + READERS + UPDATERS);
        for (auto &s : seeds)
            s = rd();
    }
    size_t seed_idx = 0;

    auto rand_key = [&](std::mt19937 &g)
    {
        std::uniform_int_distribution<int> d(-NKEYS / 4, NKEYS * 5 / 4);
        return d(g); // may be outside range
    };

    // 3-b writer threads: half insert, half erase
    for (int i = 0; i < WRITERS; ++i)
    {
        uint32_t seed = seeds[seed_idx++];
        threads.emplace_back([&, i, seed]
                             {
            std::mt19937 rng{seed};
            while (std::chrono::steady_clock::now() < stop_time) {
                int k = rand_key(rng);
                if (i & 1)
                    tree.insert(k, k);       // odd-index writer inserts
                else
                    (void) tree.erase(k);    // even-index writer erases
            } });
    }

    // 3-c updater threads: overwrite existing keys with new values
    for (int i = 0; i < UPDATERS; ++i)
    {
        uint32_t seed = seeds[seed_idx++];
        threads.emplace_back([&, seed]
                             {
            std::mt19937 rng{seed};
            while (std::chrono::steady_clock::now() < stop_time) {
                int k = (rand_key(rng) & 0x7fffffff) % NKEYS; // ensure [0,NKEYS)
                tree.insert(k, k + 42);       // duplicate insert/overwrite
            } });
    }

    // 3-d reader threads
    for (int i = 0; i < READERS; ++i)
    {
        uint32_t seed = seeds[seed_idx++];
        threads.emplace_back([&, seed]
                             {
            std::mt19937 rng{seed};
            while (std::chrono::steady_clock::now() < stop_time) {
                (void) tree.lookup(rand_key(rng));
            } });
    }

    // 3-e join everything, stop watchdog
    for (auto &t : threads)
        t.join();
    stop.store(true, std::memory_order_release);
    watchdog.join();
    std::cout << "[phase-2] mixed stress finished\n";

    // ── 4. final validation & stats ───────────────────────────────────────
    // assert(tree.validate());
    // 4. final validation  (after all worker threads joined)
    {
        std::lock_guard<std::mutex> g(tree.writer_mutex());
        assert(tree.validate());
    }

    size_t survivors = 0;
    for (int k = -NKEYS / 4; k < NKEYS * 5 / 4; ++k)
        if (tree.lookup(k))
            ++survivors;

    std::cout << "  ✔ invariants hold, " << survivors
              << " keys currently in tree\n"
              << "🎉 ALL STRESS TESTS PASSED\n";
    return 0;
}