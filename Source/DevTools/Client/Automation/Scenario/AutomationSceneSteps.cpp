/**
 * @file AutomationSceneSteps.cpp
 * @brief 시나리오가 씬과 연출을 다루는 단계입니다 — 데모 · 어트랙트 · 튜토리얼 재생(`ScenarioPlayback`)이 쓰는 것.
 * @details - `<LoadScene path="game/x/maps/a.scene.xml" timeoutFrames="600"/>`: 씬 로드를 요청하고 그 씬이 활성이 될 때까지 시나리오 시계를 붙잡는다(못 열면 실패).
 *          - `<ExpectActiveScene path="game/x/maps/a.scene.xml"/>`: 활성 씬이 그 리소스에서 왔는지 본다(세이브 불러오기가 찍은 씬으로 옮겼는지 — 감사 G-7).
 *          - `<PlaySequence object="Intro"/>`: 활성 씬의 그 이름 오브젝트에 붙은 시퀀스(`SequencePlayerComponent`)를 처음부터 재생한다.
 *            재생 칸 `rate`(음수 = 끝에서 거꾸로) · `loop`(None · Loop · PingPong) · `movement`(Free · Blocked · Confined) · `rangeStart` · `rangeEnd` 는 그 재생에 건다(비우면 씬 값).
 *          - `<WaitSequence object="Intro" timeoutFrames="1800"/>`: 그 시퀀스가 끝날 때까지 붙잡는다(언리얼 Sequencer 의 Play → Wait 같은 쌍).
 *          - `<SkipSequence object="Intro"/>`: 재생 중인 그 시퀀스를 끝으로 건너뛴다(언리얼 `GoToEndAndStop` — 끝 상태 적용, 정상 끝). 재생 중이 아니면 실패다.
 *          시퀀스 안에서 게임 행동을 끼우는 쪽(시퀀서 이벤트 트랙이 시나리오를 부르는 것)은 `ScenarioPlayback::play` 를 부른다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
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

                TaskFuture<SceneHandle> _future;
                explicit PendingLoad( TaskFuture<SceneHandle> future )
                    : _future{ std::move( future ) }
                {
                }
            };

            static bool runNothing( IAutomationStepContext& context, const AutomationStep& step )
            {
                (void)context;
                (void)step;
                return true; // 일은 붙잡기 함수가 했다
            }

            static AutomationStepHold holdLoadScene( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
            {
                const AutomationLoadSceneParams params = step.readValidatedProperties<AutomationLoadSceneParams>();
                // 상태는 맡긴 이 단계만 만든다 — 같은 타입으로 되돌려 읽는다.
                PendingLoad* pPending = static_cast<PendingLoad*>( context.findHeldStepState( step ) );
                if ( pPending == nullptr )
                {
                    TaskFuture<SceneHandle> future = engine::getSubsystem<SceneManager>().requestLoadFuture( params._path.get(), SceneLoadKind::Travel );
                    if ( future.isValid() == false )
                    {
                        context.recordFailure( step, "the scene load could not be requested" );
                        return AutomationStepHold::Run;
                    }
                    pPending = static_cast<PendingLoad*>( &context.setHeldStepState( step, make_unique<PendingLoad>( std::move( future ) ) ) );
                }
                SceneManager&      sceneManager = engine::getSubsystem<SceneManager>();
                const Scene* const pLoaded      = pPending->_future.isReady() ? sceneManager.resolveScene( pPending->_future.get() ) : nullptr;
                const bool         bLoaded      = pLoaded != nullptr && sceneManager.getActiveScene() == pLoaded;
                // Run 이면 실행기가 상태를 지운다 — 그 뒤로 pPending 을 읽지 않는다.
                StringBuilder<constant::buffer::kMax1024> waitingFor;
                waitingFor.appendFormat( "the scene '%#%#", params._path.get(), pPending->_future.isReady() ? "' to become active" : "' to load" );
                const AutomationStepHold hold = context.holdUntilReady( step, input, bLoaded, params._timeoutFrames, waitingFor.view() );
                if ( hold == AutomationStepHold::Run && bLoaded == false )
                    context.recordFailure( step, "the scene did not become active in time (or failed to load - see the log)" );
                return hold;
            }

            static bool runExpectActiveScene( IAutomationStepContext& context, const AutomationStep& step )
            {
                const AutomationExpectActiveSceneParams params = step.readValidatedProperties<AutomationExpectActiveSceneParams>();
                const Scene* const                      pScene = engine::getSubsystem<SceneManager>().getActiveScene();
                if ( pScene == nullptr )
                    context.recordFailure( step, "there is no active scene" );
                else if ( params._path != pScene->getSourcePath() )
                {
                    StringBuilder<constant::buffer::kMax1024> text;
                    text.appendFormat( "the active scene came from '%#', not '%#'", pScene->getSourcePath(), params._path.get() );
                    context.recordFailure( step, text.view() );
                }
                return true;
            }

            static SequencePlayerComponent* findSequence( IAutomationStepContext& context, const AutomationStep& step )
            {
                GameObjectManager* pManager = context.findActiveObjectManager();
                GameObject*        pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string{ step.readValidatedProperties<AutomationSequenceStepParams>()._object } ) : nullptr;
                return pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
            }

            static bool runPlaySequence( IAutomationStepContext& context, const AutomationStep& step )
            {
                SequencePlayerComponent* pSequence = findSequence( context, step );
                if ( pSequence == nullptr )
                {
                    context.recordFailure( step, "no object with that name and a SequencePlayerComponent in the active scene" );
                    return true;
                }
                const AutomationSequenceStepParams params = step.readValidatedProperties<AutomationSequenceStepParams>();
                if ( params._loop.empty() == false )
                {
                    const optional<SequenceLoopMode> loopMode = tryParseEnum<SequenceLoopMode>( params._loop );
                    if ( loopMode.has_value() == false )
                    {
                        context.recordFailure( step, "loop must be None, Loop or PingPong" );
                        return true;
                    }
                    pSequence->setLoopMode( *loopMode );
                }
                if ( params._movement.empty() == false )
                {
                    const optional<SequencePlayerMovement> movement = tryParseEnum<SequencePlayerMovement>( params._movement );
                    if ( movement.has_value() == false )
                    {
                        context.recordFailure( step, "movement must be Free, Blocked or Confined" );
                        return true;
                    }
                    pSequence->setPlayerMovement( *movement );
                }
                if ( params._rate != 0.0f && pSequence->setPlayRate( MathUtil::abs( params._rate ) ) == false )
                {
                    context.recordFailure( step, "the sequence player rejected the rate" );
                    return true;
                }
                // 범위는 틀기 전에 건다 — 틀기가 범위의 처음(역방향이면 끝)에서 시작한다.
                const bool bRange = params._rangeStart != 0 || params._rangeEnd != 0;
                if ( bRange && pSequence->setPlaybackRange( params._rangeStart, params._rangeEnd ) == false )
                {
                    context.recordFailure( step, "the sequence player rejected the playback range" );
                    return true;
                }
                if ( params._rate < 0.0f )
                    pSequence->playReverse();
                else
                    pSequence->play();
                return true;
            }

            static bool runSkipSequence( IAutomationStepContext& context, const AutomationStep& step )
            {
                SequencePlayerComponent* pSequence = findSequence( context, step );
                if ( pSequence == nullptr )
                    context.recordFailure( step, "no object with that name and a SequencePlayerComponent in the active scene" );
                else if ( pSequence->skipToEnd() == false )
                    context.recordFailure( step, "the sequence was not playing" );
                return true;
            }

            static AutomationStepHold holdWaitSequence( IAutomationStepContext& context, const AutomationStep& step, InputManager& input )
            {
                const AutomationSequenceStepParams       params    = step.readValidatedProperties<AutomationSequenceStepParams>();
                SequencePlayerComponent*                 pSequence = findSequence( context, step );
                const bool                               bDone     = pSequence == nullptr || pSequence->isPlaying() == false;
                StringBuilder<constant::buffer::kMax256> waitingFor;
                waitingFor.appendFormat( "the sequence on '%#' to finish playing", params._object );
                const AutomationStepHold hold = context.holdUntilReady( step, input, bDone, params._timeoutFrames, waitingFor.view() );
                if ( hold == AutomationStepHold::Run && ( pSequence == nullptr || bDone == false ) )
                    context.recordFailure( step, pSequence == nullptr ? "no object with that name and a SequencePlayerComponent in the active scene"
                                                                      : "the sequence was still playing when the wait timed out" );
                return hold;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( sceneLoadScene, "LoadScene", &AutomationSceneStepsInternal::runNothing, &AutomationStep::validateProperties<AutomationLoadSceneParams>, AutomationStepOptions{}.withHold( &AutomationSceneStepsInternal::holdLoadScene ) );
    SW_AUTOMATION_STEP( sceneExpectActiveScene, "ExpectActiveScene", &AutomationSceneStepsInternal::runExpectActiveScene, &AutomationStep::validateProperties<AutomationExpectActiveSceneParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( scenePlaySequence, "PlaySequence", &AutomationSceneStepsInternal::runPlaySequence, &AutomationStep::validateProperties<AutomationSequenceStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( sceneSkipSequence, "SkipSequence", &AutomationSceneStepsInternal::runSkipSequence, &AutomationStep::validateProperties<AutomationSequenceStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( sceneWaitSequence, "WaitSequence", &AutomationSceneStepsInternal::runNothing, &AutomationStep::validateProperties<AutomationSequenceStepParams>, AutomationStepOptions{}.withHold( &AutomationSceneStepsInternal::holdWaitSequence ) );
} // namespace sw
