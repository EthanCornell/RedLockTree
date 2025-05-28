// rbtree_stress_test.cpp
// Comprehensive stress test for thread-safe lock-based red-black tree
// -------------------------------------------------------------------
// Build: g++ -std=c++17 -pthread -O2 rbtree_stress_test.cpp -o rbtree_stress_test

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Include the RB-tree implementation
#include "lock_based_rb_tree.hpp"

#include "printer.hpp"

util::printer printer(0);

// Configuration parameters
struct TestConfig {
    size_t num_reader_threads = 8;     // Number of reader threads
    size_t num_writer_threads = 4;     // Number of writer threads
    size_t initial_elements = 10000;   // Elements to insert before test starts
    size_t operations_per_thread = 100000; // Operations each thread performs
    size_t key_range = 100000;         // Range of possible keys (0 to key_range-1)
    double insert_ratio = 0.3;         // Probability of insert vs. delete for writers
    bool validate_periodically = true; // Validate RB properties periodically
    size_t validation_interval = 10000; // How often to validate (operations)
    std::chrono::seconds test_duration{30}; // Maximum test duration
    bool verify_results = true;        // Verify final state against reference
};

// Statistics tracking
struct TestStats {
    std::atomic<size_t> total_lookups{0};
    std::atomic<size_t> successful_lookups{0};
    std::atomic<size_t> total_inserts{0};
    std::atomic<size_t> successful_inserts{0};
    std::atomic<size_t> total_deletes{0};
    std::atomic<size_t> successful_deletes{0};
    std::atomic<size_t> validation_count{0};
    std::vector<double> reader_throughput;
    std::vector<double> writer_throughput;
    std::chrono::milliseconds total_runtime{0};
    std::mutex agg_mtx;
    
    void print() const {
        std::cout << "\n==== Test Statistics ====\n";
        std::cout << "Total runtime: " << total_runtime.count() << " ms\n";
        std::cout << "Lookups: " << total_lookups << " (successful: " 
                  << successful_lookups << ", " 
                  << (100.0 * successful_lookups / std::max<size_t>(1, total_lookups)) << "%)\n";
        std::cout << "Inserts: " << total_inserts << " (successful: " 
                  << successful_inserts << ", " 
                  << (100.0 * successful_inserts / std::max<size_t>(1, total_inserts)) << "%)\n";
        std::cout << "Deletes: " << total_deletes << " (successful: " 
                  << successful_deletes << ", " 
                  << (100.0 * successful_deletes / std::max<size_t>(1, total_deletes)) << "%)\n";
        std::cout << "Validations performed: " << validation_count << "\n";
        
        if (!reader_throughput.empty()) {
            double avg_reader = std::accumulate(reader_throughput.begin(), reader_throughput.end(), 0.0) / reader_throughput.size();
            double min_reader = *std::min_element(reader_throughput.begin(), reader_throughput.end());
            double max_reader = *std::max_element(reader_throughput.begin(), reader_throughput.end());
            std::cout << "Reader throughput (ops/sec): avg=" << std::fixed << std::setprecision(2) << avg_reader 
                      << ", min=" << min_reader << ", max=" << max_reader << "\n";
        }
        
        if (!writer_throughput.empty()) {
            double avg_writer = std::accumulate(writer_throughput.begin(), writer_throughput.end(), 0.0) / writer_throughput.size();
            double min_writer = *std::min_element(writer_throughput.begin(), writer_throughput.end());
            double max_writer = *std::max_element(writer_throughput.begin(), writer_throughput.end());
            std::cout << "Writer throughput (ops/sec): avg=" << std::fixed << std::setprecision(2) << avg_writer 
                      << ", min=" << min_writer << ", max=" << max_writer << "\n";
        }
    }
};

// Thread-safe validator class
class TreeValidator {
private:
    std::mutex validation_mutex;
    std::atomic<bool> validation_in_progress{false};
    std::atomic<size_t> validation_requests{0};
    std::atomic<size_t> validations_performed{0};
    std::atomic<bool> validation_failed{false};

public:
    // Try to start a validation if one is not already in progress
    bool try_validate(const rbt::RBTree<int, int>& tree, const std::string& context) {
        if (validation_in_progress.load(std::memory_order_relaxed)) {
            // Another thread is already validating
            return false;
        }
        
        // Try to acquire validation rights
        bool expected = false;
        if (!validation_in_progress.compare_exchange_strong(expected, true)) {
            return false;
        }
        
        validation_requests++;
        
        // std::lock_guard<std::mutex> lock(validation_mutex);

        /* 🔒  Lock the tree’s writers-mutex so no writer mutates the structure
        while we run the (read-only) validate() traversal. */
        std::lock_guard<std::mutex> guard(tree.writer_mutex());

        bool valid = tree.validate();
        validations_performed++;
        
        if (!valid) {
            std::cerr << "VALIDATION FAILED during " << context << "!\n";
            validation_failed = true;
        }
        
        validation_in_progress.store(false, std::memory_order_relaxed);
        return valid;
    }
    
    size_t get_validations_performed() const {
        return validations_performed.load();
    }
    
    bool has_validation_failed() const {
        return validation_failed.load();
    }
};

// Helper to generate random keys and values
class RandomGenerator {
private:
    std::mt19937 gen;
    std::uniform_int_distribution<int> key_dist;
    std::uniform_int_distribution<int> val_dist;
    std::uniform_real_distribution<double> op_dist{0.0, 1.0};
    
public:
    RandomGenerator(size_t key_range, size_t seed) 
        : gen(seed), key_dist(0, key_range - 1), val_dist(0, std::numeric_limits<int>::max()) {}
    
    int random_key() {
        return key_dist(gen);
    }
    
    int random_value() {
        return val_dist(gen);
    }
    
    double random_probability() {
        return op_dist(gen);
    }
};

// Reference implementation to verify results
class ReferenceMap {
private:
    std::unordered_map<int, int> map;
    mutable std::shared_mutex mutex;
    
public:
    void insert(int key, int val) {
        std::unique_lock lock(mutex);
        map[key] = val;
    }
    
    bool erase(int key) {
        std::unique_lock lock(mutex);
        return map.erase(key) > 0;
    }
    
    std::optional<int> lookup(int key) const {
        std::shared_lock lock(mutex);
        auto it = map.find(key);
        if (it == map.end()) return std::nullopt;
        return it->second;
    }
    
    size_t size() const {
        std::shared_lock lock(mutex);
        return map.size();
    }
    
    // Compare with RB tree (not thread-safe, call when testing is complete)
    bool compare_with_tree(const rbt::RBTree<int, int>& tree) {
        std::shared_lock lock(mutex);
        for (const auto& [key, val] : map) {
            auto tree_val = tree.lookup(key);
            if (!tree_val || *tree_val != val) {
                std::cerr << "Mismatch for key " << key << ": reference=" << val 
                          << ", tree=" << (tree_val ? std::to_string(*tree_val) : "not found") << "\n";
                return false;
            }
        }
        return true;
    }
};

// Running the tests
void initialize_tree(rbt::RBTree<int, int>& tree, ReferenceMap& reference, const TestConfig& config) {
    std::cout << "Initializing tree with " << config.initial_elements << " elements...\n";
    
    // Use deterministic seed for reproducibility
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> key_dist(0, config.key_range - 1);
    std::uniform_int_distribution<int> val_dist(0, std::numeric_limits<int>::max());
    
    // Insert initial elements
    size_t inserted = 0;
    while (inserted < config.initial_elements) {
        int key = key_dist(gen);
        int val = val_dist(gen);
        tree.insert(key, val);
        reference.insert(key, val);
        inserted++;
    }
    
    std::cout << "Initialization complete.\n";
}

// Reader thread function
void reader_thread_func(
    rbt::RBTree<int, int>& tree,
    const TestConfig& config,
    TestStats& stats,
    TreeValidator& validator,
    std::atomic<bool>& stop_flag,
    size_t thread_id
) {
    RandomGenerator rng(config.key_range, thread_id + 1000);
    size_t ops = 0;
    size_t successful = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (!stop_flag.load() && ops < config.operations_per_thread) {
        int key = rng.random_key();
        auto value = tree.lookup(key);
        stats.total_lookups++;
        
        if (value) {
            successful++;
            stats.successful_lookups++;
        }
        
        ops++;
        
        // Occasionally try to validate the tree
        if (config.validate_periodically && ops % config.validation_interval == 0) {
            validator.try_validate(tree, "reader thread");
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate throughput
    double seconds = duration.count() / 1000.0;
    double throughput = ops / std::max(seconds, 0.001);
    
    // Update statistics
    // std::lock_guard<std::mutex> lock(*new std::mutex());  // Just for thread-safe vector access
    // stats.reader_throughput.push_back(throughput);

    {
        std::lock_guard<std::mutex> lock(stats.agg_mtx);
        stats.reader_throughput.push_back(throughput);
    }
    
    std::cout << "Reader " << thread_id << " completed " << ops << " lookups (" 
              << successful << " hits) in " << duration.count() << "ms ("
              << std::fixed << std::setprecision(2) << throughput << " ops/sec)\n";
}

// Writer thread function
void writer_thread_func(
    rbt::RBTree<int, int>& tree,
    ReferenceMap& reference,
    const TestConfig& config,
    TestStats& stats,
    TreeValidator& validator,
    std::atomic<bool>& stop_flag,
    size_t thread_id
) {
    RandomGenerator rng(config.key_range, thread_id + 2000);
    size_t inserts = 0, successful_inserts = 0;
    size_t deletes = 0, successful_deletes = 0;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    while (!stop_flag.load() && (inserts + deletes) < config.operations_per_thread) {
        // Decide whether to insert or delete
        bool do_insert = rng.random_probability() < config.insert_ratio;
        
        if (do_insert) {
            int key = rng.random_key();
            int val = rng.random_value();
            
            // Update both tree and reference
            tree.insert(key, val);
            reference.insert(key, val);
            
            stats.total_inserts++;
            inserts++;
            successful_inserts++;
            stats.successful_inserts++;
        } else {
            int key = rng.random_key();
            
            // Try to delete from both
            bool success = tree.erase(key);
            reference.erase(key);
            stats.total_deletes++;
            deletes++;
            if (success) {
                // reference.erase(key);
                successful_deletes++;
                stats.successful_deletes++;
            }
            
            stats.total_deletes++;
            deletes++;
        }
        
        // Occasionally try to validate the tree
        if (config.validate_periodically && (inserts + deletes) % config.validation_interval == 0) {
            validator.try_validate(tree, "writer thread");
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate throughput
    double seconds = duration.count() / 1000.0;
    double throughput = (inserts + deletes) / std::max(seconds, 0.001);
    
    // Update statistics
    // std::lock_guard<std::mutex> lock(*new std::mutex());  // Just for thread-safe vector access
    // stats.writer_throughput.push_back(throughput);

    {
        std::lock_guard<std::mutex> lock(stats.agg_mtx);
        stats.writer_throughput.push_back(throughput);
    }
    
    std::cout << "Writer " << thread_id << " completed " << inserts << " inserts + " 
              << deletes << " deletes in " << duration.count() << "ms ("
              << std::fixed << std::setprecision(2) << throughput << " ops/sec)\n";
}

// Periodic validator thread function
void validator_thread_func(
    rbt::RBTree<int, int>& tree,
    TreeValidator& validator,
    std::atomic<bool>& stop_flag,
    const TestConfig& config,
    TestStats& stats
) {
    const auto validation_sleep = std::chrono::milliseconds(500);
    
    while (!stop_flag.load()) {
        bool valid = validator.try_validate(tree, "validator thread");
        if (valid) {
            stats.validation_count++;
        }
        
        std::this_thread::sleep_for(validation_sleep);
    }
}

void run_stress_test(const TestConfig& config) {
    std::cout << "Starting stress test with configuration:\n"
              << "- Reader threads: " << config.num_reader_threads << "\n"
              << "- Writer threads: " << config.num_writer_threads << "\n"
              << "- Initial elements: " << config.initial_elements << "\n"
              << "- Operations per thread: " << config.operations_per_thread << "\n"
              << "- Key range: " << config.key_range << "\n"
              << "- Insert ratio: " << config.insert_ratio << "\n"
              << "- Test duration: " << config.test_duration.count() << " seconds\n";
    
    // Create RB tree and reference implementation
    rbt::RBTree<int, int> tree;
    ReferenceMap reference;
    
    // Initialize tree with data
    initialize_tree(tree, reference, config);
    
    // Create validator
    TreeValidator validator;
    
    // Validate initial tree
    bool initial_valid = tree.validate();
    std::cout << "Initial tree validation: " << (initial_valid ? "PASSED" : "FAILED") << "\n";
    if (!initial_valid) {
        std::cerr << "ERROR: Initial tree is invalid. Aborting test.\n";
        return;
    }
    
    // Track statistics
    TestStats stats;
    
    // Create thread stop flag
    std::atomic<bool> stop_flag(false);
    
    // Start timing
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Launch reader threads
    std::vector<std::thread> reader_threads;
    for (size_t i = 0; i < config.num_reader_threads; i++) {
        reader_threads.emplace_back(reader_thread_func, 
            std::ref(tree), std::ref(config), std::ref(stats),
            std::ref(validator), std::ref(stop_flag), i);
    }
    
    // Launch writer threads
    std::vector<std::thread> writer_threads;
    for (size_t i = 0; i < config.num_writer_threads; i++) {
        writer_threads.emplace_back(writer_thread_func, 
            std::ref(tree), std::ref(reference), std::ref(config), std::ref(stats),
            std::ref(validator), std::ref(stop_flag), i);
    }
    
    // Launch dedicated validator thread
    std::thread validator_thread(validator_thread_func,
        std::ref(tree), std::ref(validator), std::ref(stop_flag),
        std::ref(config), std::ref(stats));
    
    // Wait for test duration
    std::this_thread::sleep_for(config.test_duration);
    
    // Signal threads to stop
    std::cout << "Test duration reached, signaling threads to stop...\n";
    stop_flag.store(true);
    
    // Join all threads
    for (auto& t : reader_threads) {
        t.join();
    }
    for (auto& t : writer_threads) {
        t.join();
    }
    validator_thread.join();
    
    // Calculate total runtime
    auto end_time = std::chrono::high_resolution_clock::now();
    stats.total_runtime = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Final validation
    std::cout << "Performing final tree validation...\n";
    bool final_valid = tree.validate();
    std::cout << "Final tree validation: " << (final_valid ? "PASSED" : "FAILED") << "\n";
    
    // Verify against reference implementation if requested
    bool comparison_valid = true;
    if (config.verify_results) {
        std::cout << "Verifying tree against reference implementation...\n";
        comparison_valid = reference.compare_with_tree(tree);
        std::cout << "Tree comparison with reference: " << (comparison_valid ? "PASSED" : "FAILED") << "\n";
    }
    
    // Print statistics
    stats.print();
    
    // Final result
    if (final_valid && comparison_valid && !validator.has_validation_failed()) {
        std::cout << "\n==== STRESS TEST PASSED ====\n";
    } else {
        std::cout << "\n==== STRESS TEST FAILED ====\n";
        if (!final_valid) std::cout << "  - Final tree validation failed\n";
        if (!comparison_valid) std::cout << "  - Tree comparison with reference failed\n";
        if (validator.has_validation_failed()) std::cout << "  - At least one validation during test failed\n";
    }
}

// Test variations with different configurations
void run_all_tests() {
    // Default configuration test
    {
        std::cout << "\n======= Running default configuration test =======\n";
        TestConfig config;
        run_stress_test(config);
    }
    
    // High contention test (many writers)
    {
        std::cout << "\n======= Running high writer contention test =======\n";
        TestConfig config;
        config.num_reader_threads = 4;
        config.num_writer_threads = 8;
        config.key_range = 1000;  // Smaller key range increases contention
        config.test_duration = std::chrono::seconds(15);
        run_stress_test(config);
    }
    
    // Read-heavy workload
    {
        std::cout << "\n======= Running read-heavy workload test =======\n";
        TestConfig config;
        config.num_reader_threads = 16;
        config.num_writer_threads = 2;
        config.test_duration = std::chrono::seconds(15);
        run_stress_test(config);
    }
    
    // Small tree test
    {
        std::cout << "\n======= Running small tree test =======\n";
        TestConfig config;
        config.initial_elements = 100;
        config.key_range = 200;
        config.test_duration = std::chrono::seconds(10);
        run_stress_test(config);
    }
}

int main() {
    util::println("==== Lock-Based RB-Tree Stress Test ====");
    
    // Set up hardware concurrency info
    unsigned int hw_threads = std::thread::hardware_concurrency();
    util::println("Running on system with {} hardware threads", hw_threads);
    
    // Run individual test or all tests
    run_all_tests();
    
    return 0;
}
