#!/bin/bash
#PBS -N gradNormal
#PBS -l nodes=4:ppn=16
#PBS -q tgp
#PBS -V
#PBS -m n
#PBS -k oe
#PBS -e data/gradNormal.err
#PBS -o data/gradNormal.out
#

EXEC_DIR=/data/dunham/kallison/eqCycle
INIT_DIR=$EXEC_DIR
cd $PBS_O_WORKDIR

mpirun $EXEC_DIR/main $INIT_DIR/basin.in
