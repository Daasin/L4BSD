/*	$OpenBSD: softintr.c,v 1.4 2009/04/19 19:13:57 oga Exp $	*/
/*	$NetBSD: softintr.c,v 1.1 2003/02/26 21:26:12 fvdl Exp $	*/

/*-
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Generic soft interrupt implementation for NetBSD/x86.
 */

#include <sys/param.h>
#include <sys/malloc.h>

#include <machine/intr.h>

#include <uvm/uvm_extern.h>

#ifdef L4
#include <net/netisr.h>
#define NETISR_MAX		sizeof(netisr)
#define BIT(n)			(1 << n)
#define CPL			lapic_tpr

void	*i386_softintr_netisrs[NETISR_MAX];
#endif	/* L4 */

struct i386_soft_intr i386_soft_intrs[I386_NSOFTINTR];

const int i386_soft_intr_to_ssir[I386_NSOFTINTR] = {
	SIR_CLOCK,
	SIR_NET,
	SIR_TTY,
};

/*
 * softintr_init:
 *
 *	Initialize the software interrupt system.
 */
void
softintr_init(void)
{
	struct i386_soft_intr *si;
	int i;

	for (i = 0; i < I386_NSOFTINTR; i++) {
		si = &i386_soft_intrs[i];
		TAILQ_INIT(&si->softintr_q);
		mtx_init(&si->softintr_lock, IPL_HIGH);
		si->softintr_ssir = i386_soft_intr_to_ssir[i];
	}

#ifdef L4
	/*
	 * Establish handlers for legacy network interrupts.
	 * This code is inspired by NetBSD's generic softint framework.
	 */
#define DONETISR(n, f)							\
	i386_softintr_netisrs[(n)] = &(f)
#include <net/netisr_dispatch.h>

#endif
}

#ifdef L4

/*
 * Run all network related soft interrupts. Do not run masked
 * service routines.
 */
void
l4x_run_netisrs(void)
{
	int i;
	void (*f)(void);

	/* Execute all unmasked network service routines. */
	for (i = 0; i < NETISR_MAX; i++) {
		if ((netisr & BIT(i))) {
			f = i386_softintr_netisrs[BIT(i)];
			if (f)
				(*f)();
		}
	}
}

/*
 * Run all softintrs on a given CPL. This is basically a wrapper
 * around softintr_dispatch() to prepare it like icu.s does.
 */
void
l4x_exec_softintr(int ipl)
{
	int spl;
	int which;

	spl = CPL;
	/* XXX hshoexer */
	CPL = ipl;

	switch (ipl) {
	case IPL_SOFTCLOCK:
		which = I386_SOFTINTR_SOFTCLOCK;
		break;

	case IPL_SOFTNET:
		which = I386_SOFTINTR_SOFTNET;
		break;

	case IPL_TTY:
	case IPL_SOFTTTY:
		which = I386_SOFTINTR_SOFTTTY;
		break;

	default:
		panic("l4x_exec_softintr");
	}

#ifdef MULTIPROCESSOR
	i386_softintlock();
#endif

	/*
	 * SOFTNET needs special treatment. Run netisrs!
	 * As seen in icu.s.
	 */
	if (ipl == IPL_SOFTNET)
		l4x_run_netisrs();

	softintr_dispatch(which);

#ifdef MULTIPROCESSOR
	i386_softintunlock();
#endif

	/* XXX hshoexer */
	CPL = spl;
}

/*
 * Execute all possible softintrs, if
 *
 *   1) CPL is low enough
 *   2) They were requested to run
 */
void
l4x_run_softintr(void)
{
	struct cpu_info *ci = curcpu();


	if ((CPL < IPL_SOFTTTY) &&
			((ci->ci_ipending & BIT(SIR_TTY)) & IUNMASK(CPL))) {
		l4x_exec_softintr(IPL_SOFTTTY);
		i386_atomic_clearbits_l(&ci->ci_ipending, BIT(SIR_TTY));
	}
	if ((CPL < IPL_SOFTNET) &&
			((ci->ci_ipending & BIT(SIR_NET)) & IUNMASK(CPL))) {
		l4x_exec_softintr(IPL_SOFTNET);
		i386_atomic_clearbits_l(&ci->ci_ipending, BIT(SIR_NET));
	}
	if ((CPL < IPL_SOFTCLOCK) &&
			((ci->ci_ipending & BIT(SIR_CLOCK)) & IUNMASK(CPL))) {
		l4x_exec_softintr(IPL_SOFTCLOCK);
		i386_atomic_clearbits_l(&ci->ci_ipending, BIT(SIR_CLOCK));
	}
}

#endif	/* L4 */

/*
 * softintr_dispatch:
 *
 *	Process pending software interrupts.
 */
void
softintr_dispatch(int which)
{
	struct i386_soft_intr *si = &i386_soft_intrs[which];
	struct i386_soft_intrhand *sih;

	for (;;) {
		mtx_enter(&si->softintr_lock);
		sih = TAILQ_FIRST(&si->softintr_q);
		if (sih == NULL) {
			mtx_leave(&si->softintr_lock);
			break;
		}
		TAILQ_REMOVE(&si->softintr_q, sih, sih_q);
		sih->sih_pending = 0;

		uvmexp.softs++;

		mtx_leave(&si->softintr_lock);

		(*sih->sih_fn)(sih->sih_arg);
	}
}

/*
 * softintr_establish:		[interface]
 *
 *	Register a software interrupt handler.
 */
void *
softintr_establish(int ipl, void (*func)(void *), void *arg)
{
	struct i386_soft_intr *si;
	struct i386_soft_intrhand *sih;
	int which;

	switch (ipl) {
	case IPL_SOFTCLOCK:
		which = I386_SOFTINTR_SOFTCLOCK;
		break;

	case IPL_SOFTNET:
		which = I386_SOFTINTR_SOFTNET;
		break;

	case IPL_TTY:
	case IPL_SOFTTTY:
		which = I386_SOFTINTR_SOFTTTY;
		break;

	default:
		panic("softintr_establish");
	}

	si = &i386_soft_intrs[which];

	sih = malloc(sizeof(*sih), M_DEVBUF, M_NOWAIT);
	if (__predict_true(sih != NULL)) {
		sih->sih_intrhead = si;
		sih->sih_fn = func;
		sih->sih_arg = arg;
		sih->sih_pending = 0;
	}
	return (sih);
}

/*
 * softintr_disestablish:	[interface]
 *
 *	Unregister a software interrupt handler.
 */
void
softintr_disestablish(void *arg)
{
	struct i386_soft_intrhand *sih = arg;
	struct i386_soft_intr *si = sih->sih_intrhead;

	mtx_enter(&si->softintr_lock);
	if (sih->sih_pending) {
		TAILQ_REMOVE(&si->softintr_q, sih, sih_q);
		sih->sih_pending = 0;
	}
	mtx_leave(&si->softintr_lock);

	free(sih, M_DEVBUF);
}
