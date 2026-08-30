/**
 * @file SharedGpuTypes.h
 * @brief CPU-side POD mirror of common GPU constant-buffer layouts.
 * @note HLSL / GLSL shaders should keep matching field order and sizes.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
#pragma pack( push, 1 )
	/**
	 * @brief Per-frame globals (time / delta). 16-byte aligned CB friendly.
	 * Shader mirror: float32 timeSeconds, deltaSeconds, frameIndexAsFloat, pad0;
	 */
	struct FrameConstants
	{
		float32 _timeSeconds{ 0.0f };
		float32 _deltaSeconds{ 0.0f };
		float32 _frameIndexAsFloat{ 0.0f };
		float32 _pad0{ 0.0f };
	};

	/**
	 * @brief Per-view camera constants. Row-major float4x4 + camera / screen.
	 * Shader mirror: float4x4 viewProj; float4 cameraPos; float2 screenSize; float2 pad;
	 */
	struct ViewConstants
	{
		float4x4 _viewProj{};
		float4	 _cameraPos{};
		float2	 _screenSize{};
		float2	 _pad{};
	};
#pragma pack( pop )

	static_assert( sizeof( FrameConstants ) == 16, "FrameConstants must be 16 bytes (shader mirror)" );
	static_assert( sizeof( ViewConstants ) == 96, "ViewConstants must be 96 bytes (shader mirror)" );
	static_assert( SW_OFFSET_OF( ViewConstants, _cameraPos ) == 64, "ViewConstants cameraPos offset" );
	static_assert( SW_OFFSET_OF( ViewConstants, _screenSize ) == 80, "ViewConstants screenSize offset" );
} // namespace sw
