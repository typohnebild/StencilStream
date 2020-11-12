#!/bin/bash
#SBATCH -p fpgasyn
#SBATCH -o build.log
#SBATCH -J main-build
#SBATCH --mail-type=ALL
#SBATCH --mem=75GB
#SBATCH --time=3-00:00:00

source /cm/shared/opt/intel_oneapi/beta-10/setvars.sh
module load nalla_pcie compiler/GCC 

export HARDWARE=1
export PIPELINE_LEN=10

time make main
tar -cf - main main.prj/reports | ~/pigz > lean.tar.gz &
tar -cf - main main.prj | ~/pigz > full.tar.gz &
rm -r main.prj
wait