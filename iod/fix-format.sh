#!/bin/bash

clang-format --style=file -i $(git status -s | grep -E '\.c|\.cpp|\.h|\.hpp' | awk '$1 == "M"{print $2}')
