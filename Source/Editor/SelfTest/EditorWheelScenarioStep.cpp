/**
 * @file EditorWheelScenarioStep.cpp
 * @brief 시나리오 단계 `EditorWheel` 입니다. 커서 아래 에디터 창에 세로 휠 눈금을 넣습니다.
 * @details 엔진의 `Wheel` 단계는 엔진 입력(게임)으로 가므로 ImGui 에 닿지 않는다. 이 단계는 ImGui 입력 큐에 넣는다.
 *          커서는 앞서 `EditorHover` 나 `EditorClick state="down"` 이 레이블 위에 붙잡아 둔 자리다.
 *          예: `<EditorWheel dy="3"/>` 은 위로 세 눈금이다(씬 뷰에서는 가까워진다).
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Scenario/AutomationScenario.h"
#include "Engine/Scenario/AutomationStepContext.h"
#include "Engine/Scenario/AutomationStepRegistry.h"
#include "Engine/Scenario/AutomationStepValidation.h"

namespace sw::editor
{
    namespace
    {
        struct EditorWheelStepInternal
        {
            static constexpr const utf8* kWheelKind = "EditorWheel";

            [[nodiscard]] static bool readNotches( const AutomationStep& step, float64& outNotches, string& outError )
            {
                const string* pValue = step.requireAttribute( "dy", outError );
                if ( pValue == nullptr )
                    return false;
                if ( StringUtil::parseDouble( string_view{ *pValue }, outNotches ) == false )
                {
                    outError = step.describe() + ": dy must be a number, got '" + *pValue + "'";
                    return false;
                }
                return true;
            }

            static bool validate( const AutomationStep& step, string& outError )
            {
                float64 notches = 0.0;
                return step.hasOnlyAttributes( { "dy" }, outError ) && readNotches( step, notches, outError );
            }

            /** @brief 입력 큐가 에디터 컨텍스트에 있으므로 에디터가 없는 실행은 오류로 멈춘다. */
            [[nodiscard]] static bool prepare( AutomationStepContext& /*runner*/, const AutomationStep& step, string& outError )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) )
                    return true;
                outError = step.describe() + ": needs the editor (no editor context to queue the wheel - run with -EnableEditor)";
                return false;
            }

            static bool run( AutomationStepContext& runner, const AutomationStep& step )
            {
                float64 notches = 0.0;
                string  error;
                if ( readNotches( step, notches, error ) == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                EditorSelfTestInput::scrollMouseWheel( static_cast<float32>( notches ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP_WITH_HOOKS( editorWheel, EditorWheelStepInternal::kWheelKind, &EditorWheelStepInternal::run, &EditorWheelStepInternal::validate, &EditorWheelStepInternal::prepare, nullptr,
                                   false );
    SW_AUTOMATION_STEP_VALIDATOR( editorWheel, EditorWheelStepInternal::kWheelKind, &EditorWheelStepInternal::validate );
} // namespace sw::editor
