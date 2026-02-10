#pragma once

/*
 * sakura_lwext4.h – Public header for SakuraEDL's lwext4 integration.
 *
 * When the real lwext4 library is available, include <ext4.h> directly.
 * This header provides a C++-friendly wrapper / forward declarations.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * If the full lwext4 headers are present they will be found via the
 * include path set in CMakeLists.txt.  For now, declare nothing so that
 * the target compiles cleanly as a stub.
 */

#ifdef __cplusplus
}
#endif
