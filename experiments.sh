
#algorithms="lru drrip hawkeye belady"
algorithms="hawkj"
#benches="403.gcc-48B 410.bwaves-945B 416.gamess-875B 482.sphinx3-1522B 619.lbm_s-4268B"
#400.perlbench-41B
#401.bzip2-277B
#430.mcf-22B
#435.milc-127B
#434.zeusmp-10B
#435.gromacs-111B
#436.cactusADM-1804B
#437.leslie3d-134B
#444.namd-23B
#445.gobmk-2B
#447.dealII-3B
#450.soplex-92B
#453.povray-252B
#454.calculix-104B
#benches="619.lbm_s-4268B 482.sphinx3-1522B 464.h264ref-30B 458.sjeng-31B"
benches="456.hmmer-88B 436.cactusADM-1804B 444.namd-23B"
#benches="
#456.hmmer-88B
#458.sjeng-31B
#459.GemsFDTD-765B
#462.libquantum-714B
#464.h264ref-30B
#465.tonto-44B
#471.omnetpp-188B
#473.astar-42B
#481.wrf-196B
#483.xalancbmk-127B
#600.perlbench_s-210B
#602.gcc_s-734B
#603.bwaves_s-891B
#605.mcf_s-472B
#607.cactuBSSN_s-2421B
#620.omnetpp_s-141B
#621.wrf_s-575B
#623.xalancbmk_s-10B
#625.x264_s-12B
#627.cam4_s-490B
#628.pop2_s-17B
#631.deepsjeng_s-928B
#638.imagick_s-824B
#641.leela_s-149B
#644.nab_s-5853B
#648.exchange2_s-72B
#649.fotonik3d_s-1B
#654.roms_s-293B
#657.xz_s-56B"

sim=200000000
trace_path="./dpc3_traces"

for bench in $benches; do

	for alg in $algorithms; do
		binary="bimodal-no-no-no-no-$alg-1core"

		# run simulation
		echo "Running $alg simulation $sim instructions $bench"
		./bin/$binary -warmup_instructions 1000000 -simulation_instructions $sim 1 -traces $trace_path/$bench.champsimtrace.xz > $alg-$bench-$sim.txt
		mv output_$alg.csv $alg-$bench-$sim.csv

		# create archive
		echo "Creating archive $alg-$bench-$sim.tar.bz2"
		tar cjf $alg-$bench-$sim.tar.bz2 $alg-$bench-$sim.txt $alg-$bench-$sim.csv
	done
done
