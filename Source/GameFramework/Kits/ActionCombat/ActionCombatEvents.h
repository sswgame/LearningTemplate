/**
 * @file ActionCombatEvents.h
 * @brief 액션 룸 및 전투 룸 이벤트
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"

namespace sw
{
	/** @brief 액션 룸을 클리어했음을 알립니다. */
	struct RoomClearedEvent final : IEvent
	{
		string				   _mapPath;		   ///< 클리어한 맵
		uint8				   _bBossDefeated : 1; ///< 보스 처치 여부
		[[maybe_unused]] uint8 _reserved	  : 7;

		/** @brief 보스 미처치·예약 비트를 0으로 둡니다. */
		RoomClearedEvent() noexcept
			: _bBossDefeated{ SW_FALSE }
			, _reserved{ 0 } {}

		SW_DECLARE_GAMEPLAY_EVENT( RoomClearedEvent );
	};

	/** @brief 액션 룸에서 플레이어가 패배했음을 알립니다. */
	struct PlayerDefeatedInRoomEvent final : IEvent
	{
		string _returnMapPath; ///< 복귀할 오버월드 맵
		SW_DECLARE_GAMEPLAY_EVENT( PlayerDefeatedInRoomEvent );
	};

	/** @brief 클리어 게이트 잠금이 바뀌었음을 알립니다. */
	struct ClearGateStateChangedEvent final : IEvent
	{
		string				   _zoneId;			///< 존 ID
		uint8				   _bLocked	   : 1; ///< 잠김 여부
		uint8				   _bTriggered : 1; ///< 방 진입 시 닫힘 트리거
		[[maybe_unused]] uint8 _reserved   : 6;

		/** @brief 열림·비트리거·예약 비트를 0으로 둡니다. */
		ClearGateStateChangedEvent() noexcept
			: _bLocked{ SW_FALSE }
			, _bTriggered{ SW_FALSE }
			, _reserved{ 0 } {}

		SW_DECLARE_GAMEPLAY_EVENT( ClearGateStateChangedEvent );
	};
} // namespace sw
