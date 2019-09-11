/**
 * (C) Copyright 2016-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(tests)

#include <getopt.h>

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>

#define USE_TIME_PROFILING
#include "benchmark_util.h"

/*
 * These are only at the top of the file for reference / easy changing
 * Do not use these anywhere except in the main function where arguments are
 * parsed!
 */
#define DEFAULT_NUM_DOMAINS             8
#define DEFAULT_NODES_PER_DOMAIN        1
#define DEFAULT_VOS_PER_TARGET          4

#define BENCHMARK_STEPS 100
#define BENCHMARK_COUNT_PER_STEP 10000
#define BENCHMARK_COUNT (BENCHMARK_STEPS * BENCHMARK_COUNT_PER_STEP)

static void
print_usage(const char *prog_name, const char *const ops[], uint32_t num_ops)
{
	printf("Usage: %s --operation <op> [optional arguments] -- [operation specific arguments]\n"
	       "\n"
	       "Required Arguments\n"
	       "  --operation <op>\n"
	       "      Short version: -o\n"
	       "      The operation to invoke\n"
	       "      Possible values:\n", prog_name);

	for (; num_ops > 0; num_ops--)
		printf("          %s\n", ops[num_ops - 1]);

	printf("\n"
	       "Optional Arguments\n"
	       "  --num-domains <num>\n"
	       "      Short version: -d\n"
	       "      Number of domains (i.e. racks) at the highest level of the pool map\n"
	       "\n"
	       "      Default: %u\n"
	       "\n"
	       "  --nodes-per-domain <num>\n"
	       "      Short version: -n\n"
	       "      Number of nodes contained under each top-level domain\n"
	       "\n"
	       "      Default: %u\n"
	       "\n"
	       "  --vos-per-target <num>\n"
	       "      Short version: -v\n"
	       "      Number of VOS containers per target\n"
	       "\n"
	       "      Default: %u\n",
	       DEFAULT_NUM_DOMAINS, DEFAULT_NODES_PER_DOMAIN,
	       DEFAULT_VOS_PER_TARGET);
}

static bool g_pl_debug_msg;

typedef void (*test_op_t)(int argc, char **argv, uint32_t num_domains,
			  uint32_t nodes_per_domain, uint32_t vos_per_target);

static void
plt_obj_place(struct pl_map *pl_map, daos_obj_id_t oid,
	      struct pl_obj_layout **layout)
{
	struct daos_obj_md       md;
	int                      i;
	int                      rc;

	memset(&md, 0, sizeof(md));
	md.omd_id  = oid;
	md.omd_ver = 1;

	rc = pl_obj_place(pl_map, &md, NULL, layout);
	D_ASSERT(rc == 0);

	D_PRINT("Layout of object "DF_OID"\n", DP_OID(oid));
	for (i = 0; i < (*layout)->ol_nr; i++)
		D_PRINT("%d ", (*layout)->ol_shards[i].po_target);

	D_PRINT("\n");
}

static void
plt_obj_layout_check(struct pl_obj_layout *layout)
{
	int i;

	for (i = 0; i < layout->ol_nr; i++)
		D_ASSERT(layout->ol_shards[i].po_target != -1);
}

static bool
pt_obj_layout_match(int num_domains, struct pl_obj_layout *lo_1,
		    struct pl_obj_layout *lo_2)
{
	int i;

	D_ASSERT(lo_1->ol_nr == lo_2->ol_nr);
	D_ASSERT(lo_1->ol_nr > 0 && lo_1->ol_nr <= num_domains);

	for (i = 0; i < lo_1->ol_nr; i++) {
		if (lo_1->ol_shards[i].po_target !=
		    lo_2->ol_shards[i].po_target)
			return false;
	}

	return true;
}

static void
plt_set_tgt_status(struct pool_map *po_map, uint32_t id, int status,
		   uint32_t ver)
{
	struct pool_target	*target;
	char			*str;
	int			 rc;

	switch (status) {
	case PO_COMP_ST_UP:
		str = "PO_COMP_ST_UP";
		break;
	case PO_COMP_ST_UPIN:
		str = "PO_COMP_ST_UPIN";
		break;
	case PO_COMP_ST_DOWN:
		str = "PO_COMP_ST_DOWN";
		break;
	case PO_COMP_ST_DOWNOUT:
		str = "PO_COMP_ST_DOWNOUT";
		break;
	default:
		str = "unknown";
		break;
	};

	rc = pool_map_find_target(po_map, id, &target);
	D_ASSERT(rc == 1);
	if (g_pl_debug_msg)
		D_PRINT("set target id %d, rank %d as %s, ver %d.\n",
			id, target->ta_comp.co_rank, str, ver);
	target->ta_comp.co_status = status;
	target->ta_comp.co_fseq = ver;
	rc = pool_map_set_version(po_map, ver);
	D_ASSERT(rc == 0);
}

static void
plt_fail_tgt(struct pool_map *po_map, uint32_t *po_ver, uint32_t id)
{
	(*po_ver)++;
	plt_set_tgt_status(po_map, id, PO_COMP_ST_DOWN, *po_ver);
}

static void
plt_add_tgt(struct pool_map *po_map, uint32_t *po_ver, uint32_t id)
{
	(*po_ver)++;
	plt_set_tgt_status(po_map, id, PO_COMP_ST_UPIN, *po_ver);
}

static void
plt_spare_tgts_get(struct pool_map *po_map, struct pl_map *pl_map,
		   uint32_t *po_ver, uuid_t pl_uuid, daos_obj_id_t oid,
		   uint32_t spare_max_num, uint32_t *failed_tgts,
		   int failed_cnt, uint32_t *spare_tgt_ranks,
		   uint32_t *shard_ids, uint32_t *spare_cnt)
{
	struct daos_obj_md      md = { 0 };
	int                     i;
	int                     rc;

	for (i = 0; i < failed_cnt; i++)
		plt_fail_tgt(po_map, po_ver, failed_tgts[i]);

	rc = pl_map_update(pl_uuid, po_map, false, PL_TYPE_RING);
	D_ASSERT(rc == 0);
	pl_map = pl_map_find(pl_uuid, oid);
	D_ASSERT(pl_map != NULL);
	dc_obj_fetch_md(oid, &md);
	md.omd_ver = *po_ver;
	*spare_cnt = pl_obj_find_rebuild(pl_map, &md, NULL, *po_ver,
					 spare_tgt_ranks, shard_ids,
					 spare_max_num, -1);
	D_PRINT("spare_cnt %d for version %d -\n", *spare_cnt, *po_ver);
	for (i = 0; i < *spare_cnt; i++)
		D_PRINT("shard %d, spare target rank %d\n",
			shard_ids[i], spare_tgt_ranks[i]);

	pl_map_decref(pl_map);

	for (i = 0; i < failed_cnt; i++)
		plt_add_tgt(po_map, po_ver, failed_tgts[i]);
}

static void
gen_pool_and_placement_map(int num_domains, int nodes_per_domain,
                           int vos_per_target, pl_map_type_t pl_type,
			   struct pool_map **po_map_out,
                           struct pl_map **pl_map_out)
{
	struct pool_buf		*buf;
	int			 i;
	struct pl_map_init_attr	 mia;
	int			 nr;
	struct pool_component   *comps;
	struct pool_component	*comp;
	int			 rc;

	nr = num_domains + (nodes_per_domain * num_domains) +
	     (num_domains * nodes_per_domain * vos_per_target);
	D_ALLOC_ARRAY(comps, nr);
	D_ASSERT(comps != NULL);

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < num_domains; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RACK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= nodes_per_domain;
	}

	for (i = 0; i < num_domains * nodes_per_domain; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= vos_per_target;
	}

	for (i = 0; i < num_domains * nodes_per_domain * vos_per_target;
	     i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= 1;
	}

	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	D_ASSERT(rc == 0);

	rc = pool_map_create(buf, 1, po_map_out);
	D_ASSERT(rc == 0);

	if (g_pl_debug_msg)
		pool_map_print(*po_map_out);

	mia.ia_type	    = pl_type;
	mia.ia_ring.ring_nr = 1;
	mia.ia_ring.domain  = PO_COMP_TP_RACK;

	rc = pl_map_create(*po_map_out, &mia, pl_map_out);
	D_ASSERT(rc == 0);
}

static void
free_pool_and_placement_map(struct pool_map *po_map_in,
                            struct pl_map *pl_map_in)
{
	struct pool_buf *buf;

	pool_buf_extract(po_map_in, &buf);
	pool_map_decref(po_map_in);
	pool_buf_free(buf);

	pl_map_decref(pl_map_in);
}

static void
ring_placement_test(int argc, char **argv, uint32_t num_domains,
		    uint32_t nodes_per_domain, uint32_t vos_per_target)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;
	uint32_t		 po_ver = 1;
	int			 i;
	struct pl_obj_layout	*lo_1;
	struct pl_obj_layout	*lo_2;
	struct pl_obj_layout	*lo_3;
	uuid_t			 pl_uuid;
	daos_obj_id_t		 oid;
	uint32_t		 spare_max_num = num_domains * 3;

	uint32_t		 spare_tgt_candidate[spare_max_num];
	uint32_t		 spare_tgt_ranks[spare_max_num];
	uint32_t		 shard_ids[spare_max_num];
	uint32_t		 failed_tgts[spare_max_num];
	unsigned int		 spare_cnt;

	uuid_generate(pl_uuid);
	srand(time(NULL));
	oid.lo = rand();
	oid.hi = 5;

	/* Create reference pool/placement map */
	gen_pool_and_placement_map(num_domains, nodes_per_domain,
	                           vos_per_target, PL_TYPE_RING,
	                           &po_map, &pl_map);
	D_ASSERT(po_map != NULL);
	D_ASSERT(pl_map != NULL);
	pl_map_print(pl_map);

	/* initial placement when all nodes alive */
	daos_obj_generate_id(&oid, 0, OC_RP_4G2, 0);
	D_PRINT("\ntest initial placement when no failed shard ...\n");
	plt_obj_place(pl_map, oid, &lo_1);
	plt_obj_layout_check(lo_1);

	/* test plt_obj_place when some/all shards failed */
	D_PRINT("\ntest to fail all shards  and new placement ...\n");
	for (i = 0; i < spare_max_num && i < lo_1->ol_nr; i++)
		plt_fail_tgt(po_map, &po_ver, lo_1->ol_shards[i].po_target);
	plt_obj_place(pl_map, oid, &lo_2);
	plt_obj_layout_check(lo_2);
	D_ASSERT(!pt_obj_layout_match(num_domains, lo_1, lo_2));
	D_PRINT("spare target candidate:");
	for (i = 0; i < spare_max_num && i < lo_1->ol_nr; i++) {
		spare_tgt_candidate[i] = lo_2->ol_shards[i].po_target;
		D_PRINT(" %d", spare_tgt_candidate[i]);
	}
	D_PRINT("\n");

	D_PRINT("\ntest to add back all failed shards and new placement ...\n");
	for (i = 0; i < spare_max_num && i < lo_1->ol_nr; i++)
		plt_add_tgt(po_map, &po_ver, lo_1->ol_shards[i].po_target);
	plt_obj_place(pl_map, oid, &lo_3);
	plt_obj_layout_check(lo_3);
	D_ASSERT(pt_obj_layout_match(num_domains, lo_1, lo_3));

	/* test pl_obj_find_rebuild */
	D_PRINT("\ntest pl_obj_find_rebuild to get correct spare tagets ...\n");
	failed_tgts[0] = lo_3->ol_shards[0].po_target;
	failed_tgts[1] = lo_3->ol_shards[1].po_target;
	D_PRINT("failed target %d[0], %d[1], expected spare %d %d\n",
		failed_tgts[0], failed_tgts[1], spare_tgt_candidate[0],
		spare_tgt_candidate[1]);
	plt_spare_tgts_get(po_map, pl_map, &po_ver, pl_uuid, oid, spare_max_num,
			   failed_tgts, 2, spare_tgt_ranks, shard_ids,
			   &spare_cnt);
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 0);
	D_ASSERT(shard_ids[1] == 1);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[0]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[1]);

	/* fail the to-be-spare target and select correct next spare */
	failed_tgts[0] = lo_3->ol_shards[1].po_target;
	failed_tgts[1] = spare_tgt_candidate[0];
	failed_tgts[2] = lo_3->ol_shards[0].po_target;
	D_PRINT("\nfailed targets %d[1] %d %d[0], expected spare %d[0] %d[1]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2],
		spare_tgt_candidate[2], spare_tgt_candidate[1]);
	plt_spare_tgts_get(po_map, pl_map, &po_ver, pl_uuid, oid, spare_max_num,
			   failed_tgts, 3, spare_tgt_ranks, shard_ids,
			   &spare_cnt);
	/* should get next spare targets, the first spare candidate failed,
	 * and shard[0].fseq > shard[1].fseq, so will select shard[1]'s
	 * next spare first.
	 */
	D_ASSERT(spare_cnt == 2);
	D_ASSERT(shard_ids[0] == 1);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[1]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[2]);

	failed_tgts[0] = spare_tgt_candidate[0];
	failed_tgts[1] = spare_tgt_candidate[1];
	failed_tgts[2] = lo_3->ol_shards[3].po_target;
	failed_tgts[3] = lo_3->ol_shards[0].po_target;
	failed_tgts[4] = lo_3->ol_shards[1].po_target;
	D_PRINT("\nfailed targets %d %d %d[3] %d[0] %d[1], "
		"expected spare %d[0] %d[1] %d[3]\n",
		failed_tgts[0], failed_tgts[1], failed_tgts[2], failed_tgts[3],
		failed_tgts[4], spare_tgt_candidate[3], spare_tgt_candidate[4],
		spare_tgt_candidate[2]);
	plt_spare_tgts_get(po_map, pl_map, &po_ver, pl_uuid, oid, spare_max_num,
			   failed_tgts, 5, spare_tgt_ranks, shard_ids,
			   &spare_cnt);
	D_ASSERT(spare_cnt == 3);
	D_ASSERT(shard_ids[0] == 3);
	D_ASSERT(shard_ids[1] == 0);
	D_ASSERT(shard_ids[2] == 1);
	D_ASSERT(spare_tgt_ranks[0] == spare_tgt_candidate[2]);
	D_ASSERT(spare_tgt_ranks[1] == spare_tgt_candidate[3]);
	D_ASSERT(spare_tgt_ranks[2] == spare_tgt_candidate[4]);


	pl_obj_layout_free(lo_1);
	pl_obj_layout_free(lo_2);
	pl_obj_layout_free(lo_3);

	free_pool_and_placement_map(po_map, pl_map);
	po_map = NULL;
	pl_map = NULL;

	D_PRINT("\nRing placement tests passed!\n");
}

static void
benchmark_placement_usage() {
	printf("Placement benchmark usage: -- --map-type <type>\n"
	       "\n"
	       "Required Arguments\n"
	       "  --map-type <type>\n"
	       "      Short version: -m\n"
	       "      The map type to use\n"
	       "      Possible values:\n"
	       "          PL_TYPE_RING\n"
	       "          PL_TYPE_JUMP_MAP\n"
	       "\n"
	       "Optional Arguments\n"
	       "  --vtune-loop\n"
	       "      Short version: -t\n"
	       "      If specified, runs a tight loop on placement for analysis with VTune\n");
}

static void
benchmark_placement(int argc, char **argv, uint32_t num_domains,
                    uint32_t nodes_per_domain, uint32_t vos_per_target)
{
	struct pool_map *pool_map;
	struct pl_map *pl_map;
	struct daos_obj_md *obj_table;
	int i;
	struct pl_obj_layout **layout_table;

	pl_map_type_t map_type = PL_TYPE_UNKNOWN;
	int vtune_loop = 0;

	while (1) {
		static struct option long_options[] = {
			{"map-type", required_argument, 0, 'm'},
			{"vtune-loop", no_argument, 0, 't'},
			{0, 0, 0, 0}
		};
		int c;

		c = getopt_long(argc, argv, "m:t", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'm':
			if (strncmp(optarg, "PL_TYPE_RING", 12) == 0) {
				map_type = PL_TYPE_RING;
			} else if (strncmp(optarg, "PL_TYPE_JUMP_MAP", 15)
			           == 0) {
				map_type = PL_TYPE_JUMP_MAP;
			} else {
				printf("ERROR: Unknown map-type '%s'\n",
				       optarg);
				benchmark_placement_usage();
				return;
			}
			break;
		case 't':
			vtune_loop = 1;
			break;
		case '?':
		default:
			printf("ERROR: Unrecognized argument '%s'\n", optarg);
			benchmark_placement_usage();
			return;
		}
	}
	if (map_type == PL_TYPE_UNKNOWN) {
		printf("ERROR: --map-type must be specified!\n");
		benchmark_placement_usage();
		return;
	}

	/* Create reference pool/placement map */
	gen_pool_and_placement_map(num_domains, nodes_per_domain,
	                           vos_per_target, map_type,
	                           &pool_map, &pl_map);
	D_ASSERT(pool_map != NULL);
	D_ASSERT(pl_map != NULL);

	/* Generate list of OIDs to look up */
	D_ALLOC_ARRAY(obj_table, BENCHMARK_COUNT);
	D_ASSERT(obj_table != NULL);

	/* Storage for returned layout data */
	D_ALLOC_ARRAY(layout_table, BENCHMARK_COUNT);
	D_ASSERT(layout_table != NULL);

	for (i = 0; i < BENCHMARK_COUNT; i++) {
		memset(&obj_table[i], 0, sizeof(obj_table[i]));
		obj_table[i].omd_id.lo = rand();
		obj_table[i].omd_id.hi = 5;
		daos_obj_generate_id(&obj_table[i].omd_id, 0, OC_RP_4G2, 0);
		obj_table[i].omd_ver = 1;
	}

	/* Warm up the cache */
	for (i = 0; i < BENCHMARK_COUNT; i++)
		pl_obj_place(pl_map, &obj_table[i], NULL, &layout_table[i]);

	if (vtune_loop) {
		D_PRINT("Starting vtune loop!\n");
		while (1)
			for (i = 0; i < BENCHMARK_COUNT; i++)
				pl_obj_place(pl_map, &obj_table[i], NULL,
				             &layout_table[i]);
	}

	/* Simple layout calculation benchmark */
	{
		BENCHMARK_START(1);
		for (i = 0; i < BENCHMARK_COUNT; i++)
			pl_obj_place(pl_map, &obj_table[i], NULL,
				     &layout_table[i]);
		BENCHMARK_STOP(wallclock_delta_ns, thread_delta_ns);
		D_PRINT("\nPlacement benchmark results:\n");
		D_PRINT("# Iterations, Wallclock time delta (ns), thread time "
			" delta (ns), Wallclock placements per second\n");
		D_PRINT("%d,%lld,%lld,%lld\n", BENCHMARK_COUNT,
			wallclock_delta_ns, thread_delta_ns,
			NANOSECONDS_PER_SECOND * BENCHMARK_COUNT /
				wallclock_delta_ns);
	}

	free_pool_and_placement_map(pool_map, pl_map);
}

#define ADDITION_DEFAULT_NUM_TO_ADD 32
#define ADDITION_DEFAULT_TEST_ENTRIES 100000
void
benchmark_addition_data_movement_usage() {
	printf("Addition data movement benchmark usage: -- --map-type <type1,type2,...> [optional arguments]\n"
	       "\n"
	       "Required Arguments\n"
	       "  --map-type <type1,type2,...>\n"
	       "      Short version: -m\n"
	       "      A comma delimited list of map types to test\n"
	       "      Possible values:\n"
	       "          PL_TYPE_RING\n"
	       "          PL_TYPE_JUMP_MAP\n"
	       "\n"
	       "Optional Arguments\n"
	       "  --num-domains-to-add <num>\n"
	       "      Short version: -a\n"
	       "      Number of top-level domains to add\n"
	       "      Default: %d\n"
	       "\n"
	       "  --num-test-entries <num>\n"
	       "      Short version: -t\n"
	       "      Number of objects to test placing each iteration\n"
	       "      Default: %d\n"
	       "\n"
	       "  --use-x11\n"
	       "      Short version: -x\n"
	       "      Display the resulting graph using x11 instead of the default console\n"
	       "\n",
	       ADDITION_DEFAULT_NUM_TO_ADD, ADDITION_DEFAULT_TEST_ENTRIES);
}

void
benchmark_addition_data_movement(int argc, char **argv, uint32_t num_domains,
				 uint32_t nodes_per_domain,
				 uint32_t vos_per_target)
{
	struct pool_map *initial_pool_map;
	struct pl_map *initial_pl_map;
	struct daos_obj_md *obj_table;
	struct pl_obj_layout **initial_layout;
	struct pl_obj_layout **iter_layout;
	int obj_idx;
	int type_idx;
	int added;
	int j;
	char *token;
	double *percent_moved;

	/*
	 * This is the total number of requested map types from the user
	 * It is always +1 more than the user requested - and that last
	 * index is the "ideal" amount of moved data
	 */
	int num_map_types = 0;
	pl_map_type_t *map_types = NULL;
	const char **map_keys = NULL;
	int domains_to_add = ADDITION_DEFAULT_NUM_TO_ADD;
	int test_entries = ADDITION_DEFAULT_TEST_ENTRIES;
	bool use_x11 = false;

	D_PRINT("\n\n");
	D_PRINT("Addition test starting...\n");

	while (1) {
		static struct option long_options[] = {
			{"map-type", required_argument, 0, 'm'},
			{"num-domains-to-add", required_argument, 0, 'a'},
			{"num-test-entries", required_argument, 0, 't'},
			{"use-x11", no_argument, 0, 'x'},
			{0, 0, 0, 0}
		};
		int c;
		int ret;

		c = getopt_long(argc, argv, "m:a:t:x", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'm':
			/* Figure out how many types there are */
			j = 0;
			num_map_types = 1;
			while (optarg[j] != '\0') {
				if (optarg[j] == ',')
					num_map_types++;
				j++;
			}
			/* Pad +1 for "ideal" */
			num_map_types++;

			D_ALLOC_ARRAY(map_types, num_map_types);
			D_ASSERT(map_types != NULL);

			D_ALLOC_ARRAY(map_keys, num_map_types);
			D_ASSERT(map_keys != NULL);

			/* Ideal */
			map_keys[num_map_types - 1] = "Ideal";

			/* Populate the types array */
			num_map_types = 0;
			token = strtok(optarg, ",");
			while (token != NULL) {
				if (strncmp(token, "PL_TYPE_RING", 12) == 0) {
					map_types[num_map_types] = PL_TYPE_RING;
					map_keys[num_map_types] =
						"PL_TYPE_RING";
				} else if (strncmp(token, "PL_TYPE_JUMP_MAP", 15)
					   == 0) {
					map_types[num_map_types] =
						PL_TYPE_JUMP_MAP;
					map_keys[num_map_types] =
						"PL_TYPE_JUMP_MAP";
				} else {
					printf("ERROR: Unknown map-type '%s'\n",
					       token);
					benchmark_addition_data_movement_usage();
					return;
				}
				num_map_types++;
				token = strtok(NULL, ",");
			}
			/* Pad +1 for "ideal" */
			num_map_types++;

			break;
		case 'a':
			ret = sscanf(optarg, "%d", &domains_to_add);
			if (ret != 1 || domains_to_add <= 0) {
				printf("ERROR: Invalid num-domains-to-add\n");
				benchmark_addition_data_movement_usage();
				return;
			}
			break;
		case 't':
			ret = sscanf(optarg, "%d", &test_entries);
			if (ret != 1 || test_entries <= 0) {
				printf("ERROR: Invalid num-test-entries\n");
				benchmark_addition_data_movement_usage();
				return;
			}
			break;
		case 'x':
			use_x11 = true;
			break;
		case '?':
		default:
			printf("ERROR: Unrecognized argument '%s'\n", optarg);
			benchmark_addition_data_movement_usage();
			return;
		}
	}

	if (num_map_types == 0) {
		printf("ERROR: --map-type must be specified!\n");
		benchmark_addition_data_movement_usage();
		return;
	}

	/* Generate list of OIDs to look up */
	D_ALLOC_ARRAY(obj_table, test_entries);
	D_ASSERT(obj_table != NULL);

	for (obj_idx = 0; obj_idx < test_entries; obj_idx++) {
		memset(&obj_table[obj_idx], 0, sizeof(obj_table[obj_idx]));
		obj_table[obj_idx].omd_id.lo = rand();
		obj_table[obj_idx].omd_id.hi = 5;
		daos_obj_generate_id(&obj_table[obj_idx].omd_id, 0,
				     OC_RP_4G2, 0);
		obj_table[obj_idx].omd_ver = 1;
	}

	/* Allocate space for layouts */
	/* Initial layout - without changes to the map */
	D_ALLOC_ARRAY(initial_layout, test_entries);
	D_ASSERT(initial_layout != NULL);
	/* Per-iteration layout to diff against others */
	D_ALLOC_ARRAY(iter_layout, test_entries);
	D_ASSERT(iter_layout != NULL);

	/*
	 * Allocate space for results data
	 * This is a flat 2D array of results!
	 */
	D_ALLOC_ARRAY(percent_moved, num_map_types * (domains_to_add + 1));
	D_ASSERT(percent_moved != NULL);

	/* Measure movement for all but ideal case */
	for (type_idx = 0; type_idx < num_map_types - 1; type_idx++) {
		/* Create initial reference pool/placement map */
		gen_pool_and_placement_map(num_domains, nodes_per_domain,
					   vos_per_target, map_types[type_idx],
					   &initial_pool_map, &initial_pl_map);
		D_ASSERT(initial_pool_map != NULL);
		D_ASSERT(initial_pl_map != NULL);

		/* Initial placement */
		for (obj_idx = 0; obj_idx < test_entries; obj_idx++)
			pl_obj_place(initial_pl_map, &obj_table[obj_idx], NULL,
				     &initial_layout[obj_idx]);

		for (added = 0; added <= domains_to_add; added++) {
			struct pool_map *iter_pool_map;
			struct pl_map *iter_pl_map;
			int num_moved_all_at_once = 0;

			/*
			 * Generate a new pool/placement map combination for
			 * this new configuration
			 */
			gen_pool_and_placement_map(num_domains + added,
						   nodes_per_domain,
						   vos_per_target,
						   map_types[type_idx],
						   &iter_pool_map,
						   &iter_pl_map);
			D_ASSERT(iter_pool_map != NULL);
			D_ASSERT(iter_pl_map != NULL);

			/* Calculate new placement using this configuration */
			for (obj_idx = 0; obj_idx < test_entries; obj_idx++)
				pl_obj_place(iter_pl_map, &obj_table[obj_idx],
					     NULL, &iter_layout[obj_idx]);

			/* Compute the number of objects that moved */
			for (obj_idx = 0; obj_idx < test_entries; obj_idx++) {
				for (j = 0; j < iter_layout[obj_idx]->ol_nr; j++) {
					if (iter_layout[obj_idx]->ol_shards[j].po_target !=
					    initial_layout[obj_idx]->ol_shards[j].po_target)
						num_moved_all_at_once++;
				}
			}

			percent_moved[type_idx * (domains_to_add + 1) + added] =
				(double)num_moved_all_at_once /
				((double)test_entries * iter_layout[0]->ol_nr);

			free_pool_and_placement_map(iter_pool_map, iter_pl_map);
		}

		free_pool_and_placement_map(initial_pool_map, initial_pl_map);
	}

	/* Calculate the "ideal" data movement */
	for (added = 0; added <= domains_to_add; added++) {
		type_idx = num_map_types - 1;
		percent_moved[type_idx * (domains_to_add + 1) + added] =
			(double)added * 1 * nodes_per_domain /
			(1 * nodes_per_domain * num_domains +
			 added * 1 * nodes_per_domain);
	}

	/* Print out the data */
	for (type_idx = 0; type_idx < num_map_types; type_idx++) {
		D_PRINT("Addition Data: Type %d\n", type_idx);
		for (added = 0; added <= domains_to_add; added++) {
			D_PRINT("%f\n",
				percent_moved[type_idx * (domains_to_add + 1)
						       + added]);
		}
	}
	D_PRINT("\n");

	BENCHMARK_GRAPH((double *)percent_moved, map_keys, num_map_types,
			domains_to_add + 1, "Number of added racks",
			"% Data Moved", 1.0,
			"Data movement \% when adding racks", "/tmp/gnufifo",
			use_x11);
}


int
main(int argc, char **argv)
{
	uint32_t		 num_domains = DEFAULT_NUM_DOMAINS;
	uint32_t		 nodes_per_domain = DEFAULT_NODES_PER_DOMAIN;
	uint32_t		 vos_per_target = DEFAULT_VOS_PER_TARGET;

	int			 rc;
	int			 i;

	// Backwards compatibility - ring unit test is the default
	test_op_t operation = ring_placement_test;

	test_op_t op_fn[] = {
		ring_placement_test,
		benchmark_placement,
		benchmark_addition_data_movement,
	};
	const char *const op_names[] = {
		"ring-placement-test",
		"benchmark-placement",
		"benchmark-add",
	};
	D_ASSERT(ARRAY_SIZE(op_fn) == ARRAY_SIZE(op_names));

	while (1) {
		static struct option long_options[] = {
			{"operation", required_argument, 0, 'o'},
			{"num-domains", required_argument, 0, 'd'},
			{"nodes-per-domain", required_argument, 0, 'n'},
			{"vos-per-target", required_argument, 0, 'v'},
			{0, 0, 0, 0}
		};
		int c;
		int ret;

		c = getopt_long(argc, argv, "o:d:n:v:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'd':
			ret = sscanf(optarg, "%u", &num_domains);
			if (ret != 1) {
				num_domains = DEFAULT_NUM_DOMAINS;
				printf("Warning: Invalid num-domains\n"
				       "  Using default value %u instead\n",
				       num_domains);
			}
			break;
		case 'n':
			ret = sscanf(optarg, "%u", &nodes_per_domain);
			if (ret != 1) {
				nodes_per_domain = DEFAULT_NODES_PER_DOMAIN;
				printf("Warning: Invalid nodes-per-domain\n"
				       "  Using default value %u instead\n",
				       nodes_per_domain);
			}
			break;
		case 'v':
			ret = sscanf(optarg, "%u", &vos_per_target);
			if (ret != 1) {
				vos_per_target = DEFAULT_VOS_PER_TARGET;
				printf("Warning: Invalid vos-per-target\n"
				       "  Using default value %u instead\n",
				       vos_per_target);
			}
			break;
		case 'o':
			for (i = 0; i < ARRAY_SIZE(op_fn); i++) {
				if (strncmp(optarg, op_names[i],
				            strlen(op_names[i])) == 0) {
					operation = op_fn[i];
					break;
				}
			}
			if (i == ARRAY_SIZE(op_fn)) {
				printf("ERROR: Unknown operation '%s'\n",
				       optarg);
				print_usage(argv[0], op_names,
				            ARRAY_SIZE(op_names));
				return -1;
			}
			break;
		case '?':
		default:
			print_usage(argv[0], op_names, ARRAY_SIZE(op_names));
			return -1;
		}
	}

	if (operation == NULL) {
		printf("ERROR: operation argument is required!\n");

		print_usage(argv[0], op_names, ARRAY_SIZE(op_names));
		return -1;
	}

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	operation(argc, argv, num_domains, nodes_per_domain, vos_per_target);

	daos_debug_fini();

	return 0;
}
