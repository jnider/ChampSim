/* Implement Belady's Algorithm (optimal) for cache replacement

Belady's algorithm is the optimal cache replacement policy, because it can see
into the future. This is possible, because we have the simulation trace, and
can know exactly what will happen until the end of the trace. This is useful
to measure the upper bound of cache replacement efficiency, by which all other
algorithms can be measured.
*/

#include "cache.h"
#include "ooo_cpu.h"

unsigned long mem_used;

#ifdef LOGGING
char rlog_name[] = "output_belady.csv";
FILE *rlog;
#endif // LOGGING

// use a radix tree for sparse lookups (va->next usage)
// 64-bit virtual address -> 64 byte cache line (drop 6 bits)
// Use: l1:10 bits l2:16 bits l3:16 bits l4:16 bits
#define RL1_BITS 16
#define RL2_BITS 16
#define RL3_BITS 16
#define RL4_BITS 16

#define RL1_ENTRIES (2<<RL1_BITS)
#define RL2_ENTRIES (2<<RL2_BITS)
#define RL3_ENTRIES (2<<RL3_BITS)
#define RL4_ENTRIES (2<<RL4_BITS)

struct timestamp_array
{
	uint32_t used; // how many data elements are filled
	uint32_t alloc; // how many data elements are allocated
	uint64_t start; // which data element is 'next'
	uint64_t *data; // the data
};

uint64_t* rtable[RL1_ENTRIES];

// insert a value
static void insert(uint64_t vaddr, uint64_t timestamp)
{
	//printf("Inserting va 0x%lx %lu\n", vaddr, timestamp);

	long unsigned int rl1_index = (vaddr & 0xFFFF000000000000) >> (64-RL1_BITS);
	long unsigned int rl2_index = (vaddr & 0x0000FFFF00000000) >> (64-RL1_BITS-RL2_BITS);
	long unsigned int rl3_index = (vaddr & 0x00000000FFFF0000) >> (64-RL1_BITS-RL2_BITS-RL3_BITS);
	long unsigned int rl4_index = (vaddr & 0x000000000000FFC0) >> (64-RL1_BITS-RL2_BITS-RL3_BITS-RL4_BITS);

	//printf("rl1_index = 0x%lx\n", rl1_index);
	//printf("rl2_index = 0x%lx\n", rl2_index);
	//printf("rl3_index = 0x%lx\n", rl3_index);
	//printf("rl4_index = 0x%lx\n", rl4_index);

	uint64_t* rl2_table = rtable[rl1_index];
	if (!rl2_table)
	{
		//printf("No RL2 entry for 0x%lx - creating\n", rl1_index << (64-RL1_BITS));
		rl2_table = (uint64_t*)calloc(RL2_ENTRIES, sizeof(uint64_t*));
		rtable[rl1_index] = rl2_table;
		mem_used += RL2_ENTRIES * sizeof(uint64_t*);
	}

	// level 3
	uint64_t* rl3_table = (uint64_t*)rl2_table[rl2_index];
	if (!rl3_table)
	{
		//printf("No entry for RL3 0x%lx - creating\n", rl2_index << (64-RL1_BITS-RL2_BITS));
		rl3_table = (uint64_t*)calloc(RL3_ENTRIES, sizeof(uint64_t*));
		rl2_table[rl2_index] = (uint64_t)rl3_table;
		mem_used += RL3_ENTRIES * sizeof(uint64_t*);
	}

	// level 4
	timestamp_array** rl4_table = (timestamp_array**)rl3_table[rl3_index];
	if (!rl4_table)
	{
		//printf("No entry for RL4 0x%lx - creating\n", rl3_index << (64-RL1_BITS-RL2_BITS-RL3_BITS));
		rl4_table = (timestamp_array**)calloc(RL4_ENTRIES, sizeof(uint64_t*));
		rl3_table[rl3_index] = (uint64_t)rl4_table;
		mem_used += RL4_ENTRIES * sizeof(uint64_t*);
	}

	// array of timestamps
	timestamp_array *tsarray = rl4_table[rl4_index];
	if (!tsarray)
	{
		//printf("No array for 0x%lx - creating\n", vaddr);
		tsarray = (timestamp_array *)malloc(sizeof(struct timestamp_array));
		mem_used += sizeof(struct timestamp_array);
		rl4_table[rl4_index] = tsarray;
		tsarray->data = (uint64_t*)calloc(4, sizeof(uint64_t));
		tsarray->alloc = 4; // number of entries allocated in the array
		tsarray->used = 0;
		tsarray->start = 0;
		mem_used += 4 * sizeof(uint64_t);
	}

/*
	if (vaddr == 0x6dddc8)
	{
		printf("Dumping 0x%lx at %lu\n", vaddr, timestamp);
		for (unsigned int i=0; i < tsarray->used; i++)
			printf(" [%u]: %lu\n", i, tsarray->data[i]);
	}
*/

	if (tsarray->used == tsarray->alloc)
	{
		unsigned int newsize = tsarray->alloc * 2;
		//printf("reallocating to %u for 0x%lx\n", newsize, vaddr);
		tsarray->data = (uint64_t*)reallocarray(tsarray->data, newsize, sizeof(uint64_t));
		mem_used += tsarray->alloc * sizeof(uint64_t);
		tsarray->alloc = newsize;
	}

	//printf("inserting timestamp\n");
	tsarray->data[tsarray->used++] = timestamp;
	//printf("Done\n");

}

static int lookup(uint64_t cur_time, uint64_t vaddr, uint64_t *timestamp)
{
	*timestamp = 0;

	//if (cur_time == 0)
	//	return -1;

	//printf("Looking up 0x%lx after time %lu\n", vaddr, cur_time);

	long unsigned int rl1_index = (vaddr & 0xFFFF000000000000) >> (64-RL1_BITS);
	long unsigned int rl2_index = (vaddr & 0x0000FFFF00000000) >> (64-RL1_BITS-RL2_BITS);
	long unsigned int rl3_index = (vaddr & 0x00000000FFFF0000) >> (64-RL1_BITS-RL2_BITS-RL3_BITS);
	long unsigned int rl4_index = (vaddr & 0x000000000000FFC0) >> (64-RL1_BITS-RL2_BITS-RL3_BITS-RL4_BITS);

	//printf("rl1_index = 0x%lx\n", rl1_index);
	//printf("rl2_index = 0x%lx\n", rl2_index);
	//printf("rl3_index = 0x%lx\n", rl3_index);
	//printf("rl4_index = 0x%lx\n", rl4_index);

	uint64_t* rl2_table = rtable[rl1_index];
	if (!rl2_table)
	{
		//printf("No RL2 table\n");
		return -2;
	}

	uint64_t* rl3_table = (uint64_t*)rl2_table[rl2_index];
	if (!rl3_table)
	{
		//printf("No RL3 table\n");
		return -3;
	}

	timestamp_array** rl4_table = (timestamp_array**)rl3_table[rl3_index];
	if (!rl4_table)
	{
		//printf("No RL4 table\n");
		return -4;
	}

	timestamp_array *tsarray = rl4_table[rl4_index];
	if (!tsarray)
	{
		//printf("No tsarray\n");
		return -5;
	}

/*
	for (unsigned int i=0; i < tsarray->used; i++)
	{
		if (tsarray->data[i] > cur_time)
		{
			*timestamp = tsarray->data[i];
			return 0;
		}
	}
*/
	if (tsarray->start < tsarray->used)
	{
		unsigned int i = tsarray->start;
		*timestamp = tsarray->data[i];
		//printf("Addr: 0x%lx next use @ %lu\n", vaddr, *timestamp);
		return 0;
	}

	return -1;
}

static int update(uint64_t vaddr)
{
	if (vaddr == 0)
		return 0;

	//printf("Update 0x%lx\n", vaddr);

	long unsigned int rl1_index = (vaddr & 0xFFFF000000000000) >> (64-RL1_BITS);
	long unsigned int rl2_index = (vaddr & 0x0000FFFF00000000) >> (64-RL1_BITS-RL2_BITS);
	long unsigned int rl3_index = (vaddr & 0x00000000FFFF0000) >> (64-RL1_BITS-RL2_BITS-RL3_BITS);
	long unsigned int rl4_index = (vaddr & 0x000000000000FFC0) >> (64-RL1_BITS-RL2_BITS-RL3_BITS-RL4_BITS);

	uint64_t* rl2_table = rtable[rl1_index];
	if (!rl2_table)
		return -2;

	uint64_t* rl3_table = (uint64_t*)rl2_table[rl2_index];
	if (!rl3_table)
		return -3;

	timestamp_array** rl4_table = (timestamp_array**)rl3_table[rl3_index];
	if (!rl4_table)
		return -4;

	timestamp_array *tsarray = rl4_table[rl4_index];
	if (!tsarray)
		return -5;

	if (tsarray->start < tsarray->used)
	{
		printf("Addr: 0x%lx was: %lu next: %lu\n", vaddr, tsarray->data[tsarray->start], tsarray->data[tsarray->start + 1]);
		tsarray->start++;
		return 0;
	}

	return -1;
}

// initialize replacement state
void CACHE::llc_initialize_replacement()
{
	input_instr in;
	long unsigned int ins=0;
	long unsigned int loads=0;
	long unsigned int stores=0;

	// create a record of all memory accesses in the whole trace for each CPU
	printf("Skipping %lu warmup instructions\n", ooo_cpu[0].warmup_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		if (ins >= ooo_cpu[0].warmup_instructions)
			break;
		ins++;
	}

	//uint64_t cur_time = ooo_cpu[0].warmup_instructions;

	printf("Loading %lu simulation instructions\n", ooo_cpu[0].simulation_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		for (int s=0; s < NUM_INSTR_SOURCES; s++)
		{
			if (in.source_memory[s])
			{
				//printf("%i:   LOAD <- 0x%lx\n", i, in.source_memory[s]);
				insert(in.source_memory[s], ins);
				loads++;
				//lookup(0, in.source_memory[s], &ts);
			}
		}

		for (int d=0; d < NUM_INSTR_DESTINATIONS; d++)
		{
			if (in.destination_memory[d])
			{
				//printf("%i:   STORE -> 0x%lx\n", i, in.destination_memory[d]);
				insert(in.destination_memory[d], ins);
				stores++;
				//lookup(0, in.destination_memory[d], &ts);
			}
		}

		if (ins > (ooo_cpu[0].warmup_instructions + ooo_cpu[0].simulation_instructions))
			break;

		ins++;
	}

/*
	// dump the contents
*/

	// rewind the file pointer
	fseek(ooo_cpu[0].trace_file, 0L, SEEK_SET);

	printf("Saw %lu loads and %lu stores\n", loads, stores);
	printf("Mem used: %lu\n", mem_used);

#ifdef LOGGING
	// open the replacement log
	rlog = fopen(rlog_name, "wt");
	fprintf(rlog, "cpu, instr_id, set, way, timestamp, address, ip, type\n");
#endif // LOGGING
}

// find replacement victim
uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set,
	const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
	uint32_t way = 0;
	uint32_t best_way = 0;
	uint64_t timestamp = 0;
	uint64_t best_timestamp = 0;
	uint64_t best_vaddr = 0;

	//printf("Current time: %lu\n", instr_id);
	//printf("Physical address: 0x%lx -> set: 0x%x\n", full_addr, set);

	// check for invalid sets first
	for (way=0; way<NUM_WAY; way++)
	{
		//if (block[set][way].valid == false)
		if (current_set[way].valid == false)
		{
			best_way = way;
			goto done;
		}
	}

	// find the timestamp that is furthest in the future
	for (way=0; way<NUM_WAY; way++)
	{
		uint64_t paddr = current_set[way].full_addr;

		// translate phys to virt
		uint64_t vaddr = pa_to_va(cpu, paddr);

		// if it is not in the database, it is never reused - replace it!
		if (lookup(instr_id, vaddr, &timestamp) < 0)
		{
			best_way = way;
			best_timestamp = instr_id;
			best_vaddr = vaddr;
			printf("[%i] 0x%lx=X\n", way, vaddr);
			break;
		}

		printf("[%i] 0x%lx=%lu\n", way, vaddr, timestamp);

		if (timestamp > best_timestamp)
		{
			best_timestamp = timestamp;
			best_way = way;
			best_vaddr = vaddr;
		}
	}

	printf("Installing 0x%lx in [0x%x][%i]\n", full_addr, set, best_way);
	update(best_vaddr);

done:
#ifdef LOGGING
	// log it
	fprintf(rlog, "%u,0x%lx,%u,0x%x,0x%lx,0x%lx,0x%lx,%u\n",
		cpu, instr_id, set, best_way,
		best_timestamp, full_addr, ip, type);
#endif // LOGGING
	return best_way;
}

// called on every cache hit and cache fill
void CACHE::llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way,
	uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
	//if (hit)
	//	printf("Hit: 0x%lx\n", full_addr);
}

void CACHE::llc_replacement_final_stats()
{
#ifdef LOGGING
	printf("Closing rlog file\n");
	fclose(rlog);
#endif // LOGGING
}
