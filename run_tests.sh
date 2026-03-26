#!/usr/bin/env bash
set -e

echo "Compiling tests..."
g++ -I./include -I./test/include src/harbor_breeze.cpp test/test_harbor_breeze.cpp -o test_runner

echo "Running tests..."
./test_runner
