/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _POSIX_SEM_H
#define _POSIX_SEM_H

#include <xenomai/posix/thread.h>       /* For pse51_current_thread and
                                   pse51_thread_t definition. */

#define link2sem(laddr) \
((sem_t *)(((char *)laddr) - (int)(&((sem_t *)0)->link)))

#define synch2sem(saddr) \
((sem_t *)(((char *)saddr) - (int)(&((sem_t *)0)->synchbase)))

/* Must be called nklock locked, irq off. */
unsigned long pse51_usem_open(sem_t *sem, pid_t pid, unsigned long uaddr);

/* Must be called nklock locked, irq off. */
int pse51_usem_close(sem_t *sem, pid_t pid);

void pse51_sem_pkg_init(void);

void pse51_sem_pkg_cleanup(void);

#endif /* !_POSIX_SEM_H */
