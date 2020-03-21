#! /bin/bash

clang-format --version
find src eos-vm include test -name '*.hpp' -o -name '*.cpp' -o -name '*.h' -o -name '*.c' | xargs clang-format -i
