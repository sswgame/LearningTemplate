/**
 * @file AutomationSceneSteps.cpp
 * @brief 시나리오가 씬과 연출을 다루는 단계입니다 — 데모 · 어트랙트 · 튜토리얼 재생(`ScenarioPlayback`)이 쓰는 것.
 * @details - `<LoadScene path="game/x/maps/a.scene.xml" timeoutFrames="600"/>`: 씬 로드를 요청하고 그 씬이 활성이 될 때까지 시나리오 시계를 붙잡는다(못 열면 실패).
 *          - `<PlaySequence object="Intro"/>`: 활성 씬의 그 이름 오브젝트에 붙은 시퀀스(`SequencePlayerComponent`)를 처음부터 재생한다.
 *          - `<WaitSequence object="Intro" timeoutFrames="1800"/>`: 그 시퀀스가 끝날 때까지 붙잡는다(언리얼 Sequencer 의 Play → Wait 같은 쌍).
 *          시퀀스 안에서 게임 행동을 끼우는 쪽(시퀀서 이벤트 트랙이 시나리오를 부르는 것)은 `ScenarioPlayback::play` 를 부른다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

namespace sw
{
    namespace
    {
        struct AutomationSceneStepsInternal
        {
            /** @brief `LoadScene` 이 요청한 씬 로드의 답입니다 — 이번 실행의 붙잡은 단계 상태라 실행기가 소유하고, 단계가 놓이거나 실행이 끝나면 지운다. */
            struct PendingLoad final : IAutomationStepState
            {

                TaskFuture<Scene*> _future;
                explicit PendingLoad( TaskFuture<Scene*> future )
                    : _future{ std::move( future ) }
                {
                }
            };

            static bool runNothing( AutomationStepContext& context, const AutomationStep& step )
            {
                (void)context;
                (void)step;
                return true; // 일은 붙잡기 함수가 했다
            }

            static AutomationStepHold holdLoadScene( AutomationStepContext& context, const AutomationStep& step, InputManager& input )
            {
                const AutomationLoadSceneParams params = step.readValidatedProperties<AutomationLoadSceneParams>();
                // 상태는 맡긴 이 단계만 만든다 — 같은 타입으로 되돌려 읽는다.
                PendingLoad* pPending = static_cast<PendingLoad*>( context.findHeldStepState( step ) );
                if ( pPending == nullptr )
                {
                    TaskFuture<Scene*> future = engine::getSubsystem<SceneManager>().requestLoadFuture( params._path, SceneLoadKind::Travel );
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
                const AutomationStepHold hold = context.holdUntilReady( step, input, bLoaded, params._timeoutFrames,
                                                                        "the scene '" + params._path + ( pPending->_future.isReady() ? "' to become active" : "' to load" ) );
                if ( hold == AutomationStepHold::Run && bLoaded == false )
                    context.recordFailure( step, "the scene did not become active in time (or failed to load - see the log)" );
                return hold;
            }

            static SequencePlayerComponent* findSequence( AutomationStepContext& context, const AutomationStep& step )
            {
                GameObjectManager* pManager = context.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string{ step.readValidatedProperties<AutomationSequenceStepParams>()._object } ) : nullptr;
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
                const AutomationSequenceStepParams params    = step.readValidatedProperties<AutomationSequenceStepParams>();
                SequencePlayerComponent*           pSequence = findSequence( context, step );
                const bool                         bDone     = pSequence == nullptr || pSequence->isPlaying() == false;
                const AutomationStepHold           hold      = context.holdUntilReady( step, input, bDone, params._timeoutFrames,
                                                                                       "the sequence on '" + params._object + "' to finish playing" );
                if ( hold == AutomationStepHold::Run && ( pSequence == nullptr || bDone == false ) )
                    context.recordFailure( step, pSequence == nullptr ? "no object with that name and a SequencePlayerComponent in the active scene"
                                                                      : "the sequence was still playing when the wait timed out" );
                return hold;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( sceneLoadScene, "LoadScene", &AutomationSceneStepsInternal::runNothing, &AutomationStep::validateProperties<AutomationLoadSceneParams>, AutomationStepOptions{}.withHold( &AutomationSceneStepsInternal::holdLoadScene ) );
    SW_AUTOMATION_STEP( scenePlaySequence, "PlaySequence", &AutomationSceneStepsInternal::runPlaySequence, &AutomationStep::validateProperties<AutomationSequenceStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( sceneWaitSequence, "WaitSequence", &AutomationSceneStepsInternal::runNothing, &AutomationStep::validateProperties<AutomationSequenceStepParams>, AutomationStepOptions{}.withHold( &AutomationSceneStepsInternal::holdWaitSequence ) );
} // namespace sw
