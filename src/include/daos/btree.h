/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/btree.h
 */

#ifndef __DAOS_BTREE_H__
#define __DAOS_BTREE_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos/mem.h>

/**
 * KV record of the btree.
 *
 * NB: could be PM data structure.
 */
struct btr_record {
	/**
	 * It could either be memory ID for the child node, or body of this
	 * record. The record body could be any of varous things:
	 *
	 * - the value address of KV record.
	 * - a structure includes both the variable-length key and value.
	 * - a complex data structure under this record, e.g. a sub-tree.
	 */
	umem_off_t		rec_off;
	/**
	 * Fix-size key can be stored in if it is small enough (DAOS_HKEY_MAX),
	 * or hashed key for variable-length/large key. In the later case,
	 * the hashed key can be used for efficient comparison.
	 *
	 * When BTR_FEAT_UINT_KEY is set, no key callbacks are used for
	 * comparisons.
	 *
	 * When BTR_FEAT_DIRECT_KEY is used, we store the umem offset of the
	 * relevant leaf node for direct key comparison
	 */
	union {
		char			rec_hkey[0]; /* hashed key */
		uint64_t		rec_ukey[0]; /* uint key */
		umem_off_t		rec_node[0]; /* direct key */
	};
};

/**
 * Tree node.
 *
 * NB: could be PM data structure.
 */
struct btr_node {
	/** leaf, root etc */
	uint16_t			tn_flags;
	/** number of keys stored in this node */
	uint16_t			tn_keyn;
	/** padding bytes */
	uint32_t			tn_pad_32;
	/** generation, reserved for COW */
	uint64_t			tn_gen;
	/** the first child, it is unused on leaf node */
	umem_off_t			tn_child;
	/** records in this node */
	struct btr_record		tn_recs[0];
};

enum {
	BTR_ORDER_MIN			= 3,
	BTR_ORDER_MAX			= 63
};

/**
 * Tree root descriptor, it consists of tree attributes and reference to the
 * actual root node.
 *
 * NB: Can be stored in pmem
 */
struct btr_root {
	/** For dynamic tree ordering, the root node temporarily has less
	 * entries than the order
	 */
	uint8_t				tr_node_size;
	/** configured btree order */
	uint8_t				tr_order;
	/** depth of the tree */
	uint16_t			tr_depth;
	/**
	 * ID to find a registered tree class, which provides customized
	 * functions etc.
	 */
	uint32_t			tr_class;
	/** the actual features of the tree, e.g. hash type, integer key */
	uint64_t			tr_feats;
	/** generation, reserved for COW */
	uint64_t			tr_gen;
	/** pointer to root node (struct btr_node), UMOFF_NULL for empty tree */
	umem_off_t			tr_node;
};

/** btree attributes returned by query function. */
struct btr_attr {
	/** Estimate of entries in tree.  Exact for tree depth <= 1 */
	int				ba_count;
	/** tree order */
	unsigned int			ba_order;
	/** tree depth */
	unsigned int			ba_depth;
	unsigned int			ba_class;
	uint64_t			ba_feats;
	/** memory class, pmem pool etc */
	struct umem_attr		ba_uma;
};

/** btree statistics returned by query function. */
struct btr_stat {
	/** total number of tree nodes */
	uint64_t			bs_node_nr;
	/** total number of records in the tree */
	uint64_t			bs_rec_nr;
	/** total number of bytes of all keys */
	uint64_t			bs_key_sum;
	/** max key size */
	uint64_t			bs_key_max;
	/** total number of bytes of all values */
	uint64_t			bs_val_sum;
	/** max value size */
	uint64_t			bs_val_max;
};

struct btr_rec_stat {
	/** record key size */
	uint64_t			rs_ksize;
	/** record value size */
	uint64_t			rs_vsize;
};

struct btr_instance;

typedef enum {
	/** probe a specific key */
	BTR_PROBE_SPEC		= (1 << 8),
	/**
	 * unconditionally trust the probe result from the previous call,
	 * bypass probe process for dbtree_upsert (or delete) in the future.
	 *
	 * This can reduce search overhead for use cases like this:
	 *    rc = dbtree_fetch(...key...);
	 *    if (rc == -DER_NONEXIST) {
	 *	    do_something_else(...);
	 *	    rc = dbtree_upsert(..., BTR_PROBE_BYPASS, key...);
	 *    }
	 *
	 * Please be careful while using this flag, because it could break
	 * the correctness of dbtree if inserting a new key to a mismatched
	 * probe path.
	 */
	BTR_PROBE_BYPASS	= 0,
	/** the first record in the tree */
	BTR_PROBE_FIRST		= 1,
	/** the last record in the tree */
	BTR_PROBE_LAST		= 2,
	/** probe the record whose key equals to the provide key */
	BTR_PROBE_EQ		= BTR_PROBE_SPEC,
	/** probe the record whose key is great to the provided key */
	BTR_PROBE_GT		= BTR_PROBE_SPEC | 1,
	/** probe the record whose key is less to the provided key */
	BTR_PROBE_LT		= BTR_PROBE_SPEC | 2,
	/** probe the record whose key is great/equal to the provided key */
	BTR_PROBE_GE		= BTR_PROBE_SPEC | 3,
	/** probe the record whose key is less/equal to the provided key */
	BTR_PROBE_LE		= BTR_PROBE_SPEC | 4,
} dbtree_probe_opc_t;

/** the return value of to_hkey_cmp/to_key_cmp callback */
enum btr_key_cmp_rc {
	BTR_CMP_EQ	= (0),		/* equal */
	BTR_CMP_LT	= (1 << 0),	/* less than */
	BTR_CMP_GT	= (1 << 1),	/* greater than */
	/**
	 * User can return it combined with BTR_CMP_LT/GT. If it is set,
	 * dbtree can fetch/update value even the provided key is less/greater
	 * than the compared key.
	 */
	BTR_CMP_UNKNOWN	= (1 << 2),	/* unset */
	BTR_CMP_ERR	= (1 << 3),	/* error */
};

/**
 * Customized tree function table.
 */
typedef struct {
	/**
	 * Generate a fix-size hashed key from the real key.
	 *
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  key	key buffer
	 * \param[out] hkey	hashed key
	 */
	void		(*to_hkey_gen)(struct btr_instance *tins,
				       d_iov_t *key, void *hkey);
	/** Static callback to get size of the hashed key. */
	int		(*to_hkey_size)(void);

	/** Static callback to metadata size of the record
	 *
	 * \param[in] alloc_overhead	Expected per-allocation overhead
	 */
	int		(*to_rec_msize)(int alloc_ovheread);
	/**
	 * Optional:
	 * Comparison of hashed key.
	 *
	 * Absent:
	 * Calls memcmp.
	 *
	 * \param[in] tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in] rec	Record to be compared with \a key.
	 * \param[in] key	Key to be compared with key of \a rec.
	 *
	 * \a return	BTR_CMP_LT	hkey of \a rec is smaller than \a hkey
	 *		BTR_CMP_GT	hkey of \a rec is larger than \a hkey
	 *		BTR_CMP_EQ	hkey of \a rec is equal to \a hkey
	 *		BTR_CMP_ERR	error in the hkey comparison
	 *		return any other value will cause assertion, segfault or
	 *		other undefined result.
	 */
	int		(*to_hkey_cmp)(struct btr_instance *tins,
				       struct btr_record *rec, void *hkey);
	/**
	 * Optional:
	 * Comparison of real key. It can be ignored if there is no hash
	 * for the key and key size is fixed. See \a btr_record for the details.
	 *
	 * Absent:
	 * Skip the function and only check rec::rec_hkey for the search.
	 *
	 * \param[in] tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in] rec	Record to be compared with \a key.
	 * \param[in] key	Key to be compared with key of \a rec.
	 *
	 * \a return	BTR_CMP_LT	key of \a rec is smaller than \a key
	 *		BTR_CMP_GT	key of \a rec is larger than \a key
	 *		BTR_CMP_EQ	key of \a rec is equal to \a key
	 *		BTR_CMP_ERR	error in the key comparison
	 *		return any other value will cause assertion, segfault or
	 *		other undefined result.
	 */
	int		(*to_key_cmp)(struct btr_instance *tins,
				      struct btr_record *rec, d_iov_t *key);

	/**
	 * Required if using direct keys. (Should only be called for direct key)
	 * The encoding/decoding of direct keys is required so that the key can
	 * be serialized.
	 *
	 * @param[in]  tins	Tree instance which contains the
	 *			root umem offset and memory class etc.
	 * @param[in]  key	The current key of iteration.
	 * @param[out] anchor	Anchor for the iteration
	 */
	void		(*to_key_encode)(struct btr_instance *tins,
					 d_iov_t *key,
					 daos_anchor_t *anchor);
	/**
	 * Required if using direct keys. (Should only be called for direct key)
	 *
	 * @param[in]  tins	Tree instance which contains the root
	 *			umem offset and memory class etc.
	 * @param[out] key	The key of iteration. Anchor will
	 *			be decoded to key.
	 * @param[in]  anchor	Anchor of where iteration process is.
	 */
	void		(*to_key_decode)(struct btr_instance *tins,
					 d_iov_t *key,
					 daos_anchor_t *anchor);

	/**
	 * Allocate record body for \a key and \a val.
	 *
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  key	Key buffer
	 * \param[in]  val	Value buffer, it could be either data blob,
	 *			or complex data structure that can be parsed
	 *			by the tree class.
	 * \param[out] rec	Returned record body pointer,
	 *			See \a btr_record for the details.
	 */
	int		(*to_rec_alloc)(struct btr_instance *tins,
					d_iov_t *key, d_iov_t *val,
					struct btr_record *rec);
	/**
	 * Free the record body stored in \a rec::rec_off
	 *
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  rec	The record to be destroyed.
	 * \param[out] args	Optional: opaque buffer for providing arguments
	 *			to handle special cases for free. for example,
	 *			allocator/GC address for externally allocated
	 *			resources.
	 */
	int		(*to_rec_free)(struct btr_instance *tins,
				       struct btr_record *rec, void *args);
	/**
	 * Fetch value or both key & value of a record.
	 *
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  rec	Record to be read from.
	 * \param[out] key	Optional, sink buffer for the returned key,
	 *			or key address.
	 * \param[out] val	Sink buffer for the returned value or the
	 *			value address.
	 */
	int		(*to_rec_fetch)(struct btr_instance *tins,
					struct btr_record *rec,
					d_iov_t *key, d_iov_t *val);
	/**
	 * Update value of a record, the new value should be stored in the
	 * current rec::rec_off.
	 *
	 * \param[in] tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in] rec	Record to be updated.
	 * \param[in] val	New value to be stored for the record.
	 * \a return		0	success.
	 *			-DER_NO_PERM
	 *				cannot make inplace change, should call
	 *				rec_free() to release the original
	 *				record and rec_alloc() to create a new
	 *				record.
	 *			-ve	error code
	 */
	int		(*to_rec_update)(struct btr_instance *tins,
					 struct btr_record *rec,
					 d_iov_t *key, d_iov_t *val);
	/**
	 * Optional:
	 * Return key and value size of the record.
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  rec	Record to get size from.
	 * \param[out] rstat	Returned key & value size.
	 */
	int		(*to_rec_stat)(struct btr_instance *tins,
				       struct btr_record *rec,
				       struct btr_rec_stat *rstat);
	/**
	 * Convert record into readable string and store it in \a buf.
	 *
	 * \param[in]  tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in]  rec	Record to be converted.
	 * \param[in]  leaf	Both key and value should be converted to
	 *			string if it is true, otherwise only the
	 *			hashed key will be converted to string.
	 *			(record of intermediate node),
	 * \param[out] buf	Buffer to store the returned string.
	 * \param[in]  buf_len	Buffer length.
	 */
	char	       *(*to_rec_string)(struct btr_instance *tins,
					 struct btr_record *rec, bool leaf,
					 char *buf, int buf_len);
	/**
	 * Optional:
	 * Check whether the given record is available to outside or not.
	 *
	 * \param[in] tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in] rec	Record to be checked.
	 * \param[in] intent	The intent for why check the record.
	 *
	 * \a return	ALB_AVAILABLE_DIRTY	The target is available but with
	 *					some uncommitted modification
	 *					or garbage, need cleanup.
	 *		ALB_AVAILABLE_CLEAN	The target is available,
	 *					no pending modification.
	 *		ALB_UNAVAILABLE		The target is unavailable.
	 *		-DER_INPROGRESS		If the target record is in
	 *					some uncommitted DTX, the caller
	 *					needs to retry related operation
	 *					some time later.
	 *		Other negative values on error.
	 */
	int		(*to_check_availability)(struct btr_instance *tins,
						 struct btr_record *rec,
						 uint32_t intent);
	/**
	 * Allocate a tree node
	 *
	 * \param[in] tins	Tree instance which contains the root umem
	 *			offset and memory class etc.
	 * \param[in] size	Node size
	 * \a return		Allocated node address (offset within the pool)
	 */
	umem_off_t	(*to_node_alloc)(struct btr_instance *tins, int size);

} btr_ops_t;

/**
 * Tree instance, it is instantiated while creating or opening a tree.
 */
struct btr_instance {
	/** instance of memory class for the tree */
	struct umem_instance		 ti_umm;
	/** Private data for opener */
	void				*ti_priv;
	/**
	 * The container open handle.
	 */
	daos_handle_t			 ti_coh;
	/** root umem offset */
	umem_off_t			 ti_root_off;
	/** root pointer */
	struct btr_root			*ti_root;
	/** Customized operations for the tree */
	btr_ops_t			*ti_ops;
};

/**
 * Inline data structure for embedding the key bundle and key into an anchor
 * for serialization.
 */
#define	EMBEDDED_KEY_MAX	100
struct btr_embedded_key {
	/** Inlined iov key references */
	uint32_t	ek_size;
	/** Inlined buffer the key references*/
	unsigned char	ek_key[EMBEDDED_KEY_MAX];
};

D_CASSERT(sizeof(struct btr_embedded_key) == DAOS_ANCHOR_BUF_MAX);

static inline void
embedded_key_encode(d_iov_t *key, daos_anchor_t *anchor)
{
	struct btr_embedded_key *embedded =
		(struct btr_embedded_key *)anchor->da_buf;

	D_ASSERT(key->iov_len <= sizeof(embedded->ek_key));

	memcpy(embedded->ek_key, key->iov_buf, key->iov_len);
	/** Pointers will have to be set on decode. */
	embedded->ek_size = key->iov_len;
}

static inline void
embedded_key_decode(d_iov_t *key, daos_anchor_t *anchor)
{
	struct btr_embedded_key *embedded =
		(struct btr_embedded_key *)anchor->da_buf;

	/* Fix the pointer first */
	key->iov_buf = &embedded->ek_key[0];
	key->iov_len = embedded->ek_size;
	key->iov_buf_len = embedded->ek_size;
}

/* Features are passed as 64-bit unsigned integer.   Only the bits below are
 * reserved.   A specific class can define its own bits to customize behavior.
 * For example, VOS can use bits to indicate the type of key comparison used
 * for user supplied key.   In general, using the upper bits is safer to avoid
 * conflicts in the future.
 */
enum btr_feats {
	/** Key is an unsigned integer.  Implies no hash or key callbacks */
	BTR_FEAT_UINT_KEY		= (1 << 0),
	/** Key is not hashed or stored by library.  User must provide
	 * to_key_cmp callback
	 */
	BTR_FEAT_DIRECT_KEY		= (1 << 1),
	/** Root is dynamically sized up to tree order.  This bit is set for a
	 *  tree class
	 */
	BTR_FEAT_DYNAMIC_ROOT		= (1 << 2),
};

/**
 * Get the return code of to_hkey_cmp/to_key_cmp in case of success, for failure
 * case need to directly set it as BTR_CMP_ERR.
 */
static inline int
dbtree_key_cmp_rc(int rc)
{
	if (rc == 0)
		return BTR_CMP_EQ;
	else if (rc < 0)
		return BTR_CMP_LT;
	else
		return BTR_CMP_GT;
}

static inline int
dbtree_is_empty_inplace(const struct btr_root *root)
{
	D_ASSERT(root != NULL);
	return root->tr_depth == 0;
}

/** Register a tree class with associated callbacks
 *
 *  \param[in]	tree_class	Unique identifier for the class
 *  \param[in]	tree_feats	Mask for all supported tree features
 *  \param[in]	ops		Callbacks implementing tree class
 *
 *  return	0 on success, error otherwise
 */
int  dbtree_class_register(unsigned int tree_class, uint64_t tree_feats,
			   btr_ops_t *ops);

/** Allocate a handle that can be used open and create version 2 routines
 *  to avoid excessive memory allocations
 *
 *  \param[out]	th	A usable tree handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_handle_create(daos_handle_t *th);

/** Free a previously allocated handle
 *
 *  \param[in]	th	An allocated tree handle.  The handle must not be open.
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_handle_destroy(daos_handle_t th);

/** Allocate, create, and open a new btree
 *
 *  \param[in]	tree_class	Already registered tree class
 *  \param[in]	tree_feats	The set of features for this instance of the
 *				tree.  Must be a subset of features supported
 *				by the tree class.
 * \param[in]	tree_order	The order (width) of tree nodes
 * \param[in]	uma		umem attributes for allocations
 * \param[out]	root_offp	Returned tree root umem offset
 * \param[out]	toh		Returned open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_create(unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   umem_off_t *root_offp, daos_handle_t *toh);

/** Create an open a new btree using specified root location
 *
 *  \param[in]	tree_class	Already registered tree class
 *  \param[in]	tree_feats	The set of features for this instance of the
 *				tree.  Must be a subset of features supported
 *				by the tree class.
 * \param[in]	tree_order	The order (width) of tree nodes
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	root		Virtual memory address of tree root
 * \param[out]	toh		Returned open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
			   unsigned int tree_order, struct umem_attr *uma,
			   struct btr_root *root, daos_handle_t *toh);

/** Create an open a new btree using specified root location
 *
 *  \param[in]	tree_class	Already registered tree class
 *  \param[in]	tree_feats	The set of features for this instance of the
 *				tree.  Must be a subset of features supported
 *				by the tree class.
 * \param[in]	tree_order	The order (width) of tree nodes
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	root		Virtual memory address of tree root
 * \param[in]	coh		Container open handle
 * \param[in]	priv		Private data returned with callbacks
 * \param[out]	toh		Returned open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_create_inplace_ex(unsigned int tree_class, uint64_t tree_feats,
			      unsigned int tree_order, struct umem_attr *uma,
			      struct btr_root *root, daos_handle_t coh,
			      void *priv, daos_handle_t *toh);

/** Identical to dbtree_create except handle allocated with
 *  dbtree_handle_create is passed in and used
 *
 *  \param[in]	tree_class	Already registered tree class
 *  \param[in]	tree_feats	The set of features for this instance of the
 *				tree.  Must be a subset of features supported
 *				by the tree class.
 * \param[in]	tree_order	The order (width) of tree nodes
 * \param[in]	uma		umem attributes for allocations
 * \param[out]	root_offp	Returned tree root umem offset
 * \param[in]	th		Already created handle used for open
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_create2(unsigned int tree_class, uint64_t tree_feats,
		    unsigned int tree_order, struct umem_attr *uma,
		    umem_off_t *root_offp, daos_handle_t th);

/** Identical to dbtree_create_inplace_ex except handle allocated with
 *  dbtree_handle_create is passed in and used
 *
 *  \param[in]	tree_class	Already registered tree class
 *  \param[in]	tree_feats	The set of features for this instance of the
 *				tree.  Must be a subset of features supported
 *				by the tree class.
 * \param[in]	tree_order	The order (width) of tree nodes
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	root		Virtual memory address of tree root
 * \param[in]	coh		Container open handle
 * \param[in]	priv		Private data returned with callbacks
 * \param[in]	th		Already created handle used for open
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_create_inplace_ex2(unsigned int tree_class, uint64_t tree_feats,
			       unsigned int tree_order, struct umem_attr *uma,
			       struct btr_root *root, daos_handle_t coh,
			       void *priv, daos_handle_t th);

/** Open an existing btree
 *
 * \param[in]	root_off	Offset of root
 * \param[in]	uma		umem attributes for allocations
 * \param[out]	toh		Returns open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_open(umem_off_t root_off, struct umem_attr *uma,
		 daos_handle_t *toh);

/** Open an existing btree using virtual memory address of root
 *
 * \param[in]	root		Virtual memory address of root
 * \param[in]	uma		umem attributes for allocations
 * \param[out]	toh		Returns open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_open_inplace(struct btr_root *root, struct umem_attr *uma,
			 daos_handle_t *toh);

/** Open an existing btree using virtual memory address of root
 *
 * \param[in]	root		Virtual memory address of root
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	coh		Container open handle
 * \param[in]	priv		Optional argument passed to callbacks
 * \param[out]	toh		Returns open handle
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_open_inplace_ex(struct btr_root *root, struct umem_attr *uma,
			    daos_handle_t coh, void *priv, daos_handle_t *toh);

/** Same as dbtree_open except handle allocated with dbtree_handle_create
 *  is passed in and used.
 *
 * \param[in]	root_off	Offset of root
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	th		Already created handle used for open
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_open2(umem_off_t root_off, struct umem_attr *uma,
		  daos_handle_t th);

/** Same as dbtree_open_inplace_ex except handle allocated with
 *  dbtree_handle_create is passed in and used.
 *
 * \param[in]	root_off	Offset of root
 * \param[in]	uma		umem attributes for allocations
 * \param[in]	coh		Container open handle
 * \param[in]	priv		Optional argument passed to callbacks
 * \param[in]	th		Already created handle used for open
 *
 *  \return	0 on success, error otherwise
 */
int  dbtree_open_inplace_ex2(struct btr_root *root, struct umem_attr *uma,
			     daos_handle_t coh, void *priv, daos_handle_t th);

/* Closes an open tree handle
 *
 * \param toh	Open tree handle to close
 *
 * \return	0 on success, error otherwise
 */
int  dbtree_close(daos_handle_t toh);

/* Destroys a tree referenced by the open tree handle
 *
 * \param toh	Open tree handle.  Handle is closed by routine
 *
 * \return	0 on success, error otherwise
 */
int  dbtree_destroy(daos_handle_t toh, void *args);

/**
 * This function drains key/values from the tree, each time it deletes a KV
 * pair, it consumes a @credits, which is input parameter of this function.
 * It returns if all input credits are consumed, or the tree is empty, in
 * the later case, it also destroys the btree.
 *
 * \param[in]	  toh		Tree open handle.
 * \param[in,out] credits	Input and returned drain credits
 * \param[in]	  args		user parameter for btr_ops_t::to_rec_free
 * \param[out]	  destroy	Tree is empty and destroyed
 *
 * \return	0 for success, error otherwise
 */
int  dbtree_drain(daos_handle_t toh, int *credits, void *args, bool *destroyed);

/**
 * Search the provided \a key and return its value to \a val_out.
 * If \a val_out provides sink buffer, then this function will copy record
 * value into the buffer, otherwise it only returns address of value of the
 * current record.
 *
 * \param[in]  toh	Tree open handle.
 * \param[in]  key	Key to search.
 * \param[out] val	Returned value address, or sink buffer to
 *			store returned value.
 *
 * \return		0	found
 *			-ve	error code
 */
int  dbtree_lookup(daos_handle_t toh, d_iov_t *key, d_iov_t *val_out);

/**
 * Update value of the provided key.
 *
 * \param[in] toh	Tree open handle.
 * \param[in] key	Key to search.
 * \param[in] val	New value for the key, it will punch the
 *			original value if \val is NULL.
 *
 * \return		0	success
 *			-ve	error code
 */
int  dbtree_update(daos_handle_t toh, d_iov_t *key, d_iov_t *val);

/**
 * Search the provided \a key and fetch its value (and key if the matched key
 * is different with the input key). This function can support advanced range
 * search operation based on \a opc.
 *
 * If \a key_out and \a val_out provide sink buffers, then key and value will
 * be copied into them. Otherwise if buffer address in \a key_out or/and
 * \a val_out is/are NULL, then addresses of key or/and value of the current
 * record will be returned.
 *
 * \param[in]  toh	Tree open handle.
 * \param[in]  opc	Probe opcode, see dbtree_probe_opc_t for the details.
 * \param[in]  intent	The operation intent.
 * \param[in]  key	Key to search
 * \param[out] key_out	Return the actual matched key if \a opc is not
 *                      BTR_PROBE_EQ.
 * \param[out] val_out	Returned value address, or sink buffer to store
 *			returned value.
 *
 * \return		0	found
 *			-ve	error code
 */
int  dbtree_fetch(daos_handle_t toh, dbtree_probe_opc_t opc, uint32_t intent,
		  d_iov_t *key, d_iov_t *key_out, d_iov_t *val_out);

/**
 * Update the value of the provided key, or insert it as a new key if
 * there is no match.
 *
 * \param[in] toh	Tree open handle.
 * \param[in] opc	Probe opcode, see dbtree_probe_opc_t for the details.
 * \param[in] intent	The operation intent
 * \param[in] key	Key to search.
 * \param[in] val	New value for the key, it will punch the original value
 *			if \val is NULL.
 *
 * \return		0	success
 *			-ve	error code
 */
int  dbtree_upsert(daos_handle_t toh, dbtree_probe_opc_t opc, uint32_t intent,
		   d_iov_t *key, d_iov_t *val);

/**
 * Delete the @key and the corresponding value from the btree.
 *
 * \param[in]     toh	Tree open handle.
 * \param[in]     key	The key to be deleted.
 * \param[in,out] args	Optional: buffer to provide args to handle special
 *			cases(if any)
 */
int  dbtree_delete(daos_handle_t toh, dbtree_probe_opc_t opc,
		   d_iov_t *key, void *args);

/**
 * Query attributes and/or gather nodes and records statistics of btree.
 *
 * \param[in]  toh	The tree open handle.
 * \param[out] attr	Optional, returned tree attributes.
 * \param[out] stat	Optional, returned nodes and records statistics.
 */
int  dbtree_query(daos_handle_t toh, struct btr_attr *attr,
		  struct btr_stat *stat);
int  dbtree_is_empty(daos_handle_t toh);
struct umem_instance *btr_hdl2umm(daos_handle_t toh);

/******* iterator API ******************************************************/

enum {
	/**
	 * Use the embedded iterator of the open handle.
	 * It can reduce memory consumption, but state of iterator can be
	 * overwritten by other tree operation.
	 */
	BTR_ITER_EMBEDDED	= (1 << 0),
};

/**
 * Initialize iterator.
 *
 * \param[in] toh		[IN]	Tree open handle
 * \param[in] options	Options for the iterator.
 *			BTR_ITER_EMBEDDED:
 *				if this bit is set, then this function will
 *				return the iterator embedded in the tree open
 *				handle. It will reduce memory consumption,
 *				but state of iterator could be overwritten
 *				by any other tree operation.
 *
 * \param[out] ih	Returned iterator handle.
 *
 * \return	0 for success, error otherwise
 */
int dbtree_iter_prepare(daos_handle_t toh, unsigned int options,
			daos_handle_t *ih);
int dbtree_iter_finish(daos_handle_t ih);

/**
 * Based on the \a opc, this function can do various things:
 * - set the cursor of the iterator to the first or the last record.
 * - find the record for the provided key.
 * - find the first record whose key is greater than or equal to the key.
 * - find the first record whose key is less than or equal to the key.
 *
 * This function must be called after dbtree_iter_prepare, it can be called
 * for arbitrary times for the same iterator.
 *
 * \param[in] ih	The iterator handle.
 * \param[in] opc	Probe opcode, see dbtree_probe_opc_t for the details.
 * \param[in] intent	The operation intent.
 * \param[in] key	The key to probe, it will be ignored if opc is
 *			BTR_PROBE_FIRST or BTR_PROBE_LAST.
 * \param[in] anchor	the anchor point to probe, it will be ignored if
 *			\a key is provided.
 * \note		If opc is not BTR_PROBE_FIRST or BTR_PROBE_LAST,
 *			key or anchor is required.
 *
 * return	0 for success, error otherwise
 */
int dbtree_iter_probe(daos_handle_t ih, dbtree_probe_opc_t opc,
		      uint32_t intent, d_iov_t *key, daos_anchor_t *anchor);
int dbtree_iter_next(daos_handle_t ih);
int dbtree_iter_prev(daos_handle_t ih);

/**
 * Fetch the key and value of current record, if \a key and \a val provide
 * sink buffers, then key and value will be copied into them. If buffer
 * address in \a key or/and \a val is/are NULL, then this function only
 * returns addresses of key or/and value of the current record.
 *
 * \param[in]  ih	Iterator open handle.
 * \param[out] key	Sink buffer for the returned key, the key address is
 *			returned if buffer address is NULL.
 * \param[out] val	Sink buffer for the returned value, the value address
 *			is returned if buffer address is NULL.
 * \param[out] anchor	Returned iteration anchor.
 */
int dbtree_iter_fetch(daos_handle_t ih, d_iov_t *key,
		      d_iov_t *val, daos_anchor_t *anchor);

/**
 * Delete the record pointed by the current iterating cursor. This function
 * will reset iterator before return, it means that caller should call
 * dbtree_iter_probe() again to reinitialize the iterator.
 *
 * \param[in]  ih		Iterator open handle.
 * \param[out] value_out	Optional, buffer to preserve value while
 *				deleting btree node.
 *
 * \return	0 on success, error otherwise
 */
int dbtree_iter_delete(daos_handle_t ih, void *args);
int dbtree_iter_empty(daos_handle_t ih);

/**
 * Prototype of dbtree_iterate() callbacks. When a callback returns an rc,
 *
 *   - if rc == 0, dbtree_iterate() continues;
 *   - if rc == 1, dbtree_iterate() stops and returns 0;
 *   - otherwise, dbtree_iterate() stops and returns rc.
 */
typedef int (*dbtree_iterate_cb_t)(daos_handle_t ih, d_iov_t *key,
				   d_iov_t *val, void *arg);

/**
 * Helper function to iterate a dbtree, either from the first record forward
 * (\a backward == false) or from the last record backward (\a backward ==
 * true). \a cb will be called with \a arg for each record. See also
 * dbtree_iterate_cb_t.
 *
 * \param[in] toh	Tree open handle
 * \param[in] intent	The operation intent
 * \param[in] backward	If true, iterate from last to first
 * \param[in] cb	Callback function (see dbtree_iterate_cb_t)
 * \param[in] arg	Callback argument
 */
int dbtree_iterate(daos_handle_t toh, uint32_t intent, bool backward,
		   dbtree_iterate_cb_t cb, void *arg);

enum {
	DBTREE_VOS_BEGIN	= 10,
	DBTREE_VOS_END		= DBTREE_VOS_BEGIN + 9,
	DBTREE_DSM_BEGIN	= 20,
	DBTREE_DSM_END		= DBTREE_DSM_BEGIN + 9,
	DBTREE_SMD_BEGIN	= 30,
	DBTREE_SMD_END		= DBTREE_SMD_BEGIN + 9,
};

/** Get overhead constants for a given tree class
 *
 * \param alloc_overhead[IN]	Expected per-allocation overhead in bytes
 * \param tclass[IN]		The registered tree class
 * \param feats[IN]		The features used to initialize the tree class
 * \param tree_order[IN]	The expected tree order used in creation
 * \param ovhd[OUT]		Struct to fill with overheads
 *
 * \return 0 on success, error otherwise
 */
int dbtree_overhead_get(int alloc_overhead, unsigned int tclass, uint64_t feats,
			int tree_order, struct daos_tree_overhead *ovhd);

#endif /* __DAOS_BTREE_H__ */
