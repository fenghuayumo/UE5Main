#if !defined(PLATFORM_BUILDS_MIMALLOC)
#	define PLATFORM_BUILDS_MIMALLOC 0
#endif

#if PLATFORM_BUILDS_MIMALLOC

#define MI_PADDING 0
#define MI_TSAN 0
#define MI_OSX_ZONE 0
#define TARGET_IOS_IPHONE 0
#define TARGET_IOS_SIMULATOR 0

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4668) // Avoid undefined __cplusplus warnings in older versions
#endif

#include "ThirdParty/mimalloc/src/static.c"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // PLATFORM_BUILDS_MIMALLOC
