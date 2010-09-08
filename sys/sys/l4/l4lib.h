#ifndef __INCLUDE__ASM_L4__GENERIC__L4LIB_H__
#define __INCLUDE__ASM_L4__GENERIC__L4LIB_H__

#include <machine/asm.h>

#ifdef ARCH_arm
#define L4_EXTERNAL_FUNC(func) \
	__asm(".section \".data.l4externals.str\"                         \n" \
	    "9: .string \"" __STRING(func) "\"                       \n" \
	    ".previous                                                  \n" \
	    \
	    "7: .long 9b                                                \n" \
	    \
	    "8: .long " __STRING(func##_resolver) "                  \n" \
	    \
	    ".globl " __STRING(func) "                               \n" \
	    ".weak " __STRING(func) "                                \n" \
	    ".type " __STRING(func) ", #function                     \n" \
	    ".type " __STRING(func##_resolver) ", #function          \n" \
	    __STRING(func) ":             ldr pc, 8b                 \n" \
	    __STRING(func##_resolver) ":  adr ip, 8b                 \n" \
	    "                                push {ip}                  \n" \
	    "                                ldr ip, 7b                 \n" \
	    "                                push {ip}                  \n" \
            "                                ldr ip, [pc]               \n" \
	    "                                ldr pc, [ip]               \n" \
	    "                           .long __l4_external_resolver    \n" \
	   )

#else
#define L4_EXTERNAL_FUNC(func) \
	__asm(".section \".data.l4externals.str\"                         \n" \
	    "9: .string \"" __STRING(func) "\"                       \n" \
	    ".previous                                                  \n" \
	    \
	    ".section \".data.l4externals.symtab\"                      \n" \
	    "7: .long 9b                                                \n" \
	    ".previous                                                  \n" \
	    \
	    ".section \".data.l4externals.jmptbl\"                      \n" \
	    "8: .long " __STRING(func##_resolver) "                  \n" \
	    ".previous                                                  \n" \
	    \
	    ".section \"" __STRING(.text.l4externals.fu##nc) "\"     \n" \
	    ".globl " __STRING(func) "                               \n" \
	    ".weak " __STRING(func) "                                \n" \
	    ".type " __STRING(func) ", @function                     \n" \
	    ".type " __STRING(func##_resolver) ", @function          \n" \
	    __STRING(func) ":            jmp *8b                     \n" \
	    __STRING(func##_resolver) ": push $8b                    \n" \
            "                               push $7b                    \n" \
	    "                               jmp *__l4_external_resolver \n" \
	    ".previous                                                  \n" \
	   )
#endif

#endif /* __INCLUDE__ASM_L4__GENERIC__L4LIB_H__ */
