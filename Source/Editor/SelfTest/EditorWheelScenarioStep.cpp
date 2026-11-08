/**
 * @file EditorWheelScenarioStep.cpp
 * @brief 시나리오 단계 `EditorWheel` 입니다. 커서 아래 에디터 창에 세로 휠 눈금을 넣습니다.
 * @details 엔진의 `Wheel` 단계는 엔진 입력(게임)으로 가므로 ImGui 에 닿지 않는다. 이 단계는 ImGui 입력 큐에 넣는다.
 *          커서는 앞서 `EditorHover` 나 `EditorClick state="down"` 이 레이블 위에 붙잡아 둔 자리다.
 *          예: `<EditorWheel dy="3"/>` 은 위로 세 눈금이다(씬 뷰에서는 가까워진다).
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "DevTools/Client/Automation/Scenario/AutomationStepValidation.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorWheelStepInternal
        {
            static constexpr const utf8* kWheelKind = "EditorWheel";

            [[nodiscard]] static expected<float64> readNotches( const AutomationStep& step )
            {
                const expected<const string*> value = step.requireAttribute( "dy" );
                if ( value.has_value() == false )
                    return unexpected( value.error() );
                float64 notches = 0.0;
                if ( StringUtil::parseDouble( string_view{ **value }, notches ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "dy must be a number", **value } );
                return notches;
            }

            static expected<void> validate( const AutomationStep& step )
            {
                const expected<void> allowed = step.hasOnlyAttributes( { "dy" } );
                if ( allowed.has_value() == false )
                    return allowed;
                const expected<float64> notches = readNotches( step );
                if ( notches.has_value() == false )
                    return unexpected( notches.error() );
                return {};
            }

            /** @brief 입력 큐가 에디터 컨텍스트에 있으므로 에디터가 없는 실행은 오류로 멈춘다. */
            [[nodiscard]] static expected<void> prepare( AutomationStepContext& /*runner*/, const AutomationStep& /*step*/ )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) )
                    return {};
                return unexpected( Error{ ErrorKind::Unsupported, "needs the editor (no editor context to queue the wheel - run with -EnableEditor)" } );
            }

            static bool run( AutomationStepContext& runner, const AutomationStep& step )
            {
                const expected<float64> notches = readNotches( step );
                if ( notches.has_value() == false )
                {
                    runner.recordFailure( step, AutomationStep::describeReason( notches.error() ) ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                EditorSelfTestInput::scrollMouseWheel( static_cast<float32>( *notches ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( editorWheel, EditorWheelStepInternal::kWheelKind, &EditorWheelStepInternal::run, &EditorWheelStepInternal::validate, AutomationStepOptions{}.withPrepare( &EditorWheelStepInternal::prepare ) );
    SW_AUTOMATION_STEP_VALIDATOR( editorWheel, EditorWheelStepInternal::kWheelKind, &EditorWheelStepInternal::validate );
} // namespace sw::editor
