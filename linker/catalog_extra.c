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

const StubEntry ps5link_catalog_extra[] = {
    { "libkernel", "libkernel", "libkernel.prx", 0x0101, 0x0001,
      exports_extra_kernel, sizeof(exports_extra_kernel) / sizeof(exports_extra_kernel[0]) },
    { "libSceUserService", "libSceUserService", "libSceUserService.prx", 0x0101, 0x0001,
      exports_extra_user_service, sizeof(exports_extra_user_service) / sizeof(exports_extra_user_service[0]) },
    { "libSceGnmDriver", "libSceGnmDriver", "libSceGnmDriver.prx", 0x0101, 0x0001,
      exports_extra_gnm_driver, sizeof(exports_extra_gnm_driver) / sizeof(exports_extra_gnm_driver[0]) },
};

const size_t ps5link_catalog_extra_count =
    sizeof(ps5link_catalog_extra) / sizeof(ps5link_catalog_extra[0]);
