#!/bin/bash
for f in mapb/*.txt; do
    ./build/sqx "$(basename "$f")" 8
done
