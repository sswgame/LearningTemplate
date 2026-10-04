/**
 * @file GameEvents.h
 * @brief EventDispatcher 채널 "game" 의 장르 공통 수명주기 이벤트입니다 — 프레임워크가 그 자리에서 냅니다.
 *
 * @details 이벤트마다 내는 곳이 정해져 있습니다(모두 `GameEventUtil::send` — 버스 스레드면 그 자리에서, 아니면 다음 `processEvents`):
 *          - `SaveGameSavedEvent` · `SaveGameLoadedEvent` — `GameInstanceBase::saveStateToFile` · `loadStateFromFile` 이 끝난 자리(성공 · 실패 모두).
 *          - `SceneLoadRequestedEvent` — `GameInstanceBase::requestFirstScene` · `requestEntranceScene` 이 씬 로드를 맡긴 자리.
 *          - `SceneLoadCompletedEvent` — 그 로드가 활성 씬이 되었거나 실패한 뒤의 첫 `GameInstanceBase::update`.
 *          - `GamePausedEvent` · `GameResumedEvent` — `GameModeStateMachine` 이 `GameModes::paused()` 로 들어가거나 거기서 나간 자리.
 *          언리얼 `FCoreUObjectDelegates::PreLoadMap` · `PostLoadMapWithWorld`, `AGameModeBase::SetPause` · `ClearPause` 와 같은 자리입니다.
 *          다른 채널 이벤트는 킷이 자기 헤더에 둡니다(`ActionCombatEvents.h` 의 피해 · 룸 이벤트).
 *          `SceneManager` 를 직접 부른 씬 로드는 레벨 이벤트를 내지 않습니다 — 엔진은 GameFramework 를 모릅니다.
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
    // ------------------------------------------------------------------------------
    /** @brief 게임플레이 이벤트가 오가는 채널 이름("game")을 반환합니다. */
    inline hashed_string gameEventChannel()
    {
        static const hashed_string kChannel{ "game" };
        return kChannel;
    }

    // ------------------------------------------------------------------------------
    // 2) 세이브 / 로드 — GameInstanceBase
    // ------------------------------------------------------------------------------
    /** @brief 게임 상태 저장(`GameInstanceBase::saveStateToFile`)이 끝났음을 알립니다. */
    struct SaveGameSavedEvent final : IEvent
    {
        string _savePath;         ///< 쓴(쓰려 한) 세이브 파일 경로
        bool   _bSuccess{ true }; ///< 파일을 끝까지 썼으면 true
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameSavedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 상태 불러오기(`GameInstanceBase::loadStateFromFile`)가 끝났음을 알립니다. */
    struct SaveGameLoadedEvent final : IEvent
    {
        string _savePath;         ///< 읽은(읽으려 한) 세이브 파일 경로
        bool   _bSuccess{ true }; ///< 씬과 상태를 모두 되살렸으면 true
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameLoadedEvent );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 레벨(씬) 로드 — GameInstanceBase
    // ------------------------------------------------------------------------------
    /** @brief 프레임워크가 씬 로드를 맡겼음을 알립니다(알림이지 요청 명령이 아닙니다 — 받아서 로드하는 쪽은 없습니다). */
    struct SceneLoadRequestedEvent final : IEvent
    {
        string _levelName; ///< 로드할 씬 경로
        SW_DECLARE_GAMEPLAY_EVENT( SceneLoadRequestedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 맡긴 씬 로드가 끝났음을 알립니다. 성공이면 그 씬이 이미 활성 씬입니다. */
    struct SceneLoadCompletedEvent final : IEvent
    {
        string _levelName;        ///< 요청했던 씬 경로
        bool   _bSuccess{ true }; ///< 활성 씬이 되었으면 true. 읽지 못했거나 뒤 요청에 밀렸으면 false
        SW_DECLARE_GAMEPLAY_EVENT( SceneLoadCompletedEvent );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 4) 일시정지 — GameModeStateMachine
    // ------------------------------------------------------------------------------
    /** @brief 게임 모드가 `GameModes::paused()` 로 들어갔음을 알립니다. */
    struct GamePausedEvent final : IEvent
    {
        SW_DECLARE_GAMEPLAY_EVENT( GamePausedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 모드가 `GameModes::paused()` 에서 나갔음을 알립니다(다른 모드로 · 리셋 · 핸들러 해제 모두). */
    struct GameResumedEvent final : IEvent
    {
        SW_DECLARE_GAMEPLAY_EVENT( GameResumedEvent );
    };
} // namespace sw
