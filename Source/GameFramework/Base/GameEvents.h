/**
 * @file GameEvents.h
 * @brief EventDispatcher 채널 "game" 의 범용 게임플레이 · 엔진 수명주기 이벤트입니다.
 *
 * @warning **프레임워크는 이 이벤트들을 하나도 발행하지 않습니다.** 2026-09-18 확인 기준
 *          열두 종 모두, 그리고 `gameEventChannel()` 까지 저장소 안에 **발행자도 구독자도
 *          없습니다**(2026-09-24 에 다시 봐도 같습니다). 이름만 보고 "세이브를 요청하면 프레임워크가
 *          `SaveRequestedEvent` 를 쏴 주겠지" 라고 기대하면 **영원히 오지 않는 것을 기다리게 됩니다.**
 *
 *          여기 있는 것은 게임들이 **자기들끼리** 주고받을 때 이름이 갈리지 않도록 모아 둔
 *          어휘일 뿐입니다. 쏘는 것도 받는 것도 게임 코드가 합니다. 프레임워크가 실제로
 *          발행하게 만들 생각이라면 그 자리를 정하는 것이 먼저입니다(백로그 1-0c 참고).
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
    // 2) 세이브 / 로드 이벤트
    // ------------------------------------------------------------------------------
    /** @brief 현재 월드 · 게임 상태 저장을 요청합니다. */
    struct SaveRequestedEvent final : IEvent
    {
        string _savePath; ///< 세이브 파일 경로
        SW_DECLARE_GAMEPLAY_EVENT( SaveRequestedEvent );
    };

    /** @brief 저장된 게임 데이터 로드를 요청합니다. */
    struct LoadRequestedEvent final : IEvent
    {
        string _savePath; ///< 세이브 파일 경로
        SW_DECLARE_GAMEPLAY_EVENT( LoadRequestedEvent );
    };

    /** @brief 게임 데이터 저장이 완료되었음을 알립니다. */
    struct SaveCompletedEvent final : IEvent
    {
        string _savePath;
        bool   _bSuccess{ true };
        SW_DECLARE_GAMEPLAY_EVENT( SaveCompletedEvent );
    };

    /** @brief 게임 데이터 로드가 완료되었음을 알립니다. */
    struct LoadCompletedEvent final : IEvent
    {
        string _savePath;
        bool   _bSuccess{ true };
        SW_DECLARE_GAMEPLAY_EVENT( LoadCompletedEvent );
    };

    // ------------------------------------------------------------------------------
    // 3) 레벨 / 씬 / 전환 라이프사이클 이벤트
    // ------------------------------------------------------------------------------
    /** @brief 레벨 · 씬 로드를 요청합니다. */
    struct LevelLoadRequestedEvent final : IEvent
    {
        string _levelName; ///< 로드할 레벨/씬 경로
        SW_DECLARE_GAMEPLAY_EVENT( LevelLoadRequestedEvent );
    };

    /** @brief 레벨 · 씬 로드가 완료되었음을 알립니다. */
    struct LevelLoadCompletedEvent final : IEvent
    {
        string _levelName;
        bool   _bSuccess{ true };
        SW_DECLARE_GAMEPLAY_EVENT( LevelLoadCompletedEvent );
    };

    /** @brief 씬 전환 시작을 알립니다. */
    struct SceneTransitionRequestedEvent final : IEvent
    {
        string  _targetScene;
        float32 _duration{ 0.35f };
        SW_DECLARE_GAMEPLAY_EVENT( SceneTransitionRequestedEvent );
    };

    /** @brief 씬 전환이 완료되었음을 알립니다. */
    struct SceneTransitionCompletedEvent final : IEvent
    {
        string _currentScene;
        SW_DECLARE_GAMEPLAY_EVENT( SceneTransitionCompletedEvent );
    };

    // ------------------------------------------------------------------------------
    // 4) 게임 상태 라이프사이클 이벤트 (일시정지, 게임오버)
    // ------------------------------------------------------------------------------
    /** @brief 게임이 일시정지되었음을 알립니다. */
    struct GamePausedEvent final : IEvent
    {
        SW_DECLARE_GAMEPLAY_EVENT( GamePausedEvent );
    };

    /** @brief 게임 일시정지가 해제되었음을 알립니다. */
    struct GameResumedEvent final : IEvent
    {
        SW_DECLARE_GAMEPLAY_EVENT( GameResumedEvent );
    };

    /** @brief 게임오버 상태가 되었음을 알립니다. */
    struct GameOverEvent final : IEvent
    {
        string _reason;
        SW_DECLARE_GAMEPLAY_EVENT( GameOverEvent );
    };

    /** @brief 임의의 커스텀 문자열 명령 · 데이터를 전달하는 범용 이벤트입니다. */
    struct CustomGameEvent final : IEvent
    {
        hashed_string _eventType;
        string        _payload;
        SW_DECLARE_GAMEPLAY_EVENT( CustomGameEvent );
    };
} // namespace sw
