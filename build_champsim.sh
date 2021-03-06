#!/bin/bash

# Environment variables: BRANCH, CORES, L1I_PREF, L1D_PREF, L2C_PREF, LLC_PREF, LLC_REPL

# Legacy ChampSim configuration
if [ "$#" == 7 ]; then
    #echo "Usage: ./build_champsim.sh [branch_pred] [l1d_pref] [l2c_pref] [llc_pref] [llc_repl] [num_core]"
	BRANCH=$1           # branch/*.bpred
	L1I_PREFETCHER=$2   # prefetcher/*.l1i_pref
	L1D_PREFETCHER=$3   # prefetcher/*.l1d_pref
	L2C_PREFETCHER=$4   # prefetcher/*.l2c_pref
	LLC_PREFETCHER=$5   # prefetcher/*.llc_pref
	LLC_REPLACEMENT=$6  # replacement/*.llc_repl
	NUM_CORE=$7         # tested up to 8-core system
fi

# Set defaults for any unset variables
BRANCH=${BRANCH:=bimodal}
L1I_PREFETCHER=${L1I_PREF:=no}
L1D_PREFETCHER=${L1D_PREF:=no}
L2C_PREFETCHER=${L2C_PREF:=no}
LLC_PREFETCHER=${LLC_PREF:=no}
LLC_REPLACEMENT=${LLC_REPL:=lru}
NUM_CORE=${CORES:=1}

############## Some useful macros ###############
BOLD=$(tput bold)
NORMAL=$(tput sgr0)
#################################################

function help_bpred()
{
	echo "Possible branch predictors from branch/*.bpred "
	find branch -name "*.bpred"
}

function help_l1i_pref()
{
	echo "Possible L1I prefetchers from prefetcher/*.l1i_pref "
	find prefetcher -name "*.l1i_pref"
}

function help_l1d_pref()
{
	echo "Possible L1D prefetchers from prefetcher/*.l1d_pref "
	find prefetcher -name "*.l1d_pref"
}

function help_l2c_pref()
{
	echo "Possible L2C prefetchers from prefetcher/*.l2c_pref "
	find prefetcher -name "*.l2c_pref"
}

function help_llc_pref()
{
	echo "Possible LLC prefetchers from prefetcher/*.llc_pref "
	find prefetcher -name "*.llc_pref"
}

function help_llc_repl()
{
	echo "Possible LLC replacement policy from replacement/*.llc_repl"
	find replacement -name "*.llc_repl"
}

# If the user asked for help, give some hints
if [ $BRANCH == "help" ]; then
	help_bpred
	exit 1
fi

if [ $L1I_PREF == "help" ]; then
	help_l1i_pref
	exit 1
fi

if [ $L1D_PREF == "help" ]; then
	help_l1d_pref
	exit 1
fi

if [ $L2C_PREF == "help" ]; then
	help_l2c_pref
	exit 1
fi

if [ $LLC_PREF == "help" ]; then
	help_llc_pref
	exit 1
fi

if [ $LLC_REPL == "help" ]; then
	help_llc_repl
	exit 1
fi

# Sanity check
if [ ! -f ./branch/${BRANCH}.bpred ]; then
    echo "[ERROR] Cannot find branch predictor:" \"$BRANCH\"
	help_bpred
    exit 1
fi

if [ ! -f ./prefetcher/${L1I_PREFETCHER}.l1i_pref ]; then
    echo "[ERROR] Cannot find L1I prefetcher"
	help_l1i_pref
    exit 1
fi

if [ ! -f ./prefetcher/${L1D_PREFETCHER}.l1d_pref ]; then
    echo "[ERROR] Cannot find L1D prefetcher"
	help_l1d_pref
    exit 1
fi

if [ ! -f ./prefetcher/${L2C_PREFETCHER}.l2c_pref ]; then
    echo "[ERROR] Cannot find L2C prefetcher"
	help_l2c_pref
    exit 1
fi

if [ ! -f ./prefetcher/${LLC_PREFETCHER}.llc_pref ]; then
    echo "[ERROR] Cannot find LLC prefetcher"
	help_llc_pref
    exit 1
fi

if [ ! -f ./replacement/${LLC_REPLACEMENT}.llc_repl ]; then
    echo "[ERROR] Cannot find LLC replacement policy"
	help_llc_repl
    exit 1
fi

# Check num_core
re='^[0-9]+$'
if ! [[ $NUM_CORE =~ $re ]] ; then
    echo "[ERROR]: num_core is NOT a number" >&2;
    exit 1
fi

# Check for multi-core
if [ "$NUM_CORE" -gt "1" ]; then
    echo "Building multi-core ChampSim..."
    sed -i.bak 's/\<NUM_CPUS 1\>/NUM_CPUS '${NUM_CORE}'/g' inc/champsim.h
#	sed -i.bak 's/\<DRAM_CHANNELS 1\>/DRAM_CHANNELS 2/g' inc/champsim.h
#	sed -i.bak 's/\<DRAM_CHANNELS_LOG2 0\>/DRAM_CHANNELS_LOG2 1/g' inc/champsim.h
else
    if [ "$NUM_CORE" -lt "1" ]; then
        echo "Number of core: $NUM_CORE must be greater or equal than 1"
        exit 1
    else
        echo "Building single-core ChampSim..."
    fi
fi
echo

# Change prefetchers and replacement policy
cp branch/${BRANCH}.bpred branch/branch_predictor.cc
cp prefetcher/${L1I_PREFETCHER}.l1i_pref prefetcher/l1i_prefetcher.cc
cp prefetcher/${L1D_PREFETCHER}.l1d_pref prefetcher/l1d_prefetcher.cc
cp prefetcher/${L2C_PREFETCHER}.l2c_pref prefetcher/l2c_prefetcher.cc
cp prefetcher/${LLC_PREFETCHER}.llc_pref prefetcher/llc_prefetcher.cc
cp replacement/${LLC_REPLACEMENT}.llc_repl replacement/llc_replacement.cc

# Build
mkdir -p bin
rm -f bin/champsim
make clean
make

# Sanity check
echo ""
if [ ! -f bin/champsim ]; then
    echo "${BOLD}ChampSim build FAILED!"
    echo ""
    exit 1
fi

echo "${BOLD}ChampSim is successfully built"
echo "Branch Predictor: ${BRANCH}"
echo "L1I Prefetcher: ${L1I_PREFETCHER}"
echo "L1D Prefetcher: ${L1D_PREFETCHER}"
echo "L2C Prefetcher: ${L2C_PREFETCHER}"
echo "LLC Prefetcher: ${LLC_PREFETCHER}"
echo "LLC Replacement: ${LLC_REPLACEMENT}"
echo "Cores: ${NUM_CORE}"
BINARY_NAME="${BRANCH}-${L1I_PREFETCHER}-${L1D_PREFETCHER}-${L2C_PREFETCHER}-${LLC_PREFETCHER}-${LLC_REPLACEMENT}-${NUM_CORE}core"
echo "Binary: bin/${BINARY_NAME}"
echo ""
mv bin/champsim bin/${BINARY_NAME}
