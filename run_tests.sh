#!/bin/bash
g++ -Iinclude -Itest/include test/test_harbor_breeze.cpp src/harbor_breeze.cpp -o test_runner
./test_runner
