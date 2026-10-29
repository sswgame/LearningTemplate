/**
 * @file AutomationSceneSteps.cpp
 * @brief 시나리오가 씬과 연출을 다루는 단계입니다 — 데모 · 어트랙트 · 튜토리얼 재생(`ScenarioPlayback`)이 쓰는 것.
 * @details - `<LoadScene path="game/x/maps/a.scene.xml" timeoutFrames="600"/>`: 씬 로드를 요청하고 그 씬이 활성이 될 때까지 시나리오 시계를 붙잡는다(못 열면 실패).
 *          - `<PlaySequence object="Intro"/>`: 활성 씬의 그 이름 오브젝트에 붙은 시퀀스(`SequencePlayerComponent`)를 처음부터 재생한다.
 *          - `<WaitSequence object="Intro" timeoutFrames="1800"/>`: 그 시퀀스가 끝날 때까지 붙잡는다(언리얼 Sequencer 의 Play → Wait 같은 쌍).
 *          시퀀스 안에서 게임 행동을 끼우는 쪽(시퀀서 이벤트 트랙이 시나리오를 부르는 것)은 `ScenarioPlayback::play` 를 부른다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationSceneSteps" );

    namespace
    {
        struct AutomationSceneStepsInternal
        {
            static constexpr uint32 kDefaultLoadTimeoutFrames     = 600;
            static constexpr uint32 kDefaultSequenceTimeoutFrames = 3600;

            /** @brief `LoadScene` 이 요청한 씬 로드의 답입니다 — 이번 실행의 붙잡은 단계 상태라 실행기가 소유하고, 단계가 놓이거나 실행이 끝나면 지운다. */
            struct PendingLoad final : IAutomationStepState
            {
                explicit PendingLoad( TaskFuture<Scene*> future )
                    : _future{ std::move( future ) }
                {
                }

                TaskFuture<Scene*> _future;
            };

            static uint32 readTimeoutFrames( const AutomationStep& step, uint32 defaultFrames )
            {
                const string* pText = step.findAttribute( "timeoutFrames" );
                int64         value = 0;
                return pText != nullptr && StringUtil::parseInt64( *pText, value ) && value > 0 ? static_cast<uint32>( value ) : defaultFrames;
            }

            static bool validateTimeout( const AutomationStep& step, string& outError )
            {
                const string* pText = step.findAttribute( "timeoutFrames" );
                int64         value = 0;
                if ( pText != nullptr && ( StringUtil::parseInt64( *pText, value ) == false || value <= 0 ) )
                {
                    outError = step.describe() + ": timeoutFrames must be a positive frame count";
                    return false;
                }
                return true;
            }

            static bool validateLoadScene( const AutomationStep& step, string& outError )
            {
                return step.hasRequiredAttributes( { "path", "timeoutFrames" }, { "path" }, outError ) && validateTimeout( step, outError );
            }

            static bool validateSequenceStep( const AutomationStep& step, string& outError )
            {
                return step.hasRequiredAttributes( { "object", "timeoutFrames" }, { "object" }, outError ) && validateTimeout( step, outError );
            }

            static bool runNothing( AutomationStepContext& context, const AutomationStep& step )
            {
                (void)context;
                (void)step;
                return true; // 일은 붙잡기 함수가 했다
            }

            static AutomationStepHold holdLoadScene( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
            {
                // 상태는 맡긴 이 단계만 만든다 — 같은 타입으로 되돌려 읽는다.
                PendingLoad* pPending = static_cast<PendingLoad*>( context.findHeldStepState( step ) );
                if ( pPending == nullptr )
                {
                    TaskFuture<Scene*> future = engine::getSubsystem<SceneManager>().requestLoadFuture( *step.findAttribute( "path" ), SceneLoadKind::Travel );
                    if ( future.isValid() == false )
                    {
                        context.recordFailure( step, "the scene load could not be requested" );
                        return AutomationStepHold::Run;
                    }
                    pPending = static_cast<PendingLoad*>( &context.setHeldStepState( step, make_unique<PendingLoad>( std::move( future ) ) ) );
                }
                const bool bLoaded = pPending->_future.isReady() && pPending->_future.get() != nullptr &&
                                     engine::getSubsystem<SceneManager>().getActiveScene() == pPending->_future.get();
                // Run 이면 실행기가 상태를 지운다 — 그 뒤로 pPending 을 읽지 않는다.
                const AutomationStepHold hold = context.holdUntilReady( step, input, bLoaded, readTimeoutFrames( step, kDefaultLoadTimeoutFrames ) );
                if ( hold == AutomationStepHold::Run && bLoaded == false )
                    context.recordFailure( step, "the scene did not become active in time (or failed to load - see the log)" );
                return hold;
            }

            static SequencePlayerComponent* findSequence( AutomationStepContext& context, const AutomationStep& step )
            {
                GameObjectManager* pManager = context.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string{ *step.findAttribute( "object" ) } ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
            }

            static bool runPlaySequence( AutomationStepContext& context, const AutomationStep& step )
            {
                SequencePlayerComponent* pSequence = findSequence( context, step );
                if ( pSequence == nullptr )
                {
                    context.recordFailure( step, "no object with that name and a SequencePlayerComponent in the active scene" );
                    return true;
                }
                pSequence->play();
                return true;
            }

            static AutomationStepHold holdWaitSequence( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
            {
                SequencePlayerComponent* pSequence = findSequence( context, step );
                const bool               bDone     = pSequence == nullptr || pSequence->isPlaying() == false;
                const AutomationStepHold hold      = context.holdUntilReady( step, input, bDone, readTimeoutFrames( step, kDefaultSequenceTimeoutFrames ) );
                if ( hold == AutomationStepHold::Run && ( pSequence == nullptr || bDone == false ) )
                    context.recordFailure( step, pSequence == nullptr ? "no object with that name and a SequencePlayerComponent in the active scene"
                                                                      : "the sequence was still playing when the wait timed out" );
                return hold;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP_WITH_HOOKS( sceneLoadScene, "LoadScene", &AutomationSceneStepsInternal::runNothing, &AutomationSceneStepsInternal::validateLoadScene, nullptr,
                                   &AutomationSceneStepsInternal::holdLoadScene, false );
    SW_AUTOMATION_STEP( scenePlaySequence, "PlaySequence", &AutomationSceneStepsInternal::runPlaySequence, &AutomationSceneStepsInternal::validateSequenceStep, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( sceneWaitSequence, "WaitSequence", &AutomationSceneStepsInternal::runNothing, &AutomationSceneStepsInternal::validateSequenceStep,
                                   nullptr, &AutomationSceneStepsInternal::holdWaitSequence, false );
} // namespace sw
