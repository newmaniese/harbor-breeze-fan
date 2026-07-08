#!/bin/bash
set -e

echo "Compiling Harbor Breeze core tests..."
g++ -I include -I test/include test/test_harbor_breeze.cpp src/harbor_breeze.cpp -o run_tests

echo "Running Harbor Breeze core tests..."
./run_tests

echo "Compiling RF Capture tests..."
g++ -I include -I test/include test/test_rf_capture.cpp src/rf_capture.cpp test/mock_arduino.cpp -o run_rf_tests

echo "Running RF Capture tests..."
./run_rf_tests
