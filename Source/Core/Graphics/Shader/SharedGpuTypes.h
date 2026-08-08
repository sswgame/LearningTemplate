#pragma once
/**
 * @file SharedGpuTypes.h
 * @brief CPU-side POD mirror of common GPU constant-buffer layouts.
 * @note HLSL / GLSL shaders should keep matching field order and sizes.
 */
#include "Core/Common/Types.h"

#include <cstddef>

namespace sw
{
#pragma pack( push, 1 )
	/**
	 * @brief Per-frame globals (time / delta). 16-byte aligned CB friendly.
	 * Shader mirror: float timeSeconds, deltaSeconds, frameIndexAsFloat, pad0;
	 */
	struct FrameConstants
	{
		float32 _timeSeconds	  = 0.0f;
		float32 _deltaSeconds	  = 0.0f;
		float32 _frameIndexAsFloat = 0.0f;
		float32 _pad0			  = 0.0f;
	};

	/**
	 * @brief Per-view camera constants. Row-major float4x4 + camera / screen.
	 * Shader mirror: float4x4 viewProj; float4 cameraPos; float2 screenSize; float2 pad;
	 */
	struct ViewConstants
	{
		float32 _viewProj[16]{};
		float32 _cameraPos[4]{};
		float32 _screenSize[2]{};
		float32 _pad[2]{};
	};
#pragma pack( pop )

	static_assert( sizeof( FrameConstants ) == 16, "FrameConstants must be 16 bytes (shader mirror)" );
	static_assert( sizeof( ViewConstants ) == 96, "ViewConstants must be 96 bytes (shader mirror)" );
	static_assert( offsetof( ViewConstants, _cameraPos ) == 64, "ViewConstants cameraPos offset" );
	static_assert( offsetof( ViewConstants, _screenSize ) == 80, "ViewConstants screenSize offset" );
} // namespace sw
