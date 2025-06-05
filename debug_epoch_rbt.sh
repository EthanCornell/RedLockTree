#!/bin/bash

echo "=== Fixing AddressSanitizer Memory Issues ==="

# 1. Check current limits
echo "Current virtual memory limit: $(ulimit -v)"
echo "Available memory: $(free -h | grep Mem)"

# 2. Increase virtual memory limit
echo "Increasing virtual memory limit..."
ulimit -v unlimited
echo "New virtual memory limit: $(ulimit -v)"

# 3. Build with reduced AddressSanitizer memory requirements
echo "Building with optimized AddressSanitizer settings..."
g++ -std=c++17 -pthread -O1 -g \
    -fsanitize=address \
    -fno-omit-frame-pointer \
    -DRBTREE_DEMO epoch_based_rb_tree.cpp -o rbt_epoch_asan

# 4. Set AddressSanitizer options to use less memory
export ASAN_OPTIONS="mmap_limit_mb=2048:detect_thread_leaks=false:fast_unwind_on_malloc=0"

echo "Testing with optimized settings..."
./rbt_epoch_asan

# 5. Alternative: Build without AddressSanitizer but with debug info
echo -e "\nAlternative: Building clean debug version..."
g++ -std=c++17 -pthread -O1 -g \
    -DRBTREE_DEMO epoch_based_rb_tree.cpp -o rbt_epoch_debug

echo "Testing clean debug build..."
./rbt_epoch_debug

# 6. Test with Valgrind (more reliable in WSL2)
echo -e "\nTesting with Valgrind..."
valgrind --tool=memcheck --leak-check=full ./rbt_epoch_debug

echo -e "\nTesting with Helgrind for race detection..."
timeout 60s valgrind --tool=helgrind ./rbt_epoch_debug

echo -e "\n=== Summary ==="
echo "If AddressSanitizer still fails, use the debug build."
echo "The epoch-based algorithm should be memory-safe by design."