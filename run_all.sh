#!/bin/bash
#SBATCH --job-name=sqx_bench
#SBATCH --output="sqx_bench-%J.out"
#SBATCH --error="sqx_bench-%J.err"
#SBATCH --exclusive
#SBATCH --partition=arm_fast
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=48

for f in mapb/*.txt; do
    ./build/sqx_task "$(basename "$f")" 8
done
