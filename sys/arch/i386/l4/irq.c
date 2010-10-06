/*
 * IRQ functions.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/reg.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/i8259.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <machine/l4/vcpu.h>
#include <machine/l4/irq.h>

static int handle_irq(int irq, struct reg *regs);

static struct reg *irq_regs;

static inline struct reg *
set_irq_regs(struct reg *new_regs)
{
	struct reg *old_regs, **pp_regs = &irq_regs;

	old_regs = *pp_regs;
	*pp_regs = new_regs;
	return old_regs;
}

void l4x_vcpu_handle_irq(l4_vcpu_state_t *t, struct reg *regs)
{
	int irq = t->i.label >> 2;
	int s = splhigh();

#ifdef MULTIPROCESSOR	/* unported */
	if (irq & L4X_VCPU_IRQ_IPI)
		l4x_vcpu_handle_ipi(regs);
	else
#endif
	{
		do_IRQ(irq, regs);

#ifdef MULTIPROCESSOR	/* unported */
		if (smp_processor_id() == 0 && irq == TIMER_IRQ)
			l4x_smp_broadcast_timer();
#endif
	}
	splx(s);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(int irq, struct reg *regs)
{
	struct reg *old_regs = set_irq_regs(regs);

	cpu_unidle();

	if (!handle_irq(irq, regs)) {
		//ack_APIC_irq();

		printf("%s: %d.%d: Error processing interrupt!\n",
				__func__, cpu_number(), irq);
	}

	set_irq_regs(old_regs);
	return 1;
}

/*
 * Note, we only run hardware IRQs here.
 */
static int
handle_irq(int irq, struct reg *regs)
{
	/* On x86 all interrupts end up in one of these two handler lists. */
	struct intrhand *intrhand[ICU_LEN];	/* (E)ISA */
#if NIOAPIC > 0
	struct intrhand *apic_intrhand[256];	/*  APIC  */
#endif
	struct intrhand **p, *q;
	int result = 0;

	if (irq < ICU_LEN)
		for (p = &intrhand[irq]; (q = *p) != NULL; p = &q->ih_next)
			result |= q->ih_fun(q->ih_arg);
#if NIOAPIC > 0
	if (irq < 256)
		for (p = &apic_intrhand[irq]; (q = *p) != NULL; p = &q->ih_next)
			result |= q->ih_fun(q->ih_arg);
#endif

	return result;
}

