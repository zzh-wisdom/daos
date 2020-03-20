#include <stdio.h>
#include <argp.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/placement.h>

#define	STRING_LEN		512
#define	KEY_STR_LEN		100
#define	GROUP_STR_LEN		32

struct io_args {
	char akey_str[KEY_STR_LEN];
	char dkey_str[KEY_STR_LEN];
	char string[STRING_LEN];
	char *obj_type;
	char *iod_type;
	char *fault;
	char *fault_type;
	char *cont_uuid;
	int  recx_idx;
	int  req_len;
	int  num_inc;
	int  duplicate;
};

struct io_context {
	bool		verbose;

	uid_t uid;
	gid_t gid;

	/** pool */
	uuid_t pool_uuid;
	daos_handle_t poh;
	daos_pool_info_t pool_info;
	daos_size_t pool_size;
	uint32_t mode;
	d_rank_list_t svc;
	d_rank_t svc_ranks[5];

	char group[GROUP_STR_LEN];

	/** container */
	uuid_t cont_uuid;
	daos_handle_t coh;

	/** for I/O */
	daos_key_t	dkey;
	daos_iod_t	iod;
	daos_recx_t	recx;
	d_sg_list_t	sgl;
	int		iod_type;
	daos_obj_id_t	oid;
	daos_handle_t	oh;

	/** Fault injection */
	bool		fault;
	d_rank_t	rank_to_fault;
	/** DAOS_CHECKSUM_FAULT_DISK, DAOS_CHECKSUM_FAULT_NETWORK  */
	int		fault_type;
};

static void
print_pkey(struct io_context *ctx, struct io_args *args)
{
	char obj_class_name[100];
	daos_oclass_id2name(daos_obj_id2class(ctx->oid), obj_class_name);

	if (ctx->iod.iod_type == DAOS_IOD_SINGLE) {
		printf("object %lu (class: %s), dkey: '%s',"
		       " akey: '%s', type: SINGLE, size: %lu\n",
		       ctx->oid.lo,
		       obj_class_name,
		       args->dkey_str, args->akey_str,
		       ctx->iod.iod_size
		);

	} else if (ctx->iod.iod_type == DAOS_IOD_ARRAY) {
		printf("object %lu (class: %s), dkey: '%s',"
		       " akey: '%s', type: ARRAY, idx: %lu, len: %lu\n",
		       ctx->oid.lo,
		       obj_class_name,
		       args->dkey_str, args->akey_str,
		       ctx->iod.iod_recxs[0].rx_idx,
		       ctx->iod.iod_recxs[0].rx_nr
		);
	}
}

static daos_iod_type_t
parse_iod_type(char *type)
{
	if (strcmp("ARRAY", type) == 0)
		return DAOS_IOD_ARRAY;
	return DAOS_IOD_SINGLE;
}


static void
create_oid(daos_obj_id_t *oid, int obj_class) {
	(*oid).lo	= 1;
	(*oid).hi	= 0;
	daos_obj_generate_id(oid, 0, obj_class, 0);

}

static void
iov_alloc(d_iov_t *iov, size_t len)
{
	iov->iov_buf = calloc(len, 1);
	iov->iov_buf_len = iov->iov_len = len;
}

static void
iov_alloc_str(d_iov_t *iov, const char *str)
{
	iov_alloc(iov, strlen(str) + 1);
	strcpy(iov->iov_buf, str);
}

static void
print_uuid(uuid_t uuid)
{
	char uuid_str[100];
	uuid_unparse(uuid, uuid_str);
	printf("%s", uuid_str);
}

static int
ctx_connect_pool(struct io_context *ctx) {
	daos_size_t pool_nr = 1;
	daos_mgmt_pool_info_t pool = {0};
	int rc = 0;
	int i;

	rc = daos_mgmt_list_pools(ctx->group, &pool_nr, &pool, NULL);
	if (rc != 0) {
		printf("List Pools failed: %d", rc);
		return rc;
	}

	if (pool_nr == 0) {
		printf("ERROR: No Pool\n");
		return -1;
	} else {
		uuid_copy((*ctx).pool_uuid, pool.mgpi_uuid);
		ctx->svc.rl_nr = pool.mgpi_svc->rl_nr;
		for (i = 0; i < pool.mgpi_svc->rl_nr; i++) {
			ctx->svc_ranks[i] = pool.mgpi_svc->rl_ranks[i];
		}
	}

	rc = daos_pool_connect(ctx->pool_uuid, ctx->group,
			       &ctx->svc, DAOS_PC_RW,
			       &ctx->poh, &ctx->pool_info, NULL);
	if (rc != 0) {
		printf("Pool Connect Failed: %d\n", rc);
		return rc;
	}

	if (ctx->verbose) {
		printf("Using pool: ");
		print_uuid(ctx->pool_uuid);
		printf("\n");
	}

	return rc;
}

static int
ctx_connect_cont(struct io_context *ctx, char *uuid)
{
	int				rc;
	struct daos_pool_cont_info	cont_info[10] = {0};
	daos_size_t			cont_nr = 10;


	if (uuid == NULL) {
		rc = daos_pool_list_cont(ctx->poh, &cont_nr, cont_info, NULL);
		if (rc != 0) {
			printf("pool list container failed: %d\n", rc);
			return rc;
		}
		if (cont_nr == 0) {
			printf("ERROR: No Container\n");
			return -1;
		}
		if (cont_nr > 1) {
			printf("More than one container. Going to just pick first one.\n");
		}
		uuid_copy(ctx->cont_uuid, cont_info[0].pci_uuid);
	} else {
		uuid_parse(uuid, ctx->cont_uuid);
	}

	rc = daos_cont_open(ctx->poh, ctx->cont_uuid, DAOS_COO_RW,
			    &ctx->coh, NULL, NULL);

	if (rc != 0) {
		printf("Cont Open Failed: %d\n", rc);
		return rc;
	}

	if (ctx->verbose) {
		printf("Using cont: ");
		print_uuid(ctx->cont_uuid);
		printf("\n");
	}

	return rc;
}

static int
ctx_obj_open(struct io_context *ctx, struct io_args *args)
{
	int rc = 0;
	int oclass = daos_oclass_name2id(args->obj_type);
//	if (oclass != DAOS_OC_R1S_SPEC_RANK &&
//	    oclass !=  DAOS_OC_R2S_SPEC_RANK &&
//	    oclass != DAOS_OC_R3S_SPEC_RANK) {
//		printf("Only object class with specified rank are supported"
//		       "[S1_SR|RP_2G1_SR|RP_3G1_SR]");
//		return -1;
//	}

	create_oid(&ctx->oid, oclass);
	rc = daos_obj_open(ctx->coh, ctx->oid, DAOS_OO_RW, &ctx->oh, NULL);
	if (rc != 0) {
		printf("Object Open failed: %d\n", rc);
		return rc;
	}
	return rc;
}

static int
parse_fault_type(char *type)
{
	if (type != NULL && strcmp(type, "DISK") == 0)
		return  DAOS_CHECKSUM_FAULT_DISK;
	return DAOS_CHECKSUM_FAULT_NETWORK;
}

static int
ctx_init(struct io_context *ctx, struct io_args *args)
{
	int rc = 0;

	/** Set Values */
	ctx->mode = 0731; // 146
	ctx->uid = geteuid();
	ctx->gid = getegid();
	strcpy(ctx->group, "daos_server");

	ctx->svc.rl_nr = 1;
	ctx->svc.rl_ranks = ctx->svc_ranks;

	/* [todo-ryon]: should clean up connections better on errors */
	rc = ctx_connect_pool(ctx);
	if (rc != 0) {
		printf("Pool Connect failed: %d", rc);
		return rc;
	}

	rc = ctx_connect_cont(ctx, args->cont_uuid);
	if (rc != 0) {
		printf("Container Connect failed: %d\n", rc);

		return rc;
	}

	rc = ctx_obj_open(ctx, args);
	if (rc != 0) {
		printf("Object Open failed: %d", rc);
		return rc;
	}

	iov_alloc_str(&ctx->dkey, args->dkey_str);

	/** common iod setup */
	iov_alloc_str(&ctx->iod.iod_name, args->akey_str);
	ctx->iod.iod_type = parse_iod_type(args->iod_type);
	ctx->iod.iod_nr = 1;

	if (args->fault != NULL) {
		ctx->fault = true;
		ctx->rank_to_fault = atoi(args->fault);
		ctx->fault_type = parse_fault_type(args->fault_type);
	}

	daos_sgl_init(&ctx->sgl, 1);

	return rc;
}

static int
ctx_fini(struct io_context *ctx)
{
	int rc = 0;

	/** close connections */
	rc = daos_obj_close(ctx->oh, NULL);
	if (rc != 0)  {
		printf("Object Close failed: %d\n", rc);
		return rc;
	}

	rc = daos_cont_close(ctx->coh, NULL);
	if (rc != 0)  {
		printf("Container Close failed: %d\n", rc);
		return rc;
	}

	rc = daos_pool_disconnect(ctx->poh, NULL);
	if (rc != 0) {
		printf("Pool Closed failed %d\n", rc);
		return rc;
	}

	/** release resources */
	daos_sgl_fini(&ctx->sgl, true);
	D_FREE(ctx->dkey.iov_buf);
	D_FREE(ctx->iod.iod_name.iov_buf);
	return rc;
}

static int
ctx_obj_update(struct io_context *ctx)
{
	int rc;

//	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ONCE);
//	daos_fail_value_set(0);
	rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0, &ctx->dkey, 1,
			     &ctx->iod, &ctx->sgl, NULL);
	daos_fail_loc_reset();

	return rc;
}

static int
ctx_obj_fetch(struct io_context *ctx)
{
	int rc;
	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ONCE);
	daos_fail_value_set(0);
	rc = daos_obj_fetch((*ctx).oh, DAOS_TX_NONE, 0, &(*ctx).dkey, 1,
			    &(*ctx).iod, &(*ctx).sgl, NULL, NULL);

	daos_fail_loc_reset();

	return rc;
}


struct cmd_struct {
	const char *cmd;
	int (*fn)(int, char **);
};


enum arg_options {
	OPT_OBJECT_TYPE	= 0x1234,
	OPT_AKEY	= 0x1235,
	OPT_DKEY	= 0x1236,
	OPT_VALUE_TYPE	= 0x1237,
	OPT_INDEX	= 'd',
	OPT_STRING	= 's',
	OPT_NUM		= 'n',
	OPT_DUP		= 0x1238,
	OPT_IOD_TYPE	= 't',
	OPT_REQ_LEN	= 'l',
	OPT_FAULT	= 'x',
	OPT_FAULT_TYPE	= 0x1239,
	OPT_CONT	= 'c'
};


static int
parse_args_cb(int key, char *arg, struct argp_state *state)
{
	struct io_args *options = state->input;

	switch (key) {
	case OPT_OBJECT_TYPE:
		options->obj_type = arg;
		break;
	case OPT_IOD_TYPE:
		options->iod_type = arg;
		break;
	case OPT_REQ_LEN:
		options->req_len = atoi(arg);
		break;
	case OPT_AKEY:
		strncpy(options->akey_str, arg, KEY_STR_LEN);
		break;
	case OPT_DKEY:
		strncpy(options->dkey_str, arg, KEY_STR_LEN);
		break;
	case OPT_INDEX:
		options->recx_idx = atoi(arg);
		break;
	case OPT_STRING:
		strncpy(options->string, arg, STRING_LEN);
		break;
	case OPT_FAULT:
		options->fault = arg;
		break;
	case OPT_FAULT_TYPE:
		options->fault_type = arg;
		break;
	case OPT_CONT:
		options->cont_uuid = arg;
		break;
	case OPT_NUM:
		options->num_inc = atoi(arg);
		break;
	case OPT_DUP:
		options->duplicate = atoi(arg);
		break;
	}

	return 0;
}

static struct io_args default_args = {
	.akey_str = "akey",
	.dkey_str = "dkey",
	.obj_type = "S1_SR", //SX
	.iod_type = "SINGLE",
	.string = "<Nothing Specified>",
	.recx_idx = 0,
	.req_len = 1024
};

#define	COMMON_IO_ARGS \
	{ .name="index",.key=OPT_INDEX, .arg="index", \
		.doc="For array type, recx index"}, \
	{ .name="length", .key=OPT_REQ_LEN, .arg="rank" }, \
	{ .name="cont", .key=OPT_CONT, .arg="rank" }, \
	{ .name="iod-type", .key=OPT_IOD_TYPE, \
		.arg="SINGLE(default)|ARRAY" }, \
	{ .name="object-type", .key=OPT_OBJECT_TYPE, .arg="Object Class", \
	.doc="Use list-obj-class command"}, \
	{ .name="akey", .key=OPT_AKEY, .arg="akey", \
	.doc="akey to store the value under"}, \
	{ .name="dkey", .key=OPT_DKEY, .arg="dkey", \
	.doc="dkey to store the value under"} \

int
write_command(int argc, char **argv)
{
	int			rc;
	struct io_context	ctx = {0};
	struct io_args		args = default_args;
	struct argp		argp = {0};
	struct argp_option	options[] = {
		COMMON_IO_ARGS,
		{.name="string", .key=OPT_STRING, .arg="string value",
			.doc="String to write to an extent"},
		{ .name="fault", .key=OPT_FAULT, .arg="rank" },
		{ .name="fault-type", .key=OPT_FAULT_TYPE,
			.arg="NETWORK(default)|DISK" },
		{0}
	};

	/** will always be "write" command so just skip */
	argc--;
	argv++;
	argp.options = options;
	argp.parser = parse_args_cb;
	argp_parse(&argp, argc, argv, ARGP_PARSE_ARGV0, 0, &args);

	rc = ctx_init(&ctx, &args);
	if (rc != 0)
		return rc;

	if (ctx.iod.iod_type == DAOS_IOD_SINGLE) {
		ctx.iod.iod_size = strlen(args.string);
	} else {
		ctx.iod.iod_size = 1;
		ctx.recx.rx_nr = strlen(args.string);
		ctx.recx.rx_idx = args.recx_idx;
		ctx.iod.iod_recxs = &ctx.recx;
	}

	iov_alloc_str(&ctx.sgl.sg_iovs[0], args.string);

	if (ctx.fault) {
		/* [todo-ryon]: look at ds_pool_check_leader for server side */
		printf("Injecting %s fault to rank: %d\n",
		       ctx.fault_type ==  DAOS_CHECKSUM_FAULT_DISK ? "DISK" :
		       ctx.fault_type ==  DAOS_CHECKSUM_FAULT_NETWORK ? "NETWORK" :
		       "",
		       ctx.rank_to_fault);
		daos_mgmt_set_params(ctx.group,
				     ctx.rank_to_fault,
				     DMG_KEY_FAIL_LOC,
	/** DAOS_CHECKSUM_FAULT_DISK, DAOS_CHECKSUM_FAULT_NETWORK  */
				     ctx.fault_type | DAOS_FAIL_ALWAYS,
				     0, NULL);

	} else {
		daos_mgmt_set_params(ctx.group, -1, DMG_KEY_FAIL_LOC,
				     0,
				     0, NULL);
	}

	rc = ctx_obj_update(&ctx);

	if (rc != 0) {
		printf("Object Update failed: %d\n", rc);
		ctx_fini(&ctx);
		return rc;
	}

	printf("'%s' written to: ", args.string);
	print_pkey(&ctx, &args);


	/** Remove fault */
	daos_mgmt_set_params(ctx.group, -1, DMG_KEY_FAIL_LOC,
			     0,
			     0, NULL);

	ctx_fini(&ctx);

	return 0;
}

int
inc_write_command(int argc, char **argv)
{
	int			rc;
	struct io_context	ctx = {0};
	struct io_args		args = default_args;
	struct argp		argp = {0};
	struct argp_option	options[] = {
		COMMON_IO_ARGS,
		{.name="string", .key=OPT_STRING, .arg="string value"},
		{.name="number", .key=OPT_NUM, .arg="string value"},
		{.name="duplicate", .key=OPT_DUP, .arg="string value"},

		{0}
	};

	/** will always be "write" command so just skip */
	argc--;
	argv++;
	argp.options = options;
	argp.parser = parse_args_cb;
	argp_parse(&argp, argc, argv, ARGP_PARSE_ARGV0, 0, &args);

	rc = ctx_init(&ctx, &args);
	if (rc != 0)
		return rc;

	/** will always be array */
	ctx.iod.iod_type = DAOS_IOD_ARRAY;
	ctx.iod.iod_size = 1;
	ctx.recx.rx_nr = strlen(args.string);
	ctx.recx.rx_idx = args.recx_idx;
	ctx.iod.iod_recxs = &ctx.recx;

	iov_alloc_str(&ctx.sgl.sg_iovs[0], args.string);

	if (ctx.fault) {
		/** always write to the leader ? */
//		daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ALWAYS);
//		daos_fail_value_set(ctx.pool_info.pi_leader);
//
//		ctx.rank_to_fault = ctx.pool_info.pi_leader;


		printf("Faulting network to rank: %d\n", ctx.rank_to_fault);
		daos_mgmt_set_params(ctx.group,
				     ctx.rank_to_fault,
				     DMG_KEY_FAIL_LOC,
				     DAOS_CHECKSUM_FAULT_NETWORK | DAOS_FAIL_ALWAYS,
				     0, NULL);
	} else {
		daos_mgmt_set_params(ctx.group, -1, DMG_KEY_FAIL_LOC,
				     0,
				     0, NULL);
	}

	int i;
	for (i = 0; i < args.num_inc; i++) {
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.iod, &ctx.sgl, NULL);

		printf("'%s' written to: ", args.string);
		print_pkey(&ctx, &args);

		ctx.iod.iod_recxs->rx_idx += ctx.iod.iod_recxs->rx_nr;
		if (rc != 0) {
			printf("Object Update failed: %d\n", rc);
			ctx_fini(&ctx);
			goto done;
		}
	}

	/** Remove fault */
	daos_mgmt_set_params(ctx.group, -1, DMG_KEY_FAIL_LOC,
			     0,
			     0, NULL);
	daos_fail_loc_reset();
done:
	ctx_fini(&ctx);

	return 0;
}



int
read_command(int argc, char **argv)
{
	int			rc;
	struct io_context	ctx = {0};
	struct io_args		args = default_args;
	struct argp		argp = {0};

	struct argp_option options[] = {
		COMMON_IO_ARGS,
		{0}
	};

	/** will always be "read" command so just skip */
	argc--;
	argv++;
	argp.options = options;
	argp.parser = parse_args_cb;
	argp_parse(&argp, argc, argv, ARGP_PARSE_ARGV0, 0, &args);

	rc = ctx_init(&ctx, &args);
	if (rc != 0)
		return rc;

	if (ctx.iod.iod_type == DAOS_IOD_SINGLE) {
		ctx.iod.iod_size = args.req_len;
	} else {
		ctx.iod.iod_size = 1;
		ctx.recx.rx_nr = args.req_len;
		ctx.recx.rx_idx = args.recx_idx;
		ctx.iod.iod_recxs = &ctx.recx;
	}
	iov_alloc(&ctx.sgl.sg_iovs[0], args.req_len);

	/** start with fetch from rank 0 */
	rc = ctx_obj_fetch(&ctx);

	if (rc != 0) {
		printf("Fetch failed: %d\n", rc);
		ctx_fini(&ctx);
		return rc;
	}

	printf("'%s' read from: ", (char *)ctx.sgl.sg_iovs->iov_buf);
	print_pkey(&ctx, &args);

	ctx_fini(&ctx);

	return 0;

}

int
fault_command(int argc, char **argv)
{
	int			rc = 0;
	struct io_context	ctx = {0};
	struct io_args		args = default_args;
	struct argp		argp = {0};

	struct argp_option options[] = {
		{ .name="rank",.key=OPT_FAULT, .arg="index",
			.doc="For array type, recx index"},
		{ .name="type", .key=OPT_FAULT_TYPE,
			.arg="NETWORK(default)|DISK" },
		{0}
	};

	/** will always be "fault" command so just skip */
	argc--;
	argv++;
	argp.options = options;
	argp.parser = parse_args_cb;
	argp_parse(&argp, argc, argv, ARGP_PARSE_ARGV0, 0, &args);

	rc = ctx_init(&ctx, &args);
	if (rc != 0)
		return rc;

	if (ctx.fault) {
		printf("Setting %s fault injection to rank: %d\n",
		       ctx.fault_type ==  DAOS_CHECKSUM_FAULT_DISK ? "DISK" :
		       ctx.fault_type ==  DAOS_CHECKSUM_FAULT_NETWORK ? "NETWORK" :
		       "",
		       ctx.rank_to_fault);
		daos_mgmt_set_params(ctx.group,
				     ctx.rank_to_fault,
				     DMG_KEY_FAIL_LOC,
				     ctx.fault_type | DAOS_FAIL_ALWAYS,
				     0, NULL);

	} else {
		printf("Clearing fault injection to all ranks\n");
		daos_mgmt_set_params(ctx.group, -1, DMG_KEY_FAIL_LOC,
				     0,
				     0, NULL);
	}

	ctx_fini(&ctx);

	return 0;

}
int
show_map_command(int argc, char **argv)
{
	int			rc = 0;
	struct io_context	ctx = {0};
	struct io_args		args = default_args;
	struct argp		argp = {0};

	struct argp_option options[] = {
		{ .name="rank",.key=OPT_FAULT, .arg="index",
			.doc="For array type, recx index"},
		{ .name="cont", .key=OPT_CONT, .arg="rank" },
		{ .name="iod-type", .key=OPT_IOD_TYPE,
			.arg="SINGLE(default)|ARRAY" },
		{ .name="object-type", .key=OPT_OBJECT_TYPE, .arg="Object Class",
			.doc="Use list-obj-class command"},
		{ .name="akey", .key=OPT_AKEY, .arg="akey",
			.doc="akey to store the value under"},
		{ .name="dkey", .key=OPT_DKEY, .arg="dkey",
			.doc="dkey to store the value under"},
		{0}
	};

	/** will always be "show-map" command so just skip */
	argc--;
	argv++;
	argp.options = options;
	argp.parser = parse_args_cb;
	argp_parse(&argp, argc, argv, ARGP_PARSE_ARGV0, 0, &args);

	rc = ctx_init(&ctx, &args);
	if (rc != 0)
		return rc;

	struct pl_map *map = pl_map_find(ctx.pool_uuid, ctx.oid);

	pl_map_print(map);

	ctx_fini(&ctx);

	return 0;

}

int
list_obj_class_command(int argc, char **argv)
{
	int rc = 0;

	int str_len = 1024 * 2;
	char str[str_len];
	rc = daos_oclass_names_list(str_len, str);
	if (rc > str_len) {
		printf("Need to increase str_len ...\n");
	}

	printf("%s\n", str);

	return rc;
}

struct cmd_struct commands[] = {
	{"write",          write_command},
	{"inc-write",      inc_write_command},
	{"read",           read_command},
	{"list-obj-class", list_obj_class_command},
	{"fault",          fault_command},
	{"show-map",       show_map_command},
	{NULL},
};

int main(int argc, char **argv)
{
	int rc = 0;
	int i = 0;
	bool found_command = false;
	char *command_name = NULL;
	rc = daos_init();
	if (rc != 0) {
		printf("DAOS INIT Failed: %d\n", rc);
		goto out;
	}
	if (argc == 1) {
		printf("No command specified\n");
		rc = -1;
		goto out;
	}

	command_name = argv[1];
	while(commands[i].cmd != NULL) {
		if (!strcmp(commands[i].cmd, command_name)) {
			rc = commands[i].fn(argc, argv);
			found_command = true;
			break;
		}
		i++;
	}

	if (!found_command)
		printf("No such command: %s\n", command_name);

out:
	daos_fini();
	return -rc;
}

