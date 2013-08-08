/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <memory.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <fuse.h>
#include <xeno_config.h>
#include "copperplate/heapobj.h"
#include "copperplate/registry.h"
#include "copperplate/clockobj.h"
#include "copperplate/lock.h"
#include "copperplate/hash.h"
#include "copperplate/debug.h"
#include "copperplate/init.h"
#include "internal.h"

/*
 * CAUTION: this code shall NOT refer to the shared heap in any way,
 * only private storage is allowed here: sysregd won't map the main
 * shared heap permanently, but only in a transitory manner via
 * heapobj_bind_session() when reading a /system node.
 */

static pthread_t regfs_thid;

struct regfs_data {
	const char *arg0;
	char *mountpt;
	sem_t sync;
	int status;
	pthread_mutex_t lock;
	struct pvhash_table files;
	struct pvhash_table dirs;
};

static inline struct regfs_data *regfs_get_context(void)
{
	static struct regfs_data data;

	return &data;
}

struct regfs_dir {
	char *path;
	const char *basename;
	struct pvhashobj hobj;
	struct pvlist file_list;
	struct pvlist dir_list;
	int ndirs, nfiles;
	struct timespec ctime;
	struct pvholder link;
};

int registry_add_dir(const char *fmt, ...)
{
	struct regfs_data *p = regfs_get_context();
	char path[PATH_MAX], *basename;
	struct regfs_dir *parent, *d;
	struct pvhashobj *hobj;
	struct timespec now;
	int ret, state;
	va_list ap;

	if (__node_info.no_registry)
		return 0;

	va_start(ap, fmt);
	vsnprintf(path, PATH_MAX, fmt, ap);
	va_end(ap);

	basename = strrchr(path, '/');
	if (basename == NULL)
		return __bt(-EINVAL);

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));

	write_lock_safe(&p->lock, state);

	d = pvmalloc(sizeof(*d));
	if (d == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pvholder_init(&d->link);
	d->path = pvstrdup(path);

	if (strcmp(path, "/")) {
		d->basename = d->path + (basename - path) + 1;
		if (path == basename)
			basename++;
		*basename = '\0';
		hobj = pvhash_search(&p->dirs, path, strlen(path));
		if (hobj == NULL) {
			ret = -ENOENT;
			goto fail;
		}
		parent = container_of(hobj, struct regfs_dir, hobj);
		pvlist_append(&d->link, &parent->dir_list);
		parent->ndirs++;
	} else
		d->basename = d->path;

	pvlist_init(&d->file_list);
	pvlist_init(&d->dir_list);
	d->ndirs = d->nfiles = 0;
	d->ctime = now;
	ret = pvhash_enter(&p->dirs, d->path, strlen(d->path), &d->hobj);
	if (ret) {
	fail:
		pvfree(d->path);
		pvfree(d);
	}
done:
	write_unlock_safe(&p->lock, state);

	return __bt(ret);
}

void registry_init_file(struct fsobj *fsobj, 
			const struct registry_operations *ops)
{
	pthread_mutexattr_t mattr;

	if (__node_info.no_registry)
		return;

	fsobj->path = NULL;
	fsobj->ops = ops;
	pvholder_init(&fsobj->link);

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE));
	__RT(pthread_mutex_init(&fsobj->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));
}

int registry_add_file(struct fsobj *fsobj, int mode, const char *fmt, ...)
{
	struct regfs_data *p = regfs_get_context();
	char path[PATH_MAX], *basename, *dir;
	struct pvhashobj *hobj;
	struct regfs_dir *d;
	int ret, state;
	va_list ap;

	if (__node_info.no_registry)
		return 0;

	va_start(ap, fmt);
	vsnprintf(path, PATH_MAX, fmt, ap);
	va_end(ap);

	basename = strrchr(path, '/');
	if (basename == NULL)
		return __bt(-EINVAL);

	fsobj->path = pvstrdup(path);
	fsobj->basename = fsobj->path + (basename - path) + 1;
	fsobj->mode = mode & O_ACCMODE;
	__RT(clock_gettime(CLOCK_COPPERPLATE, &fsobj->ctime));
	fsobj->mtime = fsobj->ctime;

	write_lock_safe(&p->lock, state);

	ret = pvhash_enter(&p->files, fsobj->path, strlen(fsobj->path),
			   &fsobj->hobj);
	if (ret)
		goto fail;

	*basename = '\0';
	dir = basename == path ? "/" : path;
	hobj = pvhash_search(&p->dirs, dir, strlen(dir));
	if (hobj == NULL) {
	fail:
		pvhash_remove(&p->files, &fsobj->hobj);
		pvfree(fsobj->path);
		fsobj->path = NULL;
		ret = -ENOENT;
		goto done;
	}

	d = container_of(hobj, struct regfs_dir, hobj);
	pvlist_append(&fsobj->link, &d->file_list);
	d->nfiles++;
	fsobj->dir = d;
done:
	write_unlock_safe(&p->lock, state);

	return __bt(ret);
}

void registry_destroy_file(struct fsobj *fsobj)
{
	struct regfs_data *p = regfs_get_context();
	struct regfs_dir *d;
	int state;

	if (__node_info.no_registry)
		return;

	write_lock_safe(&p->lock, state);

	if (fsobj->path == NULL)
		goto out;	/* Not registered. */

	pvhash_remove(&p->files, &fsobj->hobj);
	/*
	 * We are covered by a previous call to write_lock_safe(), so
	 * we may nest pthread_mutex_lock() directly.
	 */
	__RT(pthread_mutex_lock(&fsobj->lock));
	d = fsobj->dir;
	pvlist_remove(&fsobj->link);
	d->nfiles--;
	assert(d->nfiles >= 0);
	pvfree(fsobj->path);
	__RT(pthread_mutex_unlock(&fsobj->lock));
out:
	__RT(pthread_mutex_destroy(&fsobj->lock));
	write_unlock_safe(&p->lock, state);
}

void registry_touch_file(struct fsobj *fsobj)
{
	if (__node_info.no_registry)
		return;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &fsobj->mtime));
}

static int regfs_getattr(const char *path, struct stat *sbuf)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct regfs_dir *d;
	struct fsobj *fsobj;
	int ret = 0;

	memset(sbuf, 0, sizeof(*sbuf));

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->dirs, path, strlen(path));
	if (hobj) {
		d = container_of(hobj, struct regfs_dir, hobj);
		sbuf->st_mode = S_IFDIR | 0755;
		sbuf->st_nlink = d->ndirs + 2;
		sbuf->st_atim = d->ctime;
		sbuf->st_ctim = d->ctime;
		sbuf->st_mtim = d->ctime;
		goto done;
	}

	hobj = pvhash_search(&p->files, path, strlen(path));
	if (hobj) {
		fsobj = container_of(hobj, struct fsobj, hobj);
		sbuf->st_mode = S_IFREG;
		switch (fsobj->mode) {
		case O_RDONLY:
			sbuf->st_mode |= 0444;
			break;
		case O_WRONLY:
			sbuf->st_mode |= 0222;
			break;
		case O_RDWR:
			sbuf->st_mode |= 0666;
			break;
		}
		sbuf->st_nlink = 1;
		sbuf->st_size = 32768; /* XXX: this should be dynamic. */
		sbuf->st_atim = fsobj->mtime;
		sbuf->st_ctim = fsobj->ctime;
		sbuf->st_mtim = fsobj->mtime;
	} else
		ret = -ENOENT;
done:
	read_unlock(&p->lock);

	return ret;
}

static int regfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct regfs_dir *d, *subd;
	struct pvhashobj *hobj;
	struct fsobj *fsobj;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->dirs, path, strlen(path));
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOENT);
	}

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	d = container_of(hobj, struct regfs_dir, hobj);

	if (!pvlist_empty(&d->dir_list)) {
		pvlist_for_each_entry(subd, &d->dir_list, link) {
			/* We don't output empty directories. */
			if (subd->ndirs + subd->nfiles == 0)
				continue;
			if (filler(buf, subd->basename, NULL, 0))
				break;
		}
	}

	if (!pvlist_empty(&d->file_list)) {
		pvlist_for_each_entry(fsobj, &d->file_list, link)
			if (filler(buf, fsobj->basename, NULL, 0))
				break;
	}

	read_unlock(&p->lock);

	return 0;
}

static int regfs_open(const char *path, struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	int ret = 0;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path));
	if (hobj == NULL) {
		ret = -ENOENT;
		goto done;
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (((fi->flags + 1) & (fsobj->mode + 1)) == 0)
		ret = -EACCES;
done:
	read_unlock(&p->lock);

	return __bt(ret);
}

static int regfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	int ret;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path));
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-EIO);
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (fsobj->ops->read == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOSYS);
	}

	push_cleanup_lock(&fsobj->lock);
	read_lock(&fsobj->lock);
	read_unlock(&p->lock);
	ret = fsobj->ops->read(fsobj, buf, size, offset);
	read_unlock(&fsobj->lock);
	pop_cleanup_lock(&fsobj->lock);

	return __bt(ret);
}

static int regfs_write(const char *path, const char *buf, size_t size, off_t offset,
		       struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	int ret;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path));
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-EIO);
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (fsobj->ops->write == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOSYS);
	}

	push_cleanup_lock(&fsobj->lock);
	read_lock(&fsobj->lock);
	read_unlock(&p->lock);
	ret = fsobj->ops->write(fsobj, buf, size, offset);
	read_unlock(&fsobj->lock);
	pop_cleanup_lock(&fsobj->lock);

	return __bt(ret);
}

static int regfs_truncate(const char *path, off_t offset)
{
	return 0;
}

static int regfs_chmod(const char *path, mode_t mode)
{
	return 0;
}

static int regfs_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}

static void *regfs_init(struct fuse_conn_info *conn)
{
	struct regfs_data *p = regfs_get_context();
	struct sigaction sa;

	/*
	 * Override annoying FUSE settings. Unless the application
	 * tells otherwise, we want the emulator to exit upon common
	 * termination signals.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	p->status = 0;	/* all ok. */
	__STD(sem_post(&p->sync));

	return p;
}

static struct fuse_operations regfs_opts = {
	.init		= regfs_init,
	.getattr	= regfs_getattr,
	.readdir	= regfs_readdir,
	.open		= regfs_open,
	.read		= regfs_read,
	.write		= regfs_write,
	/* Those must be defined for writing to files too. */
	.truncate	= regfs_truncate,
	.chown		= regfs_chown,
	.chmod		= regfs_chmod,
};

static void *registry_thread(void *arg)
{
	struct regfs_data *p = arg;
	char *av[7];
	int ret;

	av[0] = (char *)p->arg0;
	av[1] = "-s";
	av[2] = "-f";
	av[3] = p->mountpt;
	av[4] = "-o";
	av[5] = "allow_other,default_permissions";
	av[6] = NULL;

	/*
	 * Once connected to sysregd, we don't have to care for the
	 * mount point, sysregd will umount(2) it when we go away.
	 */
	ret = fuse_main(6, av, &regfs_opts, NULL);
	if (ret) {
		warning("can't mount registry onto %s", p->mountpt);
		/* Attempt to figure out why we failed. */
		ret = access(p->mountpt, F_OK);
		p->status = ret ? -errno : -EPERM;
		__STD(sem_post(&p->sync));
		return (void *)(long)ret;
	}

	return NULL;
}

static int spawn_daemon(const char *sessdir)
{
	struct sigaction sa;
	char *exec_path;
	int ret;

	ret = asprintf(&exec_path, "%s/bin/sysregd", CONFIG_XENO_PREFIX);
	if (ret < 0)
		return -ENOMEM;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	switch (vfork()) {
	case 0:
		execlp(exec_path, "sysregd", "--daemon",
		       "--root", sessdir, NULL);
		_exit(1);
	case -1:
		ret = -errno;
		break;
	default:
		usleep(1000000);
		ret = 0;
		break;
	}

	free(exec_path);

	return ret;
}

static int connect_regd(const char *sessdir, char **mountpt)
{
	struct sockaddr_un sun;
	int s, ret, retries;
	unsigned int hash;
	socklen_t addrlen;

	*mountpt = malloc(PATH_MAX);
	if (*mountpt == NULL)
		return -ENOMEM;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	hash = __hash_key(sessdir, strlen(sessdir), 0);
	snprintf(sun.sun_path, sizeof(sun.sun_path), "X%X-xenomai", hash);
	addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(sun.sun_path);
	sun.sun_path[0] = '\0';

	for (retries = 0; retries < 3; retries++) {
		s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (s < 0) {
			ret = -errno;
			free(*mountpt);
			return ret;
		}
		ret = connect(s, (struct sockaddr *)&sun, addrlen);
		if (ret == 0) {
			ret = recv(s, *mountpt, PATH_MAX, 0);
			if (ret > 0)
				return 0;
		}
		close(s);
		ret = spawn_daemon(sessdir);
		if (ret)
			break;
		ret = -EAGAIN;
	}

	free(*mountpt);

	warning("cannot connect to registry daemon");

	return ret;
}

static void pkg_cleanup(void)
{
	registry_pkg_destroy();
}

int __registry_pkg_init(const char *arg0, char *mountpt)
{
	struct regfs_data *p = regfs_get_context();
	pthread_mutexattr_t mattr;
	pthread_attr_t thattr;
	int ret;

	__RT(pthread_mutexattr_init(&mattr));
	__RT(pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT));
	__RT(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE));
	__RT(pthread_mutex_init(&p->lock, &mattr));
	__RT(pthread_mutexattr_destroy(&mattr));

	pvhash_init(&p->files, pvhash_compare_strings);
	pvhash_init(&p->dirs, pvhash_compare_strings);

	registry_add_dir("/");	/* Create the fs root. */

	/* We want a SCHED_OTHER thread, use defaults. */
	pthread_attr_init(&thattr);
	/*
	 * Memory is locked as the process data grows, so we set a
	 * smaller stack size for the fs thread than the default 8mb
	 * set by the Glibc.
	 */
	pthread_attr_setstacksize(&thattr, PTHREAD_STACK_MIN * 4);
	pthread_attr_setscope(&thattr, PTHREAD_SCOPE_PROCESS);
	p->arg0 = arg0;
	p->mountpt = mountpt;
	p->status = -EINVAL;
	__STD(sem_init(&p->sync, 0, 0));

	/*
	 * Start the FUSE filesystem daemon. Over Cobalt, it runs as a
	 * non real-time Xenomai shadow, so that it may synchronize on
	 * real-time objects.
	 */
	ret = __bt(-__RT(pthread_create(&regfs_thid, &thattr,
					registry_thread, p)));
	if (ret)
		return ret;

	/*
	 * We synchronize with regfs_init() to wait for FUSE to
	 * complete all its init chores before returning to our
	 * caller.
	 */
	ret = __bt(__STD(sem_wait(&p->sync)));
	if (ret)
		return -errno;

	atexit(pkg_cleanup);

	return p->status;
}

int registry_pkg_init(const char *arg0)
{
	char *mountpt, *sessdir;
	int ret;

	ret = asprintf(&sessdir, "%s/%s",
		       __node_info.registry_root, __node_info.session_label);
	if (ret < 0)
		return -ENOMEM;

	ret = connect_regd(sessdir, &mountpt);
	free(sessdir);
	if (ret)
		return ret;

	return __bt(__registry_pkg_init(arg0, mountpt));
}

void registry_pkg_destroy(void)
{
	if (regfs_thid) {
		pthread_cancel(regfs_thid);
		pthread_join(regfs_thid, NULL);
		regfs_thid = 0;
	}
}
