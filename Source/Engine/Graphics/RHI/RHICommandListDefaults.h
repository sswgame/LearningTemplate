/**
 * @file RHICommandListDefaults.h
 * @brief IRHICommandList / 디바이스 리플레이 공유 기본 구현
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 멀티 인디렉트 — countBuffer 없이 drawIndirect 루프
	// ------------------------------------------------------------------------------
	/**
	 * @brief 기본 멀티 드로우: drawIndirect를 반복합니다 (countBuffer 무시).
	 * @tparam DrawFn void(RHIBufferHandle, uint32)
	 */
	template <typename DrawFn>
	void defaultMultiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
								   RHIBufferHandle countBuffer, uint32 countBufferOffset, DrawFn&& drawIndirectFn )
	{
		(void)countBuffer;
		(void)countBufferOffset;
		for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
		{
			drawIndirectFn( argumentBuffer, argumentBufferOffset + commandIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
		}
	}
} // namespace sw
