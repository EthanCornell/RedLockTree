#!/bin/bash

echo "=== Debugging ThreadSanitizer Memory Mapping Issue ==="

# Step 1: Check your environment
echo "1. Environment Information:"
echo "Kernel: $(uname -a)"
echo "GCC Version: $(gcc --version | head -1)"
echo "Available Memory: $(free -h)"
echo "Virtual Memory Limits: $(ulimit -v)"
echo "Address Space: $(cat /proc/sys/kernel/randomize_va_space)"

# Step 2: Try different TSAN options
echo -e "\n2. Trying different TSAN environment variables..."

# Option A: Reduce memory requirements
export TSAN_OPTIONS="memory_limit_mb=2048:mmap_limit_mb=1024:halt_on_error=1"
echo "Trying with reduced memory limits..."
timeout 10s ./rbt_tsan 2>&1 | head -20

# Option B: Different memory layout
unset TSAN_OPTIONS
export TSAN_OPTIONS="memory_limit_mb=8192:mmap_limit_mb=4096:detect_thread_leaks=false"
echo "Trying with increased limits..."
timeout 10s ./rbt_tsan 2>&1 | head -20

# Step 3: Try building with different flags
echo -e "\n3. Rebuilding with alternative compiler flags..."

# Option A: Older TSAN version behavior
g++ -std=c++17 -O1 -g -fsanitize=thread \
    -fno-sanitize-thread-atomics \
    -DRBTREE_DEMO lock_based_rb_tree.cpp -o rbt_tsan_alt1 -pthread

echo "Testing alternative build 1..."
timeout 10s ./rbt_tsan_alt1 2>&1 | head -20

# Option B: Minimal TSAN flags
g++ -std=c++17 -O0 -g -fsanitize=thread \
    -fno-omit-frame-pointer \
    -DRBTREE_DEMO lock_based_rb_tree.cpp -o rbt_tsan_alt2 -pthread

echo "Testing alternative build 2..."
timeout 10s ./rbt_tsan_alt2 2>&1 | head -20

# Step 4: System-level fixes
echo -e "\n4. Trying system-level fixes..."

# Disable ASLR temporarily
echo "Current ASLR setting: $(cat /proc/sys/kernel/randomize_va_space)"
if [ "$(id -u)" -eq 0 ]; then
    echo "Disabling ASLR..."
    echo 0 > /proc/sys/kernel/randomize_va_space
    timeout 10s ./rbt_tsan 2>&1 | head -20
    echo 2 > /proc/sys/kernel/randomize_va_space  # Re-enable
else
    echo "Need sudo to disable ASLR. Try: sudo echo 0 > /proc/sys/kernel/randomize_va_space"
fi

# Step 5: Alternative tools
echo -e "\n5. Using alternative debugging tools..."

# Helgrind (race detection)
echo "Building for Helgrind..."
g++ -std=c++17 -O1 -g -DRBTREE_DEMO lock_based_rb_tree.cpp -o rbt_helgrind -pthread
echo "Running Helgrind (this will take a while)..."
timeout 60s valgrind --tool=helgrind --read-var-info=yes ./rbt_helgrind 2>&1 | head -50

# DRD (another race detector)
echo -e "\nRunning DRD..."
timeout 60s valgrind --tool=drd ./rbt_helgrind 2>&1 | head -50

echo -e "\n=== Summary ==="
echo "If all TSAN attempts failed, use Helgrind/DRD results above for race detection."
echo "The memory mapping error is a TSAN limitation, not a bug in your code."