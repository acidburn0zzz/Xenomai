#ifndef FPTEST_H
#define FPTEST_H

#ifdef __KERNEL__
#include <linux/module.h>
#else /* !__KERNEL__ */
#include <stdio.h>
#define printk printf
#endif /* !__KERNEL__ */

static inline void fp_regs_set(unsigned val)
{
}

static inline int fp_regs_check(unsigned val)
{
    return 0;
}

#endif /* FPTEST_H */
