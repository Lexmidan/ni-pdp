#!/bin/bash

#SBATCH --job-name=sqx_mpi
#SBATCH --output="%x-%J.out"
#SBATCH --error="%x-%J.err"
#SBATCH --exclusive
#SBATCH --partition=arm_long
#SBATCH --nodes=4
#SBATCH --ntasks=4
#SBATCH --ntasks-per-node=1

source /etc/profile.d/zz-cray-pe.sh

export MV2_HOMOGENEOUS_CLUSTER=1
export MV2_SUPPRESS_JOB_STARTUP_PERFORMANCE_WARNING=1
export MV2_ENABLE_AFFINITY=0

module load cray-mvapich2_pmix_nogpu

RESULT_FILE="bench_results_mpi_long.csv"
if [[ ! -f "$RESULT_FILE" ]]; then
    echo "variant,input,threads,score,time_sec,run" > "$RESULT_FILE"
fi

for input in mapb/*.txt; do
    input=$(basename "$input")
    for run in $(seq 1 20); do
        echo "[$run/20] $input"
        output=$(srun ./build/sqx_mpi "$input" 48 2>&1) || true
        line=$(echo "$output" | grep "^BENCH_RESULT," || true)
        if [[ -n "$line" ]]; then
            echo "${line#BENCH_RESULT,},$run" >> "$RESULT_FILE"
        else
            echo "WARNING: no BENCH_RESULT line for $input run $run"
        fi
    done
done

#srun ./sqx_mpi mapb10_10a.txt 48