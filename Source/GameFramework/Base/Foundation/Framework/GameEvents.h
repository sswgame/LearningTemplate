/**
 * @file GameEvents.h
 * @brief EventDispatcher 채널 "game" 의 장르 공통 수명주기 이벤트를 정의합니다.
 * @details 프레임워크가 정해진 자리에서 이 이벤트를 낸다.
 *          모두 `GameEventUtil::send` 로 내며, 버스 스레드면 그 자리에서, 아니면 다음 `processEvents` 에서 전달된다.
 *          - `SaveGameSavedEvent` 와 `SaveGameLoadedEvent`: `GameInstanceBase::saveStateToFile` 과 `loadStateFromFile` 이 끝난 자리에서 낸다. 성공과 실패 모두 낸다.
 *          - `SceneLoadRequestedEvent`: `GameInstanceBase::requestFirstScene` 과 `requestEntranceScene` 이 씬 로드를 맡긴 자리에서 낸다.
 *          - `SceneLoadCompletedEvent`: 그 로드가 활성 씬이 되었거나 실패한 뒤의 첫 `GameInstanceBase::update` 에서 낸다.
 *
 *          언리얼의 `FCoreUObjectDelegates::PreLoadMap` 과 `PostLoadMapWithWorld` 에 해당한다. 낼 자리가 없는 이벤트(일시정지 등)는 두지 않는다.
 *          다른 채널 이벤트는 킷이 자기 헤더에 둔다(`ActionCombatEvents.h` 의 피해와 룸 이벤트).
 *          `SceneManager` 를 직접 불러 한 씬 로드는 레벨 이벤트를 내지 않는다. 엔진은 GameFramework 를 모르기 때문이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

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
    struct SW_GF_API SaveGameSavedEvent final : IEvent
    {
        string _savePath;         ///< 쓴(쓰려 한) 세이브 파일 경로
        bool   _bSuccess{ true }; ///< 파일을 끝까지 썼으면 true
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameSavedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 상태 불러오기(`GameInstanceBase::loadStateFromFile`)가 끝났음을 알립니다. */
    struct SW_GF_API SaveGameLoadedEvent final : IEvent
    {
        string _savePath;         ///< 읽은(읽으려 한) 세이브 파일 경로
        bool   _bSuccess{ true }; ///< 씬과 상태를 모두 복원했으면 true
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameLoadedEvent );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 레벨(씬) 로드 — GameInstanceBase
    // ------------------------------------------------------------------------------
    /** @brief 프레임워크가 씬 로드를 맡겼음을 알립니다. 알림일 뿐 요청 명령이 아니며, 받아서 로드하는 쪽은 없습니다. */
    struct SW_GF_API SceneLoadRequestedEvent final : IEvent
    {
        string _levelName; ///< 로드할 씬 경로
        SW_DECLARE_GAMEPLAY_EVENT( SceneLoadRequestedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 맡긴 씬 로드가 끝났음을 알립니다. 성공이면 그 씬이 이미 활성 씬입니다. */
    struct SW_GF_API SceneLoadCompletedEvent final : IEvent
    {
        string _levelName;        ///< 요청했던 씬 경로
        bool   _bSuccess{ true }; ///< 활성 씬이 되었으면 true. 읽지 못했거나 뒤 요청에 밀렸으면 false
        SW_DECLARE_GAMEPLAY_EVENT( SceneLoadCompletedEvent );
    };
} // namespace sw
