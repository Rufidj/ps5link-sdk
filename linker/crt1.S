/* Real process entry point (_start) for a PS5 title launched by the
 * console's own kernel module loader - NOT the payload/elfldr bootstrap.
 * Sequence ported from SharpProspero.Link/CrtEmitter.cs's StartCode
 * (confirmed: _init_env and atexit are real libc.prx exports, present in
 * the ported catalog - not .NET-runtime-specific, so this is a direct,
 * faithful port rather than an approximation).
 *
 * Deliberately minimal for this phase: _init/_fini are empty stubs (no
 * .init_array walking) since PS5SDK has no C++ global constructors or
 * __attribute__((constructor)) functions yet. Add a real init-array walker
 * here if that ever changes - see DynamicWriter.cs's BuildInitWalker for
 * the reference behavior.
 *
 * This is just another object file to ps5link: _init_env/atexit/exit are
 * resolved as catalog imports exactly like any other function a PS5SDK .c
 * file calls; main/_init/_fini are resolved as local symbols within this
 * same link, through the identical code path.
 */
    .text
    .globl _start
    .globl _init
    .globl _fini

_start:
    .cfi_startproc
    push   %rbp
    mov    %rsp, %rbp
    push   %r15
    push   %r14
    push   %rbx
    push   %rax                    /* keep the stack 16-byte aligned across the calls below */

    mov    (%rdi), %r14d           /* argc, from the loader's parameter block */
    mov    %rsi, %rbx              /* the loader-provided teardown routine, run via atexit below */
    lea    8(%rdi), %r15           /* argv */

    call   _init_env                /* rdi still holds the parameter block - _init_env wants it as-is */

    mov    %rbx, %rdi
    call   atexit                   /* register the loader's teardown to run at process exit */

    lea    _fini(%rip), %rdi
    call   atexit                   /* register this module's own teardown */

    call   _init                    /* constructors (a no-op stub for now, see file header) */

    mov    %r14d, %edi
    mov    %r15, %rsi
    xor    %edx, %edx
    call   main

    mov    %eax, %edi
    call   exit
    ud2
    .cfi_endproc
    .size _start, . - _start

/* No global constructors/destructors yet - see file header. */
_init:
    ret
_fini:
    ret
