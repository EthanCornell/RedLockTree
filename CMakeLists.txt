# Keep it simple now

cmake_minimum_required(VERSION 3.28)

project(rbtree)

add_executable(stress rbtree_stress_test.cpp)

target_include_directories(stress PUBLIC
    include
)

add_executable(demo demo.cpp)

target_include_directories(demo PUBLIC
    include
)
