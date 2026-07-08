#!/bin/bash
set -e

echo "Compiling tests..."
g++ -I include -I test/include test/test_harbor_breeze.cpp src/harbor_breeze.cpp -o run_tests
g++ -I include -I test/include test/test_rf_capture.cpp src/rf_capture.cpp -o run_tests_rf

echo "Running tests..."
./run_tests
./run_tests_rf
