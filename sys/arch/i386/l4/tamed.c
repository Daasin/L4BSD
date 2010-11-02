/*
 * Interrupt disable/enable implemented with a queue
 *
 * For deadlock reasons this file must _not_ use any Linux functionality!
 */

#include <sys/types.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/segments.h>

#include <machine/l4/vcpu.h>

#include <l4/sys/types.h>
#include <l4/sys/vcpu.h>
#include <l4/sys/ipc.h>
#include <l4/sys/utcb.h>


static void do_vcpu_irq(l4_vcpu_state_t *v);


void l4x_global_cli(void)
{
	l4x_vcpu_state(cpu_number())->state &= ~L4_VCPU_F_IRQ;
#ifdef MULTIPROCESSOR
	mfence();
#endif
}

void l4x_global_sti(void)
{
	l4_vcpu_state_t *v = l4x_vcpu_state(cpu_number());
	while (/* CONSTCOND */ 1) {
		v->state |= L4_VCPU_F_IRQ;
#ifdef MULTIPROCESSOR
		mfence();
#endif

		if (!(v->sticky_flags & L4_VCPU_SF_IRQ_PENDING))
			break;

		v->state &= ~L4_VCPU_F_IRQ;
#ifdef MULTIPROCESSOR
		mfence();
#endif
		v->i.tag = l4_ipc_wait(l4_utcb(), &v->i.label, L4_IPC_NEVER);
		if (!l4_msgtag_has_error(v->i.tag))
			do_vcpu_irq(v);
	}
}

static void do_vcpu_irq(l4_vcpu_state_t *v)
{
	struct trapframe regs;
	regs.tf_cs = SEL_KPL;	/* kernel */
	regs.tf_eflags = 0;
	l4x_vcpu_handle_irq(v, &regs);
}
