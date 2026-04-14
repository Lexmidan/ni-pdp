#!/bin/bash
# Benchmark orchestrator for sequential, task-parallel, and data-parallel solvers.
#
# Usage:  ./benchmark.sh [RUNS]
#   RUNS = number of repetitions per configuration (default: 20)
#
# Output: results are appended to bench_results.csv
#         (one BENCH_RESULT line per run, extracted from program stdout)

set -euo pipefail

RUNS="${1:-20}"
RESULT_FILE="bench_results_no_dfs_count.csv"
MAPB_DIR="mapb"

THREAD_COUNTS=(1 2 4 12 16 32 48)

# Binaries (adjust paths if built elsewhere)
BIN_SEQ="./build/sqx_seq"
BIN_TASK="./build/sqx_task"
BIN_DATA="./build/sqx_data"

# Write CSV header if file doesn't exist
if [[ ! -f "$RESULT_FILE" ]]; then
    echo "variant,input,threads,score,time_sec,run" > "$RESULT_FILE"
fi

# Collect input files
INPUTS=()
for f in "$MAPB_DIR"/*.txt; do
    INPUTS+=("$(basename "$f")")
done

echo "Benchmark started: $(date)"
echo "  Inputs: ${INPUTS[*]}"
echo "  Runs per config: $RUNS"
echo "  Thread counts (OpenMP): ${THREAD_COUNTS[*]}"
echo "  Results file: $RESULT_FILE"
echo ""

# Sequential
echo ">>> Sequential solver"
for input in "${INPUTS[@]}"; do
    for run in $(seq 1 "$RUNS"); do
        echo "  [$run/$RUNS] $BIN_SEQ $input"
        output=$("$BIN_SEQ" "$input" 2>&1) || true
        line=$(echo "$output" | grep "^BENCH_RESULT," || true)
        if [[ -n "$line" ]]; then
            # Strip prefix "BENCH_RESULT," and append run number
            echo "${line#BENCH_RESULT,},$run" >> "$RESULT_FILE"
        else
            echo "  WARNING: no BENCH_RESULT line found"
        fi
    done
done

# Task parallelism
echo ">>> Task-parallel solver"
for input in "${INPUTS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        for run in $(seq 1 "$RUNS"); do
            echo "  [$run/$RUNS] $BIN_TASK $input $threads"
            output=$("$BIN_TASK" "$input" "$threads" 2>&1) || true
            line=$(echo "$output" | grep "^BENCH_RESULT," || true)
            if [[ -n "$line" ]]; then
                echo "${line#BENCH_RESULT,},$run" >> "$RESULT_FILE"
            else
                echo "  WARNING: no BENCH_RESULT line found"
            fi
        done
    done
done

# Data parallelism
echo ">>> Data-parallel solver"
for input in "${INPUTS[@]}"; do
    for threads in "${THREAD_COUNTS[@]}"; do
        for run in $(seq 1 "$RUNS"); do
            echo "  [$run/$RUNS] $BIN_DATA $input $threads"
            output=$("$BIN_DATA" "$input" "$threads" 2>&1) || true
            line=$(echo "$output" | grep "^BENCH_RESULT," || true)
            if [[ -n "$line" ]]; then
                echo "${line#BENCH_RESULT,},$run" >> "$RESULT_FILE"
            else
                echo "  WARNING: no BENCH_RESULT line found"
            fi
        done
    done
done

echo ""
echo "Benchmark finished: $(date)"
echo "Results in $RESULT_FILE"
