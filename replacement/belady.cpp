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

// make WiredTiger global state
	WT_SESSION *session;
	char table_name[1024];

// insert a value
static void insert(WT_CURSOR *cursor, uint64_t addr, uint64_t timestamp)
{
	char key[24];
	char value[24];

	snprintf(key, 24, "%lx", addr);
	snprintf(value, 24, "%lu", timestamp);
	//printf("Inserting %s %s\n", key, value);
	cursor->set_key(cursor, key);
	cursor->set_value(cursor, value);
	cursor->insert(cursor);
	cursor->close(cursor);
}

static int lookup(WT_CURSOR *cursor, uint64_t addr, uint64_t *timestamp)
{
	char key[24];
	const char *value;

	snprintf(key, 24, "%lx", addr);
    cursor->set_key(cursor, key);
   int ret = cursor->search(cursor);
	if (ret < 0)
	{
		printf("key %s not found\n", key);
		return -1;
	}
    cursor->get_value(cursor, &value);
	*timestamp = strtoul(value, NULL, 10);
	return 0;
}

// initialize replacement state
void CACHE::llc_initialize_replacement()
{
	input_instr in;
	long unsigned int ins=0;
	long unsigned int loads=0;
	long unsigned int stores=0;

	WT_CONNECTION *conn;
	WT_CURSOR *cursor;

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
				if (session->open_cursor(session, table_name, NULL, NULL, &cursor) == 0)
					insert(cursor, in.source_memory[s], ins);
				else
					printf("Error inserting\n");
				loads++;
/*
				if (session->open_cursor(session, table_name, NULL, NULL, &cursor) == 0)
				{
					uint64_t ts;
					if (lookup(cursor, in.source_memory[s], &ts) == 0)
						printf("Found %lx at %lu\n", in.source_memory[s], ts); 
				}
*/
			}
		}

		for (int d=0; d < NUM_INSTR_DESTINATIONS; d++)
		{
			if (in.destination_memory[d])
			{
				//printf("%i:   STORE -> 0x%lx\n", i, in.destination_memory[d]);
				ret = session->open_cursor(session, table_name, NULL, NULL, &cursor);
				if (ret != 0)
				{
					printf("Error opening cursor during insert\n");
					return;
				}
				insert(cursor, in.destination_memory[d], ins);
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
}

// find replacement victim
uint32_t CACHE::llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set,
	const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
	uint32_t way = 0;
	uint32_t best_way = 0;
	WT_CURSOR *cursor;
	uint64_t timestamp;
	uint64_t best_timestamp = 0;

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
		printf("Full Address=%lx address=%lx\n", block[set][way].full_addr, block[set][way].address);
		uint64_t addr = block[set][way].address;

		// look up the next time to use
		if (session->open_cursor(session, table_name, NULL, NULL, &cursor) != 0)
		{
			printf("Error opening cursor during insert\n");
			return 0;
		}
		printf("Looking up %i = 0x%lx\n", way, addr);
		fflush(stdout);

		// if it is not in the database, it is never reused
		if (lookup(cursor, addr, &timestamp) == -1)
			return way;

		printf("Way %i = 0x%lx @ %lu\n", way, addr, timestamp);
		if (timestamp > best_timestamp)
		{
			best_timestamp = timestamp;
			best_way = way;
		}
	}

	printf("Replacing %i @ %lu\n", best_way, best_timestamp);
	return best_way;
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
