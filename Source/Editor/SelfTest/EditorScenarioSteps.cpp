/**
 * @file EditorScenarioSteps.cpp
 * @brief 자동화 시나리오의 에디터 단계(`EditorClick` · `EditorText`) — 에디터 패널 입력은 엔진 입력 층이 아니라 ImGui 가 받으므로 ImGui 사건으로 넣는다.
 * @details 게임 뷰 입력은 가상 입력 장치(`<Tap>` …)로, 에디터 패널 입력은 이 단계로 — 두 창구가 한 시나리오 파일에서 같이 쓰인다. 에디터 모듈이 올라온 실행
 *          (`-EnableEditor`)에서만 등록되므로, 에디터 없이 이 단계를 쓰면 시작할 때 읽기 오류다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorScenarioStepsInternal
        {
            /** @brief 속성이 @p listAllowed 안에만 있고 @p pRequired 가 있는지 봅니다. 위젯 이름표를 적기 시작한다(이름표는 켜진 뒤 그린 위젯만 적힌다). */
            static bool validate( const AutomationStep& step, std::initializer_list<string_view> listAllowed, const utf8* pRequired, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                if ( step.findAttribute( pRequired ) == nullptr )
                {
                    outError = step.describe() + ": needs " + pRequired + "=\"…\"";
                    return false;
                }
                EditorSelfTestMarks::setEnabled( true );
                return true;
            }

            static bool validateClick( const AutomationStep& step, string& outError )
            {
                if ( validate( step, { "mark", "button" }, "mark", outError ) == false )
                    return false;
                const string* pButton = step.findAttribute( "button" );
                int32         button  = 0;
                if ( pButton != nullptr && ( StringUtil::parseInt( string_view{ *pButton }, button ) == false || button < 0 || button > 4 ) )
                {
                    outError = step.describe() + ": button must be 0..4, got '" + *pButton + "'";
                    return false;
                }
                return true;
            }

            static bool validateText( const AutomationStep& step, string& outError ) { return validate( step, { "value" }, "value", outError ); }

            /** @brief 이름표 가운데로 마우스를 옮기고 누르고 뗀다(ImGui 입력 큐가 누름 · 뗌을 프레임에 나눠 넣는다). 이름표가 없으면 실패를 적는다. */
            static bool runClick( AutomationRunner& runner, const AutomationStep& step )
            {
                const string& mark    = *step.findAttribute( "mark" );
                const string* pButton = step.findAttribute( "button" );
                int32         button  = 0;
                if ( pButton != nullptr )
                    (void)StringUtil::parseInt( string_view{ *pButton }, button ); // 검사에서 봤다
                if ( EditorSelfTestInput::moveMouseToMark( mark ) == false )
                {
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                    return true;
                }
                EditorSelfTestInput::setMouseButton( button, true );
                EditorSelfTestInput::setMouseButton( button, false );
                return true;
            }

            static bool runText( AutomationRunner& /*runner*/, const AutomationStep& step )
            {
                EditorSelfTestInput::typeText( *step.findAttribute( "value" ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( editorClick, "EditorClick", &EditorScenarioStepsInternal::runClick, &EditorScenarioStepsInternal::validateClick, false );
    SW_AUTOMATION_STEP( editorText, "EditorText", &EditorScenarioStepsInternal::runText, &EditorScenarioStepsInternal::validateText, false );
} // namespace sw::editor
