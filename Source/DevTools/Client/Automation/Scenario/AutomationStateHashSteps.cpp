/**
 * @file AutomationStateHashSteps.cpp
 * @brief 시나리오가 활성 씬의 상태 해시(`SceneObjectHash::compute` — 개발 명령 `state.hash` · 단위 테스트 `SceneStateHashTest` 와 같은 해시)를 재고 견주는 단계입니다.
 * @details - `<RecordStateHash key="beforeSave"/>`: 지금 해시를 이 실행 안에 이름으로 기억한다.
 *          - `<ExpectStateHash key="beforeSave"/>`: 지금 해시가 기억한 값과 같아야 한다 — 저장 → 복원 뒤 세계가 그대로인지(장르 수직 슬라이스 `slice.scenario.xml`).
 *          기억하는 것은 오브젝트별 해시 글(`SceneObjectHash::writeObjectReport`)이라, 다르면 실패 글에 다른 오브젝트 이름을 적는다(다시 돌려 덤프하지 않는다).
 *          언리얼 Functional Test 의 세이브 왕복 · 유니티 Test Framework 의 상태 비교 자리.
 */
#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepParams.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneObjectHash.h"

namespace sw
{
    namespace
    {
        struct AutomationStateHashStepsInternal
        {
            static bool runRecordStateHash( AutomationStepContext& context, const AutomationStep& step )
            {
                const AutomationStateHashStepParams params   = step.readValidatedProperties<AutomationStateHashStepParams>();
                const GameObjectManager*            pManager = context.findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    context.recordFailure( step, "no active scene to hash" );
                    return true;
                }
                vector<SceneObjectHash> listObject;
                SceneObjectHash::collectObjects( *pManager, listObject );
                string report;
                SceneObjectHash::writeObjectReport( listObject, report );
                context.rememberText( params._key, std::move( report ) );
                return true;
            }

            static bool runExpectStateHash( AutomationStepContext& context, const AutomationStep& step )
            {
                const AutomationStateHashStepParams params   = step.readValidatedProperties<AutomationStateHashStepParams>();
                const GameObjectManager*            pManager = context.findActiveObjectManager();
                const string*                       pReport  = context.findRememberedText( params._key );
                if ( pReport == nullptr )
                {
                    context.recordFailure( step, "no state hash was recorded under key '" + params._key + "' - put <RecordStateHash key=\"" + params._key + "\"/> before it" );
                    return true;
                }
                if ( pManager == nullptr )
                {
                    context.recordFailure( step, "no active scene to hash" );
                    return true;
                }
                vector<SceneObjectHash> listExpected;
                if ( SceneObjectHash::parseObjectReport( *pReport, listExpected ) == false )
                {
                    context.recordFailure( step, "the state hash recorded under key '" + params._key + "' could not be read back" );
                    return true;
                }
                vector<SceneObjectHash> listActual;
                SceneObjectHash::collectObjects( *pManager, listActual );
                vector<string> listDiffering;
                if ( SceneObjectHash::findDifferingObjects( listExpected, listActual, listDiffering ) )
                {
                    string names;
                    for ( const string& name : listDiffering )
                    {
                        names += names.empty() ? "" : ", ";
                        names += name;
                    }
                    context.recordFailure( step, "the scene state differs from '" + params._key + "' in " + sw::to_string( listDiffering.size() ) + " object(s): " + names );
                }
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( stateRecordStateHash, "RecordStateHash", &AutomationStateHashStepsInternal::runRecordStateHash, &AutomationStep::validateProperties<AutomationStateHashStepParams>, AutomationStepOptions{} );
    SW_AUTOMATION_STEP( stateExpectStateHash, "ExpectStateHash", &AutomationStateHashStepsInternal::runExpectStateHash, &AutomationStep::validateProperties<AutomationStateHashStepParams>, AutomationStepOptions{} );
} // namespace sw
