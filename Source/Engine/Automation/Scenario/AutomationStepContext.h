/**
 * @file AutomationStepContext.h
 * @brief 시나리오 단계 함수가 실행기에서 쓰는 것만 담은 좁은 문맥 인터페이스를 정의합니다.
 * @details 단계를 등록하는 쪽(UI · GameFramework · Editor · App)은 이 헤더만 봅니다. 로그 게이트 · 보고 · 종료 코드(`AutomationRunner.h`)를 끌어오지 않는다.
 *          단계가 시나리오를 끝낼 때도 종료 코드(`AutomationOutcome`)가 아니라 단계 쪽 말(`AutomationStepEnd`)로 말한다 — 어떤 코드로 번역할지는 실행기가 정한다.
 *          실행기(`AutomationRunner`)가 구현하고, 테스트는 가짜 문맥으로 단계 함수를 실행기 없이 부를 수 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    enum class AutomationStepHold : uint8;

    struct AutomationStep;

    class GameObjectManager;
    class AutomationFrameStats;
    class InputManager;
    class VirtualInputScript;

    /** @brief 단계가 시나리오를 바로 끝낼 때의 까닭입니다. 실행기가 종료 코드로 번역한다. */
    enum class AutomationStepEnd : uint8
    {
        Pass, ///< 통과로 끝냅니다(실패가 적혀 있으면 실패가 됩니다).
        Fail, ///< 실패로 끝냅니다.
        Skip, ///< 이 기계에서 돌 수 없어 건너뜀으로 끝냅니다.
    };

    /**
     * @class IAutomationStepState
     * @brief 시계를 붙잡는 단계 하나가 프레임을 넘어 드는 상태입니다(요청한 씬 로드의 답 …). 단계가 제 타입으로 상속해 문맥에 맡긴다.
     * @details 실행기가 이번 실행의 것으로 소유하고, 그 단계가 놓이면(`holdUntilReady` 가 Run) · 실행이 끝나면 지운다 — 단계 함수의 정적 목록이 실행을 넘어 남지 않는다.
     *          언리얼 latent 명령이 제 상태를 명령 객체에 들고 대기열이 그 수명을 쥐는 것과 같은 자리다.
     */
    class IAutomationStepState
    {
    public:
        virtual ~IAutomationStepState() = default;
    };

    /**
     * @class AutomationStepContext
     * @brief 단계 함수(`AutomationStepFunction` · `AutomationStepPrepareFunction` · `AutomationStepHoldFunction`)가 받는 문맥입니다. 게임 스레드에서만 씁니다.
     */
    class SW_API AutomationStepContext
    {
    public:
        virtual ~AutomationStepContext() = default;

        /** @brief 단언 실패를 적습니다(시나리오는 계속 진행하고 끝에서 실패가 됩니다). */
        virtual void recordFailure( const AutomationStep& step, string_view message ) = 0;
        /**
         * @brief 기다리는 대상이 아직이면(@p bReady 가 false) 시나리오 시계를 한 프레임 붙잡습니다(`WaitUntil` 과 같다).
         * @return 붙잡았으면 Hold, 준비됐거나 @p timeoutFrames 를 다 기다렸으면 Run 입니다(그 단계가 돌며 실패를 적는다).
         */
        virtual AutomationStepHold holdUntilReady( const AutomationStep& step, InputManager& input, bool bReady, uint32 timeoutFrames ) = 0;
        /**
         * @brief 붙잡고 있는 단계 @p step 의 상태입니다(`setHeldStepState` 로 맡긴 것). 맡긴 적이 없거나 다른 단계의 것이면 nullptr 입니다.
         * @details 맡긴 단계만 같은 타입으로 되돌려 읽는다(`static_cast`) — 상태 칸은 붙잡은 단계 하나의 것이다.
         */
        virtual IAutomationStepState* findHeldStepState( const AutomationStep& step ) const = 0;
        /** @brief 붙잡고 있는 단계 @p step 의 상태를 맡깁니다(앞 것은 지운다). 그 단계가 놓이거나 실행이 끝나면 실행기가 지운다. 맡긴 상태를 돌려줍니다. */
        virtual IAutomationStepState& setHeldStepState( const AutomationStep& step, unique_ptr<IAutomationStepState> state ) = 0;
        /** @brief 시나리오를 바로 끝냅니다. 이미 끝났으면 무시합니다. */
        virtual void endScenario( AutomationStepEnd end, string_view reason ) = 0;

        /** @brief 이 시나리오의 산출물 폴더입니다. */
        virtual const string& getOutputDirectory() const = 0;
        /** @brief 상대 경로면 산출물 폴더 아래로 둡니다(단계가 파일을 쓸 때). */
        virtual string resolveOutputPath( string_view file ) const = 0;
        /** @brief 지금 시나리오 프레임(시작 전이면 0)입니다. */
        virtual uint32 getFrameIndex() const = 0;
        /** @brief 초를 시나리오 프레임 수로 바꿉니다(고정 프레임 시간으로 반올림). */
        virtual uint32 convertSecondsToFrames( float32 seconds ) const = 0;

        /** @brief 활성 씬의 오브젝트 매니저입니다(없으면 nullptr). */
        virtual GameObjectManager* findActiveObjectManager() const = 0;
        /** @brief 입력 단계가 이벤트를 넣는 가상 입력 원천입니다. */
        virtual VirtualInputScript& getInputScript() = 0;
        /** @brief 시나리오를 돌리는 입력 관리자입니다(시작 프레임부터, 그 전에는 nullptr). */
        virtual InputManager* getInputManager() const = 0;
        /** @brief 프레임 시간 기록기입니다(`BeginFrameStats` · `EndFrameStats` — 실행기가 프레임 끝마다 쌓는다). */
        virtual AutomationFrameStats& getFrameStats() = 0;
    };
} // namespace sw
