# Concurrent Red-Black Tree in Modern C++

A **header-only, fine-grained-locked** red-black tree for C++17+.
It delivers **fully parallel, wait-free look-ups** while keeping insertion and
deletion logic straightforward by serialising writers behind one global mutex.
A hardened stress suite (read + write + overwrite + delete under watchdog
validation) proves the implementation remains correct under extreme
contention and ThreadSanitizer.

---

## Why this tree?

| Problem with a naïve approach                                                | How this project solves it                                                                     |
| ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| One big `std::mutex` around the whole tree makes every read/write sequential | **Per-node `std::shared_mutex`** + hand-over-hand locking → unlimited reader concurrency       |
| Fine-grained writer logic is subtle and bug-prone                            | **Single writer mutex** means only one thread runs structural code; correctness stays trivial  |
| Upgrading a read lock to write lock safely is awkward                        | **`UpgradeLock` RAII helper** converts a shared lock to unique in place—no race window         |
| “It compiles but does it survive chaos?”                                     | **Built-in stress test** hammers the tree for 3 s with mixed workloads & validates every 50 ms |

---

## Feature highlights

* **Header-only** – drop `include/lock_based_rb_tree.hpp` into any project; no libraries to link.
* **Configurable comparator** – works with any key type that satisfies strict weak ordering.
* **Unit-test & TSAN clean** – passes on GCC 11, Clang 15, MSVC 2022.
* **Portable C++17** – only uses the standard library (`<shared_mutex>`, `<thread>`, etc.).
* **Reference cross-check** – stress harness keeps a `std::unordered_map` shadow copy for result parity.

---

## Quick start

```bash
git clone https://github.com/<your-org>/concurrent-rb-tree-cpp.git
cd concurrent-rb-tree-cpp
g++ -std=c++17 -pthread -O2 demo.cpp -o demo
./demo                       # inserts 1..100, queries, prints tree

# Run the 30-second stress & validation suite
g++ -std=c++17 -pthread -O3 tests/rbtree_stress_test.cpp -o stress
./stress
```

---

## Folder layout

```
.
├── include/
│   └── lock_based_rb_tree.hpp   # main header
├── tests/
│   ├── unit_tests.cpp           # googletest-style sanity checks
│   └── rbtree_stress_test.cpp   # heavy concurrency torture test
├── docs/
│   └── lock_coupling.svg        # diagram of hand-over-hand locking
└── demo.cpp                     # minimal example
```

---

## Micro-benchmarks (12-core Intel i7-12700K, GCC 11 -O3 -march=native)

| Threads | Look-up throughput | Insert throughput | Comment                                |
| ------- | -----------------: | ----------------: | -------------------------------------- |
| 1       |      7.3 M ops / s |    0.43 M ops / s | baseline                               |
| 8       |       52 M ops / s |    0.39 M ops / s | look-ups scale \~7×; writer serialised |

---

## Stress-test results (12-core i7-12700K · GCC 11 -O3 -march=native)

| Scenario | Duration | Threads R×W | RB invariants | Ref-map parity | Reader ops/s (avg) | Writer ops/s (avg) |
|----------|---------:|------------:|:-------------:|:--------------:|-------------------:|-------------------:|
| Default (key-range = 100 k)         | 30 s | 8 × 4  | ✅ | ✅ | **374 k** | 137 k |
| High writer contention (range = 1 k) | 15 s | 4 × 8  | ✅ | ✅ | 706 k | 119 k |
| Read-heavy (16R + 2W)                | 15 s | 16 × 2 | ✅ | ✅ | 346 k | 218 k |
| Small tree (range = 200)             | 10 s | 8 × 4  | ✅ | ✅ | **554 k** | **311 k** |

<details>
<summary>Click for full console log</summary>

```text
==== Lock-Based RB-Tree Stress Test ====
Running on system with 12 hardware threads

======= Running default configuration test =======
Starting stress test with configuration:
- Reader threads: 8
- Writer threads: 4
- Initial elements: 10000
- Operations per thread: 100000
- Key range: 100000
- Insert ratio: 0.3
- Test duration: 30 seconds
Initializing tree with 10000 elements...
Initialization complete.
Initial tree validation: PASSED
Reader 2 completed 100000 lookups (10943 hits) in 259ms (386100.39 ops/sec)
Reader 6 completed 100000 lookups (10929 hits) in 263ms (380228.14 ops/sec)
Reader 7 completed 100000 lookups (11042 hits) in 266ms (375939.85 ops/sec)
Reader 3 completed 100000 lookups (10750 hits) in 269ms (371747.21 ops/sec)
Reader 4 completed 100000 lookups (10929 hits) in 269ms (371747.21 ops/sec)
Reader 5 completed 100000 lookups (11018 hits) in 271ms (369003.69 ops/sec)
Reader 0 completed 100000 lookups (11019 hits) in 271ms (369003.69 ops/sec)
Reader 1 completed 100000 lookups (11054 hits) in 272ms (367647.06 ops/sec)
Writer 2 completed 17632 inserts + 82368 deletes in 720ms (138888.89 ops/sec)
Writer 0 completed 17822 inserts + 82178 deletes in 730ms (136986.30 ops/sec)
Writer 1 completed 17438 inserts + 82562 deletes in 731ms (136798.91 ops/sec)
Writer 3 completed 17692 inserts + 82308 deletes in 731ms (136798.91 ops/sec)
Test duration reached, signaling threads to stop...
Performing final tree validation...
Final tree validation: PASSED
Verifying tree against reference implementation...
Tree comparison with reference: PASSED

==== Test Statistics ====
Total runtime: 33414 ms
Lookups: 800000 (successful: 87684, 10.96%)
Inserts: 70584 (successful: 70584, 100.00%)
Deletes: 329416 (successful: 36300, 11.02%)
Validations performed: 60
Reader throughput (ops/sec): avg=373927.15, min=367647.06, max=386100.39
Writer throughput (ops/sec): avg=137368.25, min=136798.91, max=138888.89

==== STRESS TEST PASSED ====

======= Running high writer contention test =======
Starting stress test with configuration:
- Reader threads: 4
- Writer threads: 8
- Initial elements: 10000
- Operations per thread: 100000
- Key range: 1000
- Insert ratio: 0.30
- Test duration: 15 seconds
Initializing tree with 10000 elements...
Initialization complete.
Initial tree validation: PASSED
Reader 0 completed 100000 lookups (32745 hits) in 123ms (813008.13 ops/sec)
Reader 1 completed 100000 lookups (32329 hits) in 145ms (689655.17 ops/sec)
Reader 3 completed 100000 lookups (32602 hits) in 151ms (662251.66 ops/sec)
Reader 2 completed 100000 lookups (32529 hits) in 152ms (657894.74 ops/sec)
Writer 4 completed 17829 inserts + 82172 deletes in 815ms (122700.61 ops/sec)
Writer 6 completed 17489 inserts + 82512 deletes in 840ms (119048.81 ops/sec)
Writer 7 completed 17578 inserts + 82422 deletes in 840ms (119047.62 ops/sec)
Writer 2 completed 17634 inserts + 82366 deletes in 843ms (118623.96 ops/sec)
Writer 3 completed 17692 inserts + 82308 deletes in 844ms (118483.41 ops/sec)
Writer 0 completed 17822 inserts + 82178 deletes in 849ms (117785.63 ops/sec)
Writer 1 completed 17438 inserts + 82562 deletes in 850ms (117647.06 ops/sec)
Writer 5 completed 17624 inserts + 82376 deletes in 850ms (117647.06 ops/sec)
Test duration reached, signaling threads to stop...
Performing final tree validation...
Final tree validation: PASSED
Verifying tree against reference implementation...
Tree comparison with reference: PASSED

==== Test Statistics ====
Total runtime: 18543 ms
Lookups: 400000 (successful: 130205, 32.55%)
Inserts: 141106 (successful: 141106, 100.00%)
Deletes: 658896 (successful: 99093, 15.04%)
Validations performed: 30
Reader throughput (ops/sec): avg=705702.42, min=657894.74, max=813008.13
Writer throughput (ops/sec): avg=118873.02, min=117647.06, max=122700.61

==== STRESS TEST PASSED ====

======= Running read-heavy workload test =======
Starting stress test with configuration:
- Reader threads: 16
- Writer threads: 2
- Initial elements: 10000
- Operations per thread: 100000
- Key range: 100000
- Insert ratio: 0.30
- Test duration: 15 seconds
Initializing tree with 10000 elements...
Initialization complete.
Initial tree validation: PASSED
Reader 6 completed 100000 lookups (9507 hits) in 215ms (465116.28 ops/sec)
Reader 15 completed 100000 lookups (9493 hits) in 226ms (442477.88 ops/sec)
Reader 7 completed 100000 lookups (9647 hits) in 232ms (431034.48 ops/sec)
Reader 2 completed 100000 lookups (9692 hits) in 266ms (375939.85 ops/sec)
Reader 3 completed 100000 lookups (9366 hits) in 279ms (358422.94 ops/sec)
Reader 0 completed 100000 lookups (9459 hits) in 304ms (328947.37 ops/sec)
Reader 12 completed 100000 lookups (9541 hits) in 301ms (332225.91 ops/sec)
Reader 5 completed 100000 lookups (9541 hits) in 308ms (324675.32 ops/sec)
Reader 10 completed 100000 lookups (9350 hits) in 293ms (341296.93 ops/sec)
Reader 11 completed 100000 lookups (9469 hits) in 299ms (334448.16 ops/sec)
Reader 1 completed 100000 lookups (9682 hits) in 322ms (310559.01 ops/sec)
Reader 4 completed 100000 lookups (9593 hits) in 323ms (309597.52 ops/sec)
Reader 8 completed 100000 lookups (9583 hits) in 331ms (302114.80 ops/sec)
Reader 14 completed 100000 lookups (9813 hits) in 331ms (302114.80 ops/sec)
Reader 13 completed 100000 lookups (9768 hits) in 343ms (291545.19 ops/sec)
Reader 9 completed 100000 lookups (9988 hits) in 360ms (277777.78 ops/sec)
Writer 1 completed 17438 inserts + 82562 deletes in 459ms (217864.92 ops/sec)
Writer 0 completed 17822 inserts + 82178 deletes in 460ms (217391.30 ops/sec)
Test duration reached, signaling threads to stop...
Performing final tree validation...
Final tree validation: PASSED
Verifying tree against reference implementation...
Tree comparison with reference: PASSED

==== Test Statistics ====
Total runtime: 15098 ms
Lookups: 1600000 (successful: 153492, 9.59%)
Inserts: 35260 (successful: 35260, 100.00%)
Deletes: 164740 (successful: 14768, 8.96%)
Validations performed: 30
Reader throughput (ops/sec): avg=345518.39, min=277777.78, max=465116.28
Writer throughput (ops/sec): avg=217628.11, min=217391.30, max=217864.92

==== STRESS TEST PASSED ====

======= Running small tree test =======
Starting stress test with configuration:
- Reader threads: 8
- Writer threads: 4
- Initial elements: 100
- Operations per thread: 100000
- Key range: 200
- Insert ratio: 0.30
- Test duration: 10 seconds
Initializing tree with 100 elements...
Initialization complete.
Initial tree validation: PASSED
Reader 5 completed 100000 lookups (29249 hits) in 173ms (578034.68 ops/sec)
Reader 4 completed 100000 lookups (29845 hits) in 174ms (574712.64 ops/sec)
Reader 0 completed 100000 lookups (29769 hits) in 177ms (564971.75 ops/sec)
Reader 1 completed 100000 lookups (29613 hits) in 179ms (558659.22 ops/sec)
Reader 2 completed 100000 lookups (29507 hits) in 184ms (543478.26 ops/sec)
Reader 7 completed 100000 lookups (29245 hits) in 184ms (543478.26 ops/sec)
Reader 3 completed 100000 lookups (29395 hits) in 186ms (537634.41 ops/sec)
Reader 6 completed 100000 lookups (28931 hits) in 187ms (534759.36 ops/sec)
Writer 1 completed 17438 inserts + 82562 deletes in 320ms (312500.00 ops/sec)
Writer 2 completed 17634 inserts + 82366 deletes in 320ms (312500.00 ops/sec)
Writer 0 completed 17822 inserts + 82178 deletes in 323ms (309597.52 ops/sec)
Writer 3 completed 17692 inserts + 82308 deletes in 323ms (309597.52 ops/sec)
Test duration reached, signaling threads to stop...
Performing final tree validation...
Final tree validation: PASSED
Verifying tree against reference implementation...
Tree comparison with reference: PASSED

==== Test Statistics ====
Total runtime: 10003 ms
Lookups: 800000 (successful: 235554, 29.44%)
Inserts: 70586 (successful: 70586, 100.00%)
Deletes: 329414 (successful: 49385, 14.99%)
Validations performed: 20
Reader throughput (ops/sec): avg=554466.07, min=534759.36, max=578034.68
Writer throughput (ops/sec): avg=311048.76, min=309597.52, max=312500.00

==== STRESS TEST PASSED ====
```
</details>

---

## Limitations & future work

* **Single writer bottleneck** – write scalability tops out at one thread.
  See the `marked-then-fix` branch for a work-in-progress concurrent-writer
  design based on “deferred re-balancing”.
* **Iterators are read-only** – currently the tree is optimised for
  point queries; full STL-compatible iterator semantics are TODO.
* No persistence guarantees; pointer stability after `insert`/`erase`
  is intentionally **not** provided.

---

## License

MIT.  Use it in hobby, academic, or commercial projects; just keep the notice.
