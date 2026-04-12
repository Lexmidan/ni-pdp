#!/bin/bash
#SBATCH --job-name=sqx_bench
#SBATCH --output="sqx_bench-%J.out"
#SBATCH --error="sqx_bench-%J.err"
#SBATCH --exclusive
#SBATCH --partition=arm_serial
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=48

# Load modules if needed (adjust for your cluster)
# module load gcc
# source /etc/profile.d/zz-cray-pe.sh

echo "Node: $(hostname)"
echo "CPUs available: $(nproc)"
echo "Date: $(date)"
echo ""

cd "$SLURM_SUBMIT_DIR" || exit 1

# Build all three variants
mkdir -p build
CC -O2 -std=c++17 -o build/sqx_seq src/main_sequential.cpp
CC -O2 -std=c++17 -fopenmp -o build/sqx_task src/main_task_paral.cpp
CC -O2 -std=c++17 -fopenmp -o build/sqx_data src/main_data_paral.cpp

# Run benchmark (20 repetitions per config)
bash benchmark.sh 10
