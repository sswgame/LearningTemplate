/**
 * @file GameEvents.h
 * @brief EventDispatcher 채널 "game"의 게임플레이 이벤트
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 채널 — EventDispatcher "game"
	//    워프·전투·세이브는 이 채널로만 발행
	// ------------------------------------------------------------------------------
	/** @brief 게임플레이 이벤트가 오가는 채널 이름("game")을 반환합니다. */
	inline hashed_string gameEventChannel()
	{
		static const hashed_string kChannel{ "game" };
		return kChannel;
	}

	// ------------------------------------------------------------------------------
	// 2) 워프 — 요청 / 클리어 게이트 차단 / 완료
	// ------------------------------------------------------------------------------
	/** @brief 맵 이동을 요청합니다. */
	struct WarpRequestedEvent final : IEvent
	{
		string _mapPath;	 ///< 대상 맵 (Resource 상대)
		int32  _spawnX{ 1 }; ///< 스폰 타일 X
		int32  _spawnY{ 1 }; ///< 스폰 타일 Y
		SW_DECLARE_GAMEPLAY_EVENT( WarpRequestedEvent );
	};

	/** @brief 클리어 게이트 등으로 맵 이동이 막혔음을 알립니다. */
	struct WarpBlockedEvent final : IEvent
	{
		string _zoneId; ///< 활성 존 ID
		string _reason; ///< 차단 이유
		SW_DECLARE_GAMEPLAY_EVENT( WarpBlockedEvent );
	};

	/** @brief 맵 이동이 끝났음을 알립니다. */
	struct WarpCompletedEvent final : IEvent
	{
		string _mapPath;	 ///< 도착 맵
		int32  _spawnX{ 1 }; ///< 스폰 타일 X
		int32  _spawnY{ 1 }; ///< 스폰 타일 Y
		SW_DECLARE_GAMEPLAY_EVENT( WarpCompletedEvent );
	};

	// ------------------------------------------------------------------------------
	// 3) 전투 — 시작 / 종료
	// ------------------------------------------------------------------------------
	/** @brief 전투 시작을 요청합니다. */
	struct BattleRequestedEvent final : IEvent
	{
		string _fromMapPath; ///< 전투 직전 오버월드 맵
		SW_DECLARE_GAMEPLAY_EVENT( BattleRequestedEvent );
	};

	/** @brief 전투가 끝났음을 알립니다. */
	struct BattleEndedEvent final : IEvent
	{
		uint8				   _bPlayerWon : 1; ///< 플레이어 승리
		[[maybe_unused]] uint8 _reserved   : 7;

		/** @brief 패배·예약 비트를 0으로 둡니다. */
		BattleEndedEvent() noexcept
			: _bPlayerWon{ SW_FALSE }
			, _reserved{ 0 } {}

		SW_DECLARE_GAMEPLAY_EVENT( BattleEndedEvent );
	};

	// ------------------------------------------------------------------------------
	// 4) 세이브 — 슬롯 경로만 실어 요청
	// ------------------------------------------------------------------------------
	/** @brief 현재 월드를 파일로 저장하라고 요청합니다. */
	struct SaveRequestedEvent final : IEvent
	{
		string _savePath; ///< 세이브 파일 경로
		SW_DECLARE_GAMEPLAY_EVENT( SaveRequestedEvent );
	};

	/** @brief 파일에서 월드를 불러오라고 요청합니다. */
	struct LoadRequestedEvent final : IEvent
	{
		string _savePath; ///< 세이브 파일 경로
		SW_DECLARE_GAMEPLAY_EVENT( LoadRequestedEvent );
	};

	// ------------------------------------------------------------------------------
	// 5) 액션 룸 — 클리어 / 패배 / 게이트
	// ------------------------------------------------------------------------------
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
	struct ClearGateChangedEvent final : IEvent
	{
		uint8				   _bLocked	 : 1; ///< 잠금이면 워프 차단
		[[maybe_unused]] uint8 _reserved : 7;

		/** @brief 잠금 해제·예약 비트를 0으로 둡니다. */
		ClearGateChangedEvent() noexcept
			: _bLocked{ SW_FALSE }
			, _reserved{ 0 } {}

		SW_DECLARE_GAMEPLAY_EVENT( ClearGateChangedEvent );
	};
} // namespace sw
