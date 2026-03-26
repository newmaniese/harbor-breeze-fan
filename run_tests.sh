#!/bin/bash
set -e

echo "Compiling tests..."
g++ -I include -I test/include test/test_harbor_breeze.cpp src/harbor_breeze.cpp -o run_tests

echo "Running tests..."
./run_tests
