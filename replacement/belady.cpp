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
char rlog_name[] = "output_belady.csv";
FILE *rlog;

// insert a value
static void insert(WT_CURSOR *cursor, uint64_t vaddr, uint64_t timestamp)
{
	//printf("Inserting va 0x%lx %lu\n", vaddr, timestamp);
	cursor->set_key(cursor, timestamp);
	cursor->set_value(cursor, vaddr, timestamp);
	cursor->insert(cursor);
}

static int lookup(WT_CURSOR *cursor, uint64_t cur_time, uint64_t vaddr, uint64_t *timestamp)
{
	int ret;
	int exact;
	uint64_t addr;

	//printf("cur_time: %lu\n", cur_time);
	if (cur_time == 0)
		return -1;

	cursor->set_key(cursor, cur_time);
	ret = cursor->search_near(cursor, &exact);
	if (ret != 0)
	{
		*timestamp = 0;
		return -1;
	}

	while ((ret = cursor->next(cursor)) == 0)
	{
		ret = cursor->get_value(cursor, &addr, timestamp);
		//printf("vaddr: 0x%lx @ %lu\n", vaddr, *timestamp);
		if (addr == vaddr)
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
	snprintf(table_name, 127, "table:%s", "usage");
	int ret = session->create(session, table_name, "key_format=r,value_format=QQ,columns=(id,vaddr,ts)");
	if (ret != 0)
	{
		printf("error creating session for table %s\n", table_name);
		return;
	}
	/* Create an index with a simple key. */
	ret = session->create(session, "index:usage:vaddr", "columns=(vaddr)");

	// create a record of all memory accesses in the whole trace for each CPU
	printf("Skipping %lu warmup instructions\n", ooo_cpu[0].warmup_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		if (ins >= ooo_cpu[0].warmup_instructions)
			break;
		ins++;
	}

	uint64_t cur_time = ooo_cpu[0].warmup_instructions;
	//if (session->open_cursor(session, table_name, NULL, "append", &cursor) != 0)
	if (session->open_cursor(session, table_name, NULL, NULL, &cursor) != 0)
		printf("Error opening cursor\n");
	cursor->set_key(cursor, cur_time);

	printf("Loading %lu simulation instructions\n", ooo_cpu[0].simulation_instructions);
	while (fread(&in, sizeof(in), 1, ooo_cpu[0].trace_file))
	{
		for (int s=0; s < NUM_INSTR_SOURCES; s++)
		{
			if (in.source_memory[s])
			{
				//printf("%i:   LOAD <- 0x%lx\n", i, in.source_memory[s]);
				insert(cursor, in.source_memory[s], ins);
				loads++;
			}
		}

		for (int d=0; d < NUM_INSTR_DESTINATIONS; d++)
		{
			if (in.destination_memory[d])
			{
				//printf("%i:   STORE -> 0x%lx\n", i, in.destination_memory[d]);
				insert(cursor, in.destination_memory[d], ins);
				stores++;
			}
		}

		if (ins > (ooo_cpu[0].warmup_instructions + ooo_cpu[0].simulation_instructions))
			break;

		ins++;
	}
	cursor->close(cursor);

/*
	// dump the contents
	uint64_t vaddr, ts, recno;
	if (session->open_cursor(session, table_name, NULL, NULL, &cursor) != 0)
		printf("Error opening cursor 2\n");
	while ((ret = cursor->next(cursor)) == 0)
	{
		cursor->get_key(cursor, &recno);
		ret = cursor->get_value(cursor, &vaddr, &ts);
		printf("%lu: 0x%lx @ %lu\n", recno, vaddr, ts);
	}
	ret = cursor->close(cursor);
*/

	// rewind the file pointer
	fseek(ooo_cpu[0].trace_file, 0L, SEEK_SET);

	printf("Saw %lu loads and %lu stores\n", loads, stores);

	// open the replacement log
	rlog = fopen(rlog_name, "wt");
	fprintf(rlog, "cpu, instr_id, set, way, timestamp, address, ip, type\n");
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

	// check for invalid sets first
	for (way=0; way<NUM_WAY; way++)
	{
		if (block[set][way].valid == false)
		{
			best_way = way;
			goto done;
		}
	}

	// look up the next time to use
	if (session->open_cursor(session, table_name, NULL, NULL, &cursor) != 0)
	{
		printf("Error opening cursor during insert\n");
		return 0;
	}

	// find the timestamp that is furthest in the future
	for (way=0; way<NUM_WAY; way++)
	{
		uint64_t paddr = block[set][way].full_addr;

		// translate phys to virt
		uint64_t vaddr = pa_to_va(cpu, paddr);


		// if it is not in the database, it is never reused - replace it!
		if (lookup(cursor, instr_id, vaddr, &timestamp) == -1)
		{
			best_way = way;
			//printf("Next use for 0x%lx=never!\n", vaddr);
			break;
		}
		//printf("[%i] 0x%lx=%lu\n", way, vaddr, timestamp);

		if (timestamp > best_timestamp)
		{
			best_timestamp = timestamp;
			best_way = way;
		}
	}
	cursor->close(cursor);

	//printf("Replacing %i @ %lu\n", best_way, best_timestamp);

done:
	// log it
	fprintf(rlog, "%u,0x%lx,%u,0x%x,0x%lx,0x%lx,0x%lx,%u\n",
		cpu, instr_id, set, best_way,
		best_timestamp, full_addr, ip, type);
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
	printf("Closing rlog file\n");
	fclose(rlog);
}
