#include "catalog_extra.h"

/* sceKernelGetCompiledSdkVersion: published by libkernel, same module/soname
 * as the main Kernel entry in the generated catalog. */
static const StubExport exports_extra_kernel[] = {
    { "sceKernelGetCompiledSdkVersion" },
};

/* sceUserServiceGetForegroundUser: published by libSceUserService, same
 * module/soname as the main UserService entry in the generated catalog. */
static const StubExport exports_extra_user_service[] = {
    { "sceUserServiceGetForegroundUser" },
};

/* libSceGnmDriver: PS5's PS4-compatibility GPU driver module - a separate
 * module from libSceAgc/libSceAgcDriver (the native PS5 graphics API), not
 * an alternate name for it (confirmed: none of these 4 names collide with
 * anything already in the generated Agc/AgcDriver entries). */
static const StubExport exports_extra_gnm_driver[] = {
    { "sceGnmFlushGarlic" },
    { "sceGnmDebugHardwareStatus" },
    { "sceGnmGetProtectionFaultTimeStamp" },
    { "sceGnmEndWorkload" },
};

/* Plain libc functions the generated catalog.c's own exports_C/exports_Posix
 * happened to miss - same module/soname as those (libc.prx), just not part
 * of whatever symbol dump catalog.c was generated from. Found linking
 * BennuGD2's libmod_3d against PS5GL (a large, real C codebase exercising
 * much more of libc than any single ps5link title had before) - link_real
 * reported these as its only unresolved symbols, everything else in a
 * ~55-file real codebase resolved cleanly.
 *
 * CONFIRMED ON HARDWARE (a title calling each in turn, with a log flushed
 * after every single one so a crash mid-list still leaves a clear trail):
 * fseek/ftell/fgetc/ungetc/ferror/fgetpos/fsetpos/rewind/remove/sprintf/
 * frexp/ldexp/sincos/sincosf/tolower/rand/atoi/atof/atoll/strpbrk/strcspn all
 * resolve and run - real dynwriter/NID-catalog behavior, not the "should
 * exist in any BSD libc" guess this list started as. __assert also
 * CONFIRMED: its address resolves to non-NULL (never called - real BSD
 * __assert aborts the process, so only the resolved address is checked).
 *
 * getenv and opendir are NOT in this list anymore - both CONFIRMED BROKEN on
 * hardware (SIGSEGV, rip=0, the same "import never bound to anything real"
 * signature every other unresolved-at-runtime case in this project's history
 * has had). getenv almost certainly because a ps5link title's process has no
 * real environment to look up; opendir/readdir/closedir (the latter two
 * untested - opendir itself never returns, so they were never reached)
 * likely because PS5's libc.prx does not export the POSIX directory-stream
 * wrappers by these names at all, only the lower-level getdents syscall
 * (already catalogued under exports_Posix) that they would normally be built
 * on top of. Do not re-add any of these four without a real fix, not just a
 * NID guess.
 *
 * (Earlier note: __assert had not been tested yet, since the hardware run
 * that would have reached it was blocked by opendir crashing first in the
 * same title. A later run with opendir removed reached and confirmed it -
 * see above.) */
static const StubExport exports_extra_libc[] = {
    { "fseek" }, { "ftell" }, { "fgetc" }, { "ungetc" }, { "ferror" },
    { "fgetpos" }, { "fsetpos" }, { "rewind" }, { "sprintf" },
    { "frexp" }, { "ldexp" }, { "sincos" }, { "sincosf" },
    { "tolower" }, { "rand" }, { "atoi" }, { "atof" }, { "atoll" },
    { "strpbrk" }, { "strcspn" }, { "remove" }, { "__assert" },
};

const StubEntry ps5link_catalog_extra[] = {
    { "libkernel", "libkernel", "libkernel.prx", 0x0101, 0x0001,
      exports_extra_kernel, sizeof(exports_extra_kernel) / sizeof(exports_extra_kernel[0]) },
    { "libSceUserService", "libSceUserService", "libSceUserService.prx", 0x0101, 0x0001,
      exports_extra_user_service, sizeof(exports_extra_user_service) / sizeof(exports_extra_user_service[0]) },
    { "libSceGnmDriver", "libSceGnmDriver", "libSceGnmDriver.prx", 0x0101, 0x0001,
      exports_extra_gnm_driver, sizeof(exports_extra_gnm_driver) / sizeof(exports_extra_gnm_driver[0]) },
    { "libc", "libc", "libc.prx", 0x0101, 0x0001,
      exports_extra_libc, sizeof(exports_extra_libc) / sizeof(exports_extra_libc[0]) },
};

const size_t ps5link_catalog_extra_count =
    sizeof(ps5link_catalog_extra) / sizeof(ps5link_catalog_extra[0]);
