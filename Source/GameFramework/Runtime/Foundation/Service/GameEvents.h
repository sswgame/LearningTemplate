/**
 * @file GameEvents.h
 * @brief EventDispatcher 채널 "game" 의 장르 공통 수명주기 이벤트를 정의합니다.
 * @details 프레임워크가 정해진 시점에 이 이벤트를 낸다.
 *          모두 `GameEventUtil::send` 로 내며, 버스 스레드면 즉시, 아니면 다음 `processEvents` 에서 전달된다.
 *          - `SaveGameSavedEvent` 와 `SaveGameLoadedEvent`: `GameSaveSlots::save` 과 `load`(자동 저장 되돌리기 `AutosaveManager::restoreLatest` · `restoreCheckpoint` 도 같은 적용 길)이 맡긴 슬롯 쓰기 · 읽기가 끝난 뒤 낸다 — 세이브가 다른 씬이면 그 씬이 열려 적용한 뒤다. 성공과 실패 모두 낸다.
 *          - `SceneLoadCompletedEvent`: 게임 이동 씬 로드가 활성 씬이 되었거나 실패한 뒤의 첫 `GameInstance::update` 에서 낸다. 요청은 씬 매니저가 알린다(`SceneManager::onSceneLoadRequested`).
 *
 *          언리얼의 `FCoreUObjectDelegates::PostLoadMapWithWorld` 에 해당한다. 낼 시점이 없는 이벤트(일시정지 등)는 두지 않는다.
 *          다른 채널 이벤트는 킷이 자기 헤더에 둔다(`ActionRoom.h` 의 룸 이벤트). 유닛의 피해는 `DamageEvents.h` 다.
 *          `SceneManager` 를 직접 불러 한 씬 로드는 레벨 이벤트를 내지 않는다. 엔진은 GameFramework 를 모르기 때문이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 채널: EventDispatcher "game"
    // ------------------------------------------------------------------------------
    /** @brief 게임플레이 이벤트가 오가는 채널 이름("game")을 반환합니다. */
    inline hashed_string gameEventChannel()
    {
        static const hashed_string kChannel{ "game" };
        return kChannel;
    }

    // ------------------------------------------------------------------------------
    // 2) 세이브와 로드: GameInstance
    // ------------------------------------------------------------------------------
    /** @brief 게임 상태 저장(`GameSaveSlots::save`)이 끝났음을 알립니다. 실패는 `LoadingScreenController` 가 대화 상자로 알린다. */
    struct SW_GF_API SaveGameSavedEvent final : EventBase
    {
        string _slot;             ///< 쓴(쓰려 한) 로컬 저장 슬롯 이름
        bool   _bSuccess{ true }; ///< 슬롯을 끝까지 썼으면 true
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameSavedEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 상태 불러오기(`GameSaveSlots::load`)가 끝났음을 알립니다. 실패는 `LoadingScreenController` 가 대화 상자로 알린다. 되살린 상태는 컴포넌트가 복원 길로 직접 받는다. */
    struct SW_GF_API SaveGameLoadedEvent final : EventBase
    {
        string _slot;                 ///< 읽은(읽으려 한) 로컬 저장 슬롯 이름
        bool   _bSuccess{ true };     ///< 씬과 상태를 모두 복원했으면 true
        bool   _bSuperseded{ false }; ///< 플레이어가 뒤에 고른 이동(타이틀로 · 다른 슬롯)이 물렸다 — 실패로 알릴 것이 아니다
        SW_DECLARE_GAMEPLAY_EVENT( SaveGameLoadedEvent );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 레벨(씬) 로드: GameInstance
    // ------------------------------------------------------------------------------
    /** @brief 맡긴 씬 로드가 끝났음을 알립니다. 성공이면 그 씬이 이미 활성 씬입니다. 실패는 로딩 화면 컨트롤러(`LoadingScreenController`)가 대화 상자로 알린다. */
    struct SW_GF_API SceneLoadCompletedEvent final : EventBase
    {
        string _levelName;        ///< 요청했던 씬 경로
        bool   _bSuccess{ true }; ///< 활성 씬이 되었으면 true. 읽지 못했거나 뒤 요청에 밀렸으면 false
        SW_DECLARE_GAMEPLAY_EVENT( SceneLoadCompletedEvent );
    };
} // namespace sw
