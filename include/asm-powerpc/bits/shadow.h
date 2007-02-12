/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * 64-bit PowerPC adoption
 *   copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
 *   
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _XENO_ASM_POWERPC_BITS_SHADOW_H
#define _XENO_ASM_POWERPC_BITS_SHADOW_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

#include <asm-generic/xenomai/switch.h>

static inline void xnarch_init_shadow_tcb(xnarchtcb_t * tcb,
					  struct xnthread *thread,
					  const char *name)
{
	struct task_struct *task = current;

	tcb->user_task = task;
	tcb->active_task = NULL;
	tcb->tsp = &task->thread;
#ifdef CONFIG_XENO_HW_FPU
	tcb->user_fpu_owner = task;
	tcb->fpup = (rthal_fpenv_t *) & task->thread.fpr[0];
#endif /* CONFIG_XENO_HW_FPU */
	tcb->entry = NULL;
	tcb->cookie = NULL;
	tcb->self = thread;
	tcb->imask = 0;
	tcb->name = name;
}

static inline void xnarch_grab_xirqs(rthal_irq_handler_t handler)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
		rthal_virtualize_irq(rthal_current_domain,
				     irq,
				     handler, NULL, NULL, IPIPE_HANDLE_MASK);

	/* On this arch, the decrementer trap is not an external IRQ but
	   it is instead mapped to a virtual IRQ, so we must grab it
	   individually. */

	rthal_virtualize_irq(rthal_current_domain,
			     RTHAL_TIMER_IRQ,
			     handler, NULL, NULL, IPIPE_HANDLE_MASK);
}

static inline void xnarch_lock_xirqs(rthal_pipeline_stage_t * ipd, int cpuid)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++) {
		switch (irq) {
#ifdef CONFIG_SMP
		case RTHAL_CRITICAL_IPI:

			/* Never lock out this one. */
			continue;
#endif /* CONFIG_SMP */

		default:

			rthal_lock_irq(ipd, cpuid, irq);
		}
	}

	rthal_lock_irq(ipd, cpuid, RTHAL_TIMER_IRQ);
}

static inline void xnarch_unlock_xirqs(rthal_pipeline_stage_t * ipd, int cpuid)
{
	unsigned irq;

	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++) {
		switch (irq) {
#ifdef CONFIG_SMP
		case RTHAL_CRITICAL_IPI:

			continue;
#endif /* CONFIG_SMP */

		default:

			rthal_unlock_irq(ipd, irq);
		}
	}

	rthal_unlock_irq(ipd, RTHAL_TIMER_IRQ);
}

static inline int xnarch_local_syscall(struct pt_regs *regs)
{
	return -ENOSYS;
}

#define xnarch_schedule_tail(prev) do { } while(0)

#endif /* !_XENO_ASM_POWERPC_BITS_SHADOW_H */
