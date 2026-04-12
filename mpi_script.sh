#!/bin/bash

#SBATCH --job-name=sqx_mpi
#SBATCH --output="%x-%J.out"
#SBATCH --error="%x-%J.err"
#SBATCH --exclusive
#SBATCH --ntasks=4
#SBATCH --ntasks-per-node=1

source /etc/profile.d/zz-cray-pe.sh

export MV2_HOMOGENEOUS_CLUSTER=1
export MV2_SUPPRESS_JOB_STARTUP_PERFORMANCE_WARNING=1
export MV2_ENABLE_AFFINITY=0

module load cray-mvapich2_pmix_nogpu

srun ./sqx_mpi mapb10_10a.txt 16
