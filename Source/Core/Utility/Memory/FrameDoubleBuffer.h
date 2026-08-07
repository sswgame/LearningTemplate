#pragma once
/**
 * @file FrameDoubleBuffer.h
 * @brief 더블 버퍼링 기법이 적용된 프레임 아레나 할당자입니다.
 * @details 프레임 N의 데이터를 프레임 N+1에서 렌더링 스레드가 접근하는 동안 파괴되지 않도록 보호하기 위해 사용됩니다.
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

#include "Core/Utility/Memory/FrameArenaAllocator.h"

namespace sw
{

	/**
	 * @class FrameDoubleBuffer
	 * @brief 2개의 FrameArenaAllocator를 교차로 사용하여 프레임간 데이터 수명 충돌을 방지합니다.
	 */
	class FrameDoubleBuffer
	{
	public:
		/**
		 * @brief 초기화 (내부적으로 2개의 아레나 생성)
		 * @param arenaCapacity 각 아레나의 초기 크기
		 */
		explicit FrameDoubleBuffer( uint64 arenaCapacity = 1024 * 1024 )
			: _arenas{ FrameArenaAllocator( arenaCapacity ), FrameArenaAllocator( arenaCapacity ) }
			, _activeIdx{ 0 }
		{
		}
		~FrameDoubleBuffer() = default;

		/**
		 * @brief 현재 액티브 버퍼에서 메모리를 할당합니다.
		 */
		SW_INLINE void* allocate( uint64 size, uint64 alignment = 16 )
		{
			return _arenas[_activeIdx].allocate( size, alignment );
		}

		/**
		 * @brief 프레임이 끝날 때 호출되어, 이전 버퍼를 비우고 액티브 인덱스를 교체합니다.
		 */
		SW_INLINE void swapAndResetPrevious()
		{
			_activeIdx = 1 - _activeIdx;
			_arenas[_activeIdx].reset();
		}

		/** @brief 현재 할당에 사용 중인 버퍼 인덱스 반환 (0 또는 1) */
		SW_INLINE uint32 getCurrentIndex() const { return _activeIdx; }

		SW_INLINE uint64 getCurrentUsedBytes() const { return _arenas[_activeIdx].getUsedBytes(); }

	private:
		FrameArenaAllocator _arenas[2];
		uint32				_activeIdx = 0;
	};
}
