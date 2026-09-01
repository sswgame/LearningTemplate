/**
 * @file InputSnapshot.h
 * @brief 롤백 넷코드 및 리플레이 재생을 위한 프레임 틱 입력 스냅샷 및 링버퍼
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Math/Math.h"

namespace sw
{
	/**
	 * @struct InputSnapshot
	 * @brief 단일 프레임(틱)의 버튼 비트마스크, 2D 벡터 및 아날로그 트리거 압력 스냅샷
	 */
	struct SW_API InputSnapshot
	{
		uint32	_tickNumber{ 0 };
		uint64	_buttonMask{ 0 };		   ///< 최대 64개 액션/버튼 눌림 비트마스크
		float2	_moveVector{ 0.0f, 0.0f }; ///< 이동 2D 벡터
		float2	_lookVector{ 0.0f, 0.0f }; ///< 시점 2D 벡터
		float32 _leftTrigger{ 0.0f };	   ///< LT 아날로그 압력 (0.0 ~ 1.0)
		float32 _rightTrigger{ 0.0f };	   ///< RT 아날로그 압력 (0.0 ~ 1.0)

		/** @brief 바이너리 버퍼에 직렬화합니다 (성공 시 기록된 바이트 수 반환). */
		uint32 serialize( uint8* pOutBuffer, uint32 bufferSize ) const;

		/** @brief 바이너리 버퍼에서 역직렬화합니다. */
		bool deserialize( const uint8* pBuffer, uint32 bufferSize );
	};

	/**
	 * @class InputHistoryBuffer
	 * @brief 최근 N개 틱의 입력을 보관하는 롤백 및 리플레이 전용 순환 링버퍼
	 */
	class SW_API InputHistoryBuffer
	{
	public:
		static constexpr size_t kDefaultCapacity = 256; ///< 약 4초 분량 (60Hz 기준)

		InputHistoryBuffer();

		/** @brief 현재 틱 스냅샷을 링버퍼에 기록합니다. */
		void recordSnapshot( const InputSnapshot& snapshot );

		/** @brief 특정 틱 번호의 스냅샷을 조회합니다 (범위 밖이면 nullptr). */
		const InputSnapshot* getSnapshot( uint32 tickNumber ) const;

		/** @brief 가장 최근에 기록된 스냅샷을 반환합니다. */
		const InputSnapshot* getLatestSnapshot() const;

		/** @brief 링버퍼를 비웁니다. */
		void clear();

		/** @brief 현재 기록된 스냅샷 개수를 반환합니다. */
		size_t getCount() const { return _count; }

	private:
		array<InputSnapshot, kDefaultCapacity> _arrHistory;
		size_t								   _writeIndex;
		size_t								   _count;
		uint32								   _latestTick;
	};
} // namespace sw
