/**
 * @file ScenarioPlayback.h
 * @brief 게임 · 콘솔이 시나리오를 **재생**하는 창구입니다 — 데모 · 어트랙트 모드, 내장 벤치마크 경로, 튜토리얼 연출, 개발 매크로.
 * @details 테스트 실행(`-scenario`)은 판정이 끝나면 프로세스를 끝내지만, 재생은 프로세스를 그대로 두고 끝을 델리게이트로 알린다. 같은 시나리오 형식 · 단계 · 가상 입력을 쓴다.
 *          재생기(`IScenarioPlayer`)는 위 층의 호스트가 건다(App 의 `AutomationSession` — `AssetIO::bindPackManager` 처럼 위가 아래에 건다). 걸린 재생기가 없으면
 *          (전용 서버 · 자동화를 뺀 구성) `play` 가 false 다. 한 번에 하나만 돈다 — 테스트 실행이 돌고 있어도 false 다.
 *          언리얼 Gauntlet 의 데모 · 어트랙트 루프와 Source 엔진의 `timedemo` · `exec` 매크로를 한 형식으로 받는 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    /** @brief 재생이 끝난 까닭입니다. */
    enum class ScenarioPlayEnd : uint8
    {
        Completed,      ///< 끝까지 돌았다(`<Pass/>` 까지)
        Failed,         ///< 단계가 실패했거나 시나리오를 읽지 못했다(이유는 로그)
        StoppedByHuman, ///< 사람이 키보드 · 마우스를 건드렸다(`_bStopOnHumanInput`)
        Stopped,        ///< `ScenarioPlayback::stop` · `scenario.stop`
    };

    /** @brief 재생 끝 알림입니다. */
    using ScenarioPlayFinishedDelegate = Delegate<void( ScenarioPlayEnd end )>;

    /** @struct ScenarioPlayOptions @brief 재생 하나의 선택입니다. */
    struct ScenarioPlayOptions
    {
        bool                         _bLoop{ false };             ///< 끝까지 돌면 처음부터 다시(어트랙트 루프). 실패하면 멈춘다
        bool                         _bStopOnHumanInput{ false }; ///< 사람이 키보드 · 마우스를 건드리면 멈춘다(데모 · 어트랙트)
        bool                         _bFixedDelta{ false };       ///< 시나리오의 `fixedDelta` 로 고정 프레임 시간을 건다(벤치마크 · 결정적 재생). 끄면 벽시계로 돈다
        ScenarioPlayFinishedDelegate _onFinished{};               ///< 끝날 때 한 번(반복 중에는 멈출 때)
    };

    /** @class IScenarioPlayer @brief 재생을 실제로 하는 쪽(호스트가 건다)입니다. 게임 스레드에서만 부른다. */
    class SW_API IScenarioPlayer
    {
    public:
        virtual ~IScenarioPlayer() = default;
        /** @brief 재생을 시작합니다. 이미 무엇이 돌고 있으면 false 입니다. */
        [[nodiscard]] virtual bool play( const AssetID& scenarioPath, const ScenarioPlayOptions& options ) = 0;
        /** @brief 재생 중이면 멈춥니다(`ScenarioPlayEnd::Stopped`). */
        virtual void stopPlayback() = 0;
        /** @brief 재생 중이면 true 입니다(테스트 실행은 재생이 아니다). */
        virtual bool isPlaying() const = 0;
    };

    /** @struct ScenarioPlayback @brief 걸린 재생기로 넘기는 창구입니다. 게임 스레드에서만 부른다. */
    struct SW_API ScenarioPlayback
    {
        /** @brief 시나리오를 재생합니다. 재생기가 없거나 이미 돌고 있으면 이유를 남기고 false 입니다. */
        [[nodiscard]] static bool play( const AssetID& scenarioPath, const ScenarioPlayOptions& options = {} );
        /** @brief 재생 중이면 멈춥니다. */
        static void stop();
        /** @brief 재생 중이면 true 입니다. */
        static bool isPlaying();
    };
} // namespace sw
