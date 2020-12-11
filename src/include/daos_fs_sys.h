/**
 * (C) Copyright 2018-2021 Intel Corporation.
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

/* TODO evaluate includes */
#include <libgen.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/checksum.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/container.h>
#include <daos/array.h>

#include "daos.h"
#include "daos_fs.h"

#define DFS_SYS_NO_CACHE 1
#define DFS_SYS_NO_LOCK 2

/** struct holding attributes for the dfs_sys calls */
typedef struct dfs_sys dfs_sys_t;

/**
 * Mount a file system with dfs_mount.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	coh	Container open handle.
 * \param[in]	flags	Mount flags (O_RDONLY or O_RDWR).
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK)
 * \param[out]	dfs_sys	Pointer to the sys object created.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int flags, int sflags,
	      dfs_sys_t **dfs_sys);

/**
 * Unmount a file system with dfs_mount.
 *
 * \param[in]	dfs_sys	Pointer to the sys object.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys);

/**
 * Call dfs_access on a path.
 *
 * \param[in]	dfs_sys Pointer to the sys object.
 * \param[in]	path	Path to the entry.
 * \param[in]	mask	accessibility check(s) to be performed.
 *			It should be either the value F_OK, or a mask with
 *			bitwise OR of one or more of R_OK, W_OK, and X_OK.
 * \param[in]	flags	Access flags (O_NOFOLLOW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_access(dfs_sys_t *dfs_sys, const char* path, int mask, int flags);

/**
 * TODO documentation
 */
int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char* path, mode_t mode);

/**
 * TODO documentation
 */
int
dfs_sys_utimensat(dfs_sys_t *dfs_sys, const char *pathname,
		  const struct timespec times[2], int flags);

/**
 * TODO documentation
 */
int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char* path, struct stat* buf,
	     int flags);
/**
 * TODO documentation
 */
int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char* path, mode_t mode,
	      daos_oclass_id_t cid, daos_size_t chunk_size);

/**
 * TODO documentation
 */
int
dfs_sys_listxattr(dfs_sys_t *dfs_sys, const char *path, char *list,
		  daos_size_t *size, int flags);

/**
 * TODO documentation
 */
int
dfs_sys_getxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 void *value, daos_size_t *size, int flags);

/**
 * TODO documentation
 */
int
dfs_sys_setxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 const void *value, daos_size_t size, int set_flags, int flags);

/**
 * TODO documentation
 */
int
dfs_sys_readlink(dfs_sys_t *dfs_sys, const char *path, char *buf,
		 daos_size_t *size);

/**
 * TODO documentation
 */
int
dfs_sys_symlink(dfs_sys_t *dfs_sys, const char *oldpath, const char *newpath);

/**
 * TODO documentation
 */
/* TODO dfs_sys_open */

/**
 * TODO documentation
 */
int
dfs_sys_close(dfs_obj_t *obj);

/**
 * TODO documentation
 */
int
dfs_sys_read(dfs_sys_t *dfs_sys, dfs_obj_t *obj, void *buf, daos_size_t *size,
	     daos_off_t offset);

/**
 * TODO documentation
 */
int
dfs_sys_write(dfs_sys_t *dfs_sys, dfs_obj_t *obj, const void *buf,
	      daos_size_t size, daos_off_t offset);

/**
 * TODO documentation
 */
int
dfs_sys_truncate(dfs_sys_t *dfs_sys, const char *file, daos_off_t length);

/**
 * TODO documentation
 */
int
dfs_sys_ftruncate(dfs_sys_t *dfs_sys, dfs_obj_t *obj, daos_off_t length);

/**
 * TODO documentation
 */
int
dfs_sys_remove(dfs_sys_t *dfs_sys, const char *file, bool force,
	       daos_obj_id_t *oid);

/**
 * TODO documentation
 */
int
dfs_sys_mkdir(dfs_sys_t *dfs_sys, const char *dir, mode_t mode,
	      daos_oclass_id_t cid);

/**
 * TODO documentation
 */
int
dfs_sys_opendir(dfs_sys_t *dfs_sys, const char *dir, DIR **_dirp);

/**
 * TODO documentation
 */
int
dfs_sys_closedir(DIR *dirp);

/**
 * TODO documentation
 */
int
dfs_sys_readdir(dfs_sys_t *dfs_sys, DIR *dirp, struct dirent **_dirent);
