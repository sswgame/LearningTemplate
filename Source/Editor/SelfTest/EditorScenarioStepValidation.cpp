#include "pch.h"

#include "Editor/SelfTest/EditorScenarioStepValidation.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"

namespace sw::editor
{
    namespace
    {
        struct EditorScenarioStepValidationInternal
        {
            /** @brief 레이블 안의 위치(0..1)인지 봅니다. 아니면 false 와 이유입니다. */
            [[nodiscard]] static bool checkAnchor( const AutomationStep& step, const float2& anchor, string& outError )
            {
                if ( anchor._x >= 0.0f && anchor._x <= 1.0f && anchor._y >= 0.0f && anchor._y <= 1.0f )
                    return true;
                outError = step.describe() + ": anchor must be 'x,y' in 0..1 (0.5,0.5 is the centre)";
                return false;
            }

            /** @brief 수정자 글을 봅니다. */
            [[nodiscard]] static bool checkModifiers( const AutomationStep& step, const string& mods, string& outError )
            {
                uint8 mask = 0;
                if ( EditorScenarioStepValidation::parseModifiers( mods, mask ) )
                    return true;
                outError = step.describe() + ": mods must be ctrl, shift or alt joined with '+', got '" + mods + "'";
                return false;
            }
        };
    } // namespace

    bool EditorScenarioStepValidation::parseModifiers( string_view mods, uint8& outMask )
    {
        outMask = 0;
        if ( mods.empty() )
            return true;
        size_t begin = 0;
        while ( begin <= mods.size() )
        {
            const size_t      plus  = mods.find( '+', begin );
            const size_t      end   = plus == string_view::npos ? mods.size() : plus;
            const string_view token = mods.substr( begin, end - begin );
            if ( StringUtil::equals( token, "ctrl" ) )
                outMask |= EditorModifierBit::kCtrl;
            else if ( StringUtil::equals( token, "shift" ) )
                outMask |= EditorModifierBit::kShift;
            else if ( StringUtil::equals( token, "alt" ) )
                outMask |= EditorModifierBit::kAlt;
            else
                return false;
            if ( plus == string_view::npos )
                break;
            begin = plus + 1;
        }
        return true;
    }

    bool EditorScenarioStepValidation::splitKeyChord( string_view chord, vector<string_view>& outListToken )
    {
        outListToken.clear();
        size_t begin = 0;
        while ( begin <= chord.size() )
        {
            const size_t      plus  = chord.find( '+', begin );
            const size_t      end   = plus == string_view::npos ? chord.size() : plus;
            const string_view token = chord.substr( begin, end - begin );
            if ( token.empty() )
                return false;
            outListToken.push_back( token );
            if ( plus == string_view::npos )
                break;
            begin = plus + 1;
        }
        return outListToken.empty() == false;
    }

    bool EditorScenarioStepValidation::validateClick( const AutomationStep& step, string& outError )
    {
        using Internal = EditorScenarioStepValidationInternal;
        EditorClickFields fields;
        return step.readFields( fields, outError ) && Internal::checkAnchor( step, fields._anchor, outError ) && Internal::checkModifiers( step, fields._mods, outError );
    }

    bool EditorScenarioStepValidation::validateText( const AutomationStep& step, string& outError )
    {
        EditorTextFields fields;
        return step.readFields( fields, outError );
    }

    bool EditorScenarioStepValidation::validateSelfTest( const AutomationStep& step, string& outError )
    {
        EditorSelfTestFields fields;
        return step.readFields( fields, outError );
    }

    bool EditorScenarioStepValidation::validateDrag( const AutomationStep& step, string& outError )
    {
        using Internal = EditorScenarioStepValidationInternal;
        EditorDragFields fields;
        if ( step.readFields( fields, outError ) == false )
            return false;
        if ( fields._toMark.empty() == false && ( step.findAttribute( "dx" ) != nullptr || step.findAttribute( "dy" ) != nullptr ) )
        {
            outError = step.describe() + ": give either toMark or dx/dy, not both";
            return false;
        }
        return Internal::checkAnchor( step, fields._anchor, outError ) && Internal::checkModifiers( step, fields._mods, outError );
    }

    bool EditorScenarioStepValidation::validateHover( const AutomationStep& step, string& outError )
    {
        using Internal = EditorScenarioStepValidationInternal;
        EditorHoverFields fields;
        if ( step.readFields( fields, outError, { "release" } ) == false )
            return false;
        const bool bRelease = step.findAttribute( "release" ) != nullptr;
        if ( bRelease == ( fields._mark.empty() == false ) )
        {
            outError = step.describe() + ": give either mark (and anchor) or release=\"true\"";
            return false;
        }
        return Internal::checkAnchor( step, fields._anchor, outError );
    }

    bool EditorScenarioStepValidation::validateKey( const AutomationStep& step, string& outError )
    {
        EditorKeyFields fields;
        if ( step.readFields( fields, outError ) == false )
            return false;
        vector<string_view> listToken;
        if ( splitKeyChord( fields._key, listToken ) == false )
        {
            outError = step.describe() + ": key must be names joined with '+' (e.g. Enter · Escape · F5 · Ctrl+Z), got '" + fields._key + "'";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::validateExpectObject( const AutomationStep& step, string& outError )
    {
        EditorExpectObjectFields fields;
        return step.readFields( fields, outError );
    }

    bool EditorScenarioStepValidation::validateDockLayout( const AutomationStep& step, string& outError )
    {
        EditorDockLayoutFields fields;
        return step.readFields( fields, outError );
    }

    bool EditorScenarioStepValidation::validateCapture( const AutomationStep& step, string& outError )
    {
        EditorCaptureFields fields;
        if ( step.readFields( fields, outError ) == false )
            return false;
        if ( StringUtil::endsWith( fields._file, ".png", true ) )
            return true;
        outError = step.describe() + ": needs file=\"….png\"";
        return false;
    }

    bool EditorScenarioStepValidation::validateWaitSettled( const AutomationStep& step, string& outError )
    {
        EditorNoFields fields;
        return step.readFields( fields, outError );
    }

    // 에디터 모듈이 올라오지 않은 프로세스도 이 단계들의 속성을 검사할 수 있게, 단계 등록이 받는 것과 같은 검사 함수를 따로 올린다.
    SW_AUTOMATION_STEP_VALIDATOR( editorClick, EditorScenarioStepValidation::kClickKind, &EditorScenarioStepValidation::validateClick );
    SW_AUTOMATION_STEP_VALIDATOR( editorDrag, EditorScenarioStepValidation::kDragKind, &EditorScenarioStepValidation::validateDrag );
    SW_AUTOMATION_STEP_VALIDATOR( editorHover, EditorScenarioStepValidation::kHoverKind, &EditorScenarioStepValidation::validateHover );
    SW_AUTOMATION_STEP_VALIDATOR( editorText, EditorScenarioStepValidation::kTextKind, &EditorScenarioStepValidation::validateText );
    SW_AUTOMATION_STEP_VALIDATOR( editorKey, EditorScenarioStepValidation::kKeyKind, &EditorScenarioStepValidation::validateKey );
    SW_AUTOMATION_STEP_VALIDATOR( editorExpectObject, EditorScenarioStepValidation::kExpectObjectKind, &EditorScenarioStepValidation::validateExpectObject );
    SW_AUTOMATION_STEP_VALIDATOR( editorExpectDockLayout, EditorScenarioStepValidation::kDockLayoutKind, &EditorScenarioStepValidation::validateDockLayout );
    SW_AUTOMATION_STEP_VALIDATOR( editorCaptureUI, EditorScenarioStepValidation::kCaptureKind, &EditorScenarioStepValidation::validateCapture );
    SW_AUTOMATION_STEP_VALIDATOR( editorSelfTest, EditorScenarioStepValidation::kSelfTestKind, &EditorScenarioStepValidation::validateSelfTest );
    SW_AUTOMATION_STEP_VALIDATOR( editorWaitSettled, EditorScenarioStepValidation::kWaitSettledKind, &EditorScenarioStepValidation::validateWaitSettled );
} // namespace sw::editor
