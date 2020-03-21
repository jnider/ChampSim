/* Implement Belady's Algorithm (optimal) for cache replacement

Belady's algorithm is the optimal cache replacement policy, because it can see
into the future. This is possible, because we have the simulation trace, and
can know exactly what will happen until the end of the trace. This is useful
to measure the upper bound of cache replacement efficiency, by which all other
algorithms can be measured.
*/

#include "cache.h"
#include "ooo_cpu.h"
#include "wiredtiger.h"

// insert a value
static void insert(WT_SESSION *session, char *table, uint64_t addr, uint64_t timestamp)
{
	WT_CURSOR *cursor;
	int ret;
	char key[24];
	char value[24];

	//printf("Inserting 0x%lx %lu\n", addr, timestamp);
	ret = session->open_cursor(session, table, NULL, NULL, &cursor);
	if (ret != 0)
	{
		printf("Error opening cursor during insert\n");
		return;
	}
	snprintf(key, 24, "%lu", addr);
	snprintf(value, 24, "%lu", timestamp);
	cursor->set_key(cursor, key);
	cursor->set_value(cursor, value);
	cursor->insert(cursor);
	cursor->close(cursor);
}

// initialize replacement state
void CACHE::llc_initialize_replacement()
{
	input_instr in;
	long unsigned int ins=0;
	long unsigned int loads=0;
	long unsigned int stores=0;

	WT_CONNECTION *conn;
	WT_SESSION *session;

	char home[] = "/home/joel/projects/ChampSim/db";

	/* Open a connection to the database, creating it if necessary. */
	printf("Opening db at %s\n", home);
	if (wiredtiger_open(home, NULL, "create", &conn) != 0)
	{
		printf("Error opening db\n");
		return;
	}

	printf("Opening session\n");
	if (conn->open_session(conn, NULL, NULL, &session) != 0)
	{
		printf("Error opening session\n");
		return;
	}

	// create a table
	char table_name[1024];
	snprintf(table_name, 127, "table:%s", "gamess");
	int ret = session->create(session, table_name, "key_format=S,value_format=S");
	if (ret != 0)
	{
		printf("error creating session for table %s\n", table_name);
		return;
	}

	// create a record of all memory accesses in the whole trace for each CPU
	printf("Skipping %lu warmup instructions\n", ooo_cpu[0].warmup_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		if (ins >= ooo_cpu[0].warmup_instructions)
			break;
		ins++;
	}

	printf("Loading %lu simulation instructions\n", ooo_cpu[0].simulation_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		for (int s=0; s < NUM_INSTR_SOURCES; s++)
		{
			if (in.source_memory[s])
			{
				//printf("%i:   LOAD <- 0x%lx\n", i, in.source_memory[s]);
				insert(session, table_name, in.source_memory[s], ins);
				loads++;
			}
		}

		for (int d=0; d < NUM_INSTR_DESTINATIONS; d++)
		{
			if (in.destination_memory[d])
			{
				//printf("%i:   STORE -> 0x%lx\n", i, in.destination_memory[d]);
				insert(session, table_name, in.destination_memory[d], ins);
				stores++;
			}
		}

		if (ins > (ooo_cpu[0].warmup_instructions + ooo_cpu[0].simulation_instructions))
			break;

		ins++;
	}

	// rewind the file pointer
	fseek(ooo_cpu[0].trace_file, 0L, SEEK_SET);

	printf("Saw %lu loads and %lu stores\n", loads, stores);
/*
	// dump the map
	printf("Dump\n");
	for (mem_map::iterator iter=memory_accesses.begin(); iter != memory_accesses.end(); iter++)
	{
		printf("0x%lx: ", iter->first);
		access_vector& v = memory_accesses[iter->first];
		for (unsigned int idx=0; idx < v.size(); idx++)
			printf("%lu, ", v[idx]);
		printf("\n");
	}
*/
}

// find replacement victim
uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set,
	const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
	uint32_t way = 0;

	// baseline LRU
	//return lru_victim(cpu, instr_id, set, current_set, ip, full_addr, type); 

	// check for invalid sets first
	for (way=0; way<NUM_WAY; way++)
	{
		if (block[set][way].valid == false)
			return way;
	}

	printf("What are my options:\n");
	for (way=0; way<NUM_WAY; way++)
	{
		printf("Way %i = 0x%lx\n", way, block[set][way].full_addr);
	}
	return 0;
}

// called on every cache hit and cache fill
void CACHE::llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way,
	uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
	string TYPE_NAME;
	if (type == LOAD)
		TYPE_NAME = "LOAD";
	else if (type == RFO)
		TYPE_NAME = "RFO";
	else if (type == PREFETCH)
		TYPE_NAME = "PF";
	else if (type == WRITEBACK)
		TYPE_NAME = "WB";
	else
	assert(0);

	if (hit)
		TYPE_NAME += "_HIT";
	else
		TYPE_NAME += "_MISS";

	if ((type == WRITEBACK) && ip)
		assert(0);

    // uncomment this line to see the LLC accesses
    // cout << "CPU: " << cpu << "  LLC " << setw(9) << TYPE_NAME << " set: " << setw(5) << set << " way: " << setw(2) << way;
    // cout << hex << " paddr: " << setw(12) << paddr << " ip: " << setw(8) << ip << " victim_addr: " << victim_addr << dec << endl;

	// baseline LRU
	if (hit && (type == WRITEBACK)) // writeback hit does not update LRU state
		return;

	return lru_update(set, way);
}

void CACHE::llc_replacement_final_stats()
{
}
