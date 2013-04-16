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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _COPPERPLATE_HASH_H
#define _COPPERPLATE_HASH_H

#include <pthread.h>
#include <copperplate/list.h>

#define HASHSLOTS  (1<<8)

struct hashobj {
	const char *key;
	struct holder link;
};

struct hash_bucket {
	struct list obj_list;
};

struct hash_table {
	struct hash_bucket table[HASHSLOTS];
	pthread_mutex_t lock;
};

#ifdef CONFIG_XENO_PSHARED
/* Private version - not shareable between processes. */
struct pvhashobj {
	const char *key;
	struct pvholder link;
};

struct pvhash_bucket {
	struct pvlist obj_list;
};

struct pvhash_table {
	struct pvhash_bucket table[HASHSLOTS];
	pthread_mutex_t lock;
};
#else /* !CONFIG_XENO_PSHARED */
#define pvhashobj	hashobj
#define pvhash_bucket	hash_bucket
#define pvhash_table	hash_table
#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
extern "C" {
#endif

unsigned int __hash_key(const void *key,
			int length, unsigned int c);

void __hash_init(void *heap, struct hash_table *t);

int __hash_enter(struct hash_table *t,
		 const char *key, struct hashobj *newobj,
		 int nodup);

static inline void hash_init(struct hash_table *t)
{
	__hash_init(__pshared_heap, t);
}

void hash_destroy(struct hash_table *t);

static inline int hash_enter(struct hash_table *t,
			     const char *key, struct hashobj *newobj)
{
	return __hash_enter(t, key, newobj, 1);
}

static inline int hash_enter_dup(struct hash_table *t,
				 const char *key, struct hashobj *newobj)
{
	return __hash_enter(t, key, newobj, 0);
}

int hash_remove(struct hash_table *t, struct hashobj *delobj);

struct hashobj *hash_search(struct hash_table *t, const char *key);

int hash_walk(struct hash_table *t,
		int (*walk)(struct hash_table *t, struct hashobj *obj));

#ifdef CONFIG_XENO_PSHARED

int __hash_enter_probe(struct hash_table *t,
		       const char *key, struct hashobj *newobj,
		       int (*probefn)(struct hashobj *oldobj),
		       int nodup);

int __pvhash_enter(struct pvhash_table *t,
		   const char *key, struct pvhashobj *newobj,
		   int nodup);

static inline
int hash_enter_probe(struct hash_table *t,
		     const char *key, struct hashobj *newobj,
		     int (*probefn)(struct hashobj *oldobj))
{
	return __hash_enter_probe(t, key, newobj, probefn, 1);
}

static inline
int hash_enter_probe_dup(struct hash_table *t,
			 const char *key, struct hashobj *newobj,
			 int (*probefn)(struct hashobj *oldobj))
{
	return __hash_enter_probe(t, key, newobj, probefn, 0);
}

struct hashobj *hash_search_probe(struct hash_table *t, const char *key,
				  int (*probefn)(struct hashobj *obj));

void pvhash_init(struct pvhash_table *t);

static inline
int pvhash_enter(struct pvhash_table *t,
		 const char *key, struct pvhashobj *newobj)
{
	return __pvhash_enter(t, key, newobj, 1);
}

static inline
int pvhash_enter_dup(struct pvhash_table *t,
		     const char *key, struct pvhashobj *newobj)
{
	return __pvhash_enter(t, key, newobj, 0);
}

int pvhash_remove(struct pvhash_table *t, struct pvhashobj *delobj);

struct pvhashobj *pvhash_search(struct pvhash_table *t, const char *key);

int pvhash_walk(struct pvhash_table *t,
		int (*walk)(struct pvhash_table *t, struct pvhashobj *obj));

#else /* !CONFIG_XENO_PSHARED */
#define pvhash_init		hash_init
#define pvhash_enter		hash_enter
#define pvhash_enter_dup	hash_enter_dup
#define pvhash_remove		hash_remove
#define pvhash_search		hash_search
#define pvhash_walk		hash_walk
#endif /* !CONFIG_XENO_PSHARED */

#ifdef __cplusplus
}
#endif

#endif /* _COPPERPLATE_HASH_H */
