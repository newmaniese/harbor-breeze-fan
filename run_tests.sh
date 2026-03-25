#!/bin/bash
set -e
g++ -Iinclude -Itest/include src/harbor_breeze.cpp test/test_harbor_breeze.cpp -o test_runner
./test_runner
rm test_runner
