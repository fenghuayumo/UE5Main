// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Used enable/disable optimization for the entire module.
 *
 */
#if UE_BUILD_SHIPPING
	#define WINDOWSPLATFORMFEATURES_DEBUG 0
#else
	#define WINDOWSPLATFORMFEATURES_DEBUG 0
#endif

#if WINDOWSPLATFORMFEATURES_DEBUG
	#define WINDOWSPLATFORMFEATURES_START PRAGMA_DISABLE_OPTIMIZATION
	#define WINDOWSPLATFORMFEATURES_END PRAGMA_ENABLE_OPTIMIZATION
#else
	#define WINDOWSPLATFORMFEATURES_START 
	#define WINDOWSPLATFORMFEATURES_END 
#endif
