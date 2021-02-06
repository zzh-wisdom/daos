/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

/**
 * Test suite helper functions.
 */
#include <stdio.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
//#include <daos_test.h>
#include <daos.h>
#include "dts_obj_ctl.h"

/* path to dmg config file */
const char *dmg_config_file;

enum {
	DTS_INIT_NONE,		/* nothing has been initialized */
	DTS_INIT_DEBUG,		/* debug system has been initialized */
	DTS_INIT_MODULE,	/* modules have been loaded */
	DTS_INIT_POOL,		/* pool has been created */
	DTS_INIT_CONT,		/* container has been created */
	DTS_INIT_CREDITS,	/* I/O credits have been initialized */
};

static void
credit_return(struct dts_context *tsc, struct dts_io_credit *cred)
{
	tsc->tsc_credits[tsc->tsc_cred_avail] = cred;
	tsc->tsc_cred_inuse--;
	tsc->tsc_cred_avail++;
}

/**
 * examines if there is available credit freed by completed I/O, it will wait
 * until all credits are freed if @drain is true.
 */
static int
credit_poll(struct dts_context *tsc, bool drain)
{
	daos_event_t	*evs[DTS_CRED_MAX];
	int		 i;
	int		 rc;

	if (tsc->tsc_cred_inuse == 0)
		return 0; /* nothing inflight (sync mode never set inuse) */

	while (1) {
		rc = daos_eq_poll(tsc->tsc_eqh, 0, DAOS_EQ_WAIT, DTS_CRED_MAX,
				  evs);
		if (rc < 0) {
			fprintf(stderr, "failed to pool event: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		for (i = 0; i < rc; i++) {
			int err = evs[i]->ev_error;

			if (err != 0) {
				fprintf(stderr, "failed op: %d\n", err);
				return err;
			}
			credit_return(tsc, container_of(evs[i],
				      struct dts_io_credit, tc_ev));
		}

		if (tsc->tsc_cred_avail == 0)
			continue; /* still no available event */

		/* if caller wants to drain, is there any event inflight? */
		if (tsc->tsc_cred_inuse != 0 && drain)
			continue;

		return 0;
	}
}

/** try to obtain a free credit */
struct dts_io_credit *
dts_credit_take(struct dts_context *tsc)
{
	int	 rc;

	if (tsc->tsc_cred_avail < 0) /* synchronous mode */
		return &tsc->tsc_cred_buf[0];

	while (1) {
		if (tsc->tsc_cred_avail > 0) { /* yes there is free credit */
			tsc->tsc_cred_avail--;
			tsc->tsc_cred_inuse++;
			return tsc->tsc_credits[tsc->tsc_cred_avail];
		}

		rc = credit_poll(tsc, false);
		if (rc)
			return NULL;
	}
}

static int
credits_init(struct dts_context *tsc)
{
	int	i;
	int	rc;

	if (tsc->tsc_cred_nr > 0) {
		rc = daos_eq_create(&tsc->tsc_eqh);
		if (rc)
			return rc;

		if (tsc->tsc_cred_nr > DTS_CRED_MAX)
			tsc->tsc_cred_avail = tsc->tsc_cred_nr = DTS_CRED_MAX;
		else
			tsc->tsc_cred_avail = tsc->tsc_cred_nr;
	} else { /* synchronous mode */
		tsc->tsc_eqh		= DAOS_HDL_INVAL;
		tsc->tsc_cred_nr	= 1;  /* take one slot in the buffer */
		tsc->tsc_cred_avail	= -1; /* always available */
	}

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		struct dts_io_credit *cred = &tsc->tsc_cred_buf[i];

		memset(cred, 0, sizeof(*cred));
		D_ALLOC(cred->tc_vbuf, tsc->tsc_cred_vsize);
		if (!cred->tc_vbuf) {
			fprintf(stderr, "Cannt allocate buffer size=%d\n",
				tsc->tsc_cred_vsize);
			return -1;
		}

		if (daos_handle_is_valid(tsc->tsc_eqh)) {
			rc = daos_event_init(&cred->tc_ev, tsc->tsc_eqh, NULL);
			D_ASSERTF(!rc, "rc="DF_RC"\n", DP_RC(rc));
			cred->tc_evp = &cred->tc_ev;
		}
		tsc->tsc_credits[i] = cred;
	}
	return 0;
}

static void
credits_fini(struct dts_context *tsc)
{
	int	i;

	D_ASSERT(!tsc->tsc_cred_inuse);

	for (i = 0; i < tsc->tsc_cred_nr; i++) {
		if (daos_handle_is_valid(tsc->tsc_eqh))
			daos_event_fini(&tsc->tsc_cred_buf[i].tc_ev);

		D_FREE(tsc->tsc_cred_buf[i].tc_vbuf);
	}

	if (daos_handle_is_valid(tsc->tsc_eqh))
		daos_eq_destroy(tsc->tsc_eqh, DAOS_EQ_DESTROY_FORCE);
}

static int
pool_init(struct dts_context *tsc)
{
	daos_handle_t	poh = DAOS_HDL_INVAL;
	int		rc;

	if (tsc->tsc_scm_size == 0)
		tsc->tsc_scm_size = (1ULL << 30);

	d_rank_list_t	*svc = &tsc->tsc_svc;

	if (tsc->tsc_dmg_conf)
		dmg_config_file = tsc->tsc_dmg_conf;

	rc = dmg_pool_create(dmg_config_file, geteuid(), getegid(),
			     NULL, NULL,
			     tsc->tsc_scm_size, tsc->tsc_nvme_size,
			     NULL, svc, tsc->tsc_pool_uuid);

	rc = daos_pool_connect(tsc->tsc_pool_uuid, NULL,
			       DAOS_PC_EX, &poh, NULL, NULL);

	tsc->tsc_poh = poh;

	return rc;
}

static void
pool_fini(struct dts_context *tsc)
{
	int	rc;

	daos_pool_disconnect(tsc->tsc_poh, NULL);

	rc = dmg_pool_destroy(dmg_config_file,
			      tsc->tsc_pool_uuid, NULL, true);
	D_ASSERTF(rc == 0 || rc == -DER_NONEXIST ||
		  rc == -DER_TIMEDOUT, "rc="DF_RC"\n",
		  DP_RC(rc));


}

static int
cont_init(struct dts_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;

	rc = daos_cont_create(tsc->tsc_poh, tsc->tsc_cont_uuid, NULL,
			      NULL);
	if (rc != 0)
		goto out;

	rc = daos_cont_open(tsc->tsc_poh, tsc->tsc_cont_uuid,
			    DAOS_COO_RW, &coh, NULL, NULL);

	tsc->tsc_coh = coh;
 
out:
	return rc;
}

static void
cont_fini(struct dts_context *tsc)
{
	daos_cont_close(tsc->tsc_coh, NULL);

	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

/* see comments in daos/dts.h */
int
dts_ctx_init(struct dts_context *tsc)
{
	int	rc;

	tsc->tsc_init = DTS_INIT_NONE;
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_DEBUG;

	rc = daos_init();

	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_MODULE;

	rc = pool_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_POOL;

	rc = cont_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_CONT;

	/* initialize I/O credits, which include EQ, event, I/O buffers... */
	rc = credits_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = DTS_INIT_CREDITS;

	return 0;
 out:
	fprintf(stderr, "Failed to initialize step=%d, rc=%d\n",
		tsc->tsc_init, rc);
	dts_ctx_fini(tsc);
	return rc;
}

/* see comments in daos/dts.h */
void
dts_ctx_fini(struct dts_context *tsc)
{
	switch (tsc->tsc_init) {
	case DTS_INIT_CREDITS:	/* finalize credits */
		credits_fini(tsc);
		/* fall through */
	case DTS_INIT_CONT:	/* close and destroy container */
		cont_fini(tsc);
		/* fall through */
	case DTS_INIT_POOL:	/* close and destroy pool */
		pool_fini(tsc);
		/* fall through */
	case DTS_INIT_MODULE:	/* finalize module */
			daos_fini();
		/* fall through */
	case DTS_INIT_DEBUG:	/* finalize debug system */
		daos_debug_fini();
	}
}
