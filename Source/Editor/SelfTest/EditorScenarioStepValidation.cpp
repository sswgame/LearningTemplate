#include "pch.h"

#include "Editor/SelfTest/EditorScenarioStepValidation.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/pair.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepValidation.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 속성이 @p listAllowed 안에만 있고 @p pRequired 가 있는지 검사합니다. 순수 검사입니다(기록을 켜는 일은 단계의 준비 함수). */
        bool validateAttributes( const AutomationStep& step, std::initializer_list<string_view> listAllowed, const utf8* pRequired, string& outError )
        {
            if ( step.hasOnlyAttributes( listAllowed, outError ) == false )
                return false;
            if ( pRequired != nullptr && step.findAttribute( pRequired ) == nullptr )
            {
                outError = step.describe() + ": needs " + pRequired + "=\"…\"";
                return false;
            }
            return true;
        }
    } // namespace

    bool EditorScenarioStepValidation::readCount( const AutomationStep& step, string_view name, uint32& inoutValue )
    {
        const string* pText = step.findAttribute( name );
        int32         value = 0;
        if ( pText == nullptr )
            return true;
        if ( StringUtil::parseInt( string_view{ *pText }, value ) == false || value < 0 )
            return false;
        inoutValue = static_cast<uint32>( value );
        return true;
    }

    bool EditorScenarioStepValidation::readAnchor( const AutomationStep& step, float2& outAnchor )
    {
        const string* pAnchor = step.findAttribute( "anchor" );
        if ( pAnchor == nullptr )
            return true;
        const size_t comma = pAnchor->find( ',' );
        float64      x{ 0.0 };
        float64      y{ 0.0 };
        const bool   bInvalidAnchor = comma == string::npos || StringUtil::parseDouble( string_view{ *pAnchor }.substr( 0, comma ), x ) == false ||
                                    StringUtil::parseDouble( string_view{ *pAnchor }.substr( comma + 1 ), y ) == false || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0;
        if ( bInvalidAnchor )
            return false;
        outAnchor = float2{ static_cast<float32>( x ), static_cast<float32>( y ) };
        return true;
    }

    bool EditorScenarioStepValidation::readPressState( const AutomationStep& step, EditorPressState& inoutState )
    {
        const string* pState = step.findAttribute( "state" );
        if ( pState == nullptr )
            return true;
        if ( StringUtil::equals( string_view{ *pState }, "down" ) )
            inoutState = EditorPressState::Down;
        else if ( StringUtil::equals( string_view{ *pState }, "up" ) )
            inoutState = EditorPressState::Up;
        else
            return false;
        return true;
    }

    bool EditorScenarioStepValidation::parseModifiers( const AutomationStep& step, uint8& outMask )
    {
        outMask             = 0;
        const string* pMods = step.findAttribute( "mods" );
        if ( pMods == nullptr )
            return true;
        const string_view mods{ *pMods };
        size_t            begin = 0;
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

    bool EditorScenarioStepValidation::parseClick( const AutomationStep& step, EditorClickArgs& outArgs, string& outError )
    {
        if ( readAnchor( step, outArgs._anchor ) == false )
        {
            outError = step.describe() + ": anchor must be 'x,y' in 0..1 (0.5,0.5 is the centre), got '" + *step.findAttribute( "anchor" ) + "'";
            return false;
        }
        if ( readPressState( step, outArgs._state ) == false )
        {
            outError = step.describe() + ": state must be down or up, got '" + *step.findAttribute( "state" ) + "'";
            return false;
        }
        if ( parseModifiers( step, outArgs._modifierMask ) == false )
        {
            outError = step.describe() + ": mods must be ctrl, shift or alt joined with '+', got '" + *step.findAttribute( "mods" ) + "'";
            return false;
        }
        const string* pButton = step.findAttribute( "button" );
        if ( pButton != nullptr && ( StringUtil::parseInt( string_view{ *pButton }, outArgs._button ) == false || outArgs._button < 0 || outArgs._button > 4 ) )
        {
            outError = step.describe() + ": button must be 0..4, got '" + *pButton + "'";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::parseDrag( const AutomationStep& step, EditorDragArgs& outArgs, string& outError )
    {
        const pair<const utf8*, float64*> arrNumber[] = {
            {    "dx",       &outArgs._deltaX},
            {    "dy",       &outArgs._deltaY},
            {"button", &outArgs._buttonNumber},
        };
        for ( const pair<const utf8*, float64*>& entry : arrNumber )
        {
            const string* pValue = step.findAttribute( entry.first );
            if ( pValue != nullptr && StringUtil::parseDouble( string_view{ *pValue }, *entry.second ) == false )
            {
                outError = step.describe() + ": " + entry.first + " must be a number, got '" + *pValue + "'";
                return false;
            }
        }
        if ( parseModifiers( step, outArgs._modifierMask ) == false )
        {
            outError = step.describe() + ": mods must be ctrl, shift or alt joined with '+', got '" + *step.findAttribute( "mods" ) + "'";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::parseExpectObject( const AutomationStep& step, EditorExpectObjectArgs& outArgs, string& outError )
    {
        const bool bInvalidCountArguments = readCount( step, "count", outArgs._expectedCount ) == false || readCount( step, "selected", outArgs._selected ) == false || outArgs._selected > 1;
        if ( bInvalidCountArguments )
        {
            outError = step.describe() + ": count must be a whole number and selected 0 or 1";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::validateClick( const AutomationStep& step, string& outError )
    {
        EditorClickArgs args;
        return validateAttributes( step, { "mark", "button", "mods", "state", "anchor" }, "mark", outError ) && parseClick( step, args, outError );
    }

    bool EditorScenarioStepValidation::validateText( const AutomationStep& step, string& outError )
    {
        return validateAttributes( step, { "value" }, "value", outError );
    }

    bool EditorScenarioStepValidation::validateDrag( const AutomationStep& step, string& outError )
    {
        if ( validateAttributes( step, { "mark", "toMark", "dx", "dy", "button", "mods" }, "mark", outError ) == false )
            return false;
        if ( step.findAttribute( "toMark" ) != nullptr && ( step.findAttribute( "dx" ) != nullptr || step.findAttribute( "dy" ) != nullptr ) )
        {
            outError = step.describe() + ": give either toMark or dx/dy, not both";
            return false;
        }
        EditorDragArgs args;
        return parseDrag( step, args, outError );
    }

    bool EditorScenarioStepValidation::validateHover( const AutomationStep& step, string& outError )
    {
        if ( validateAttributes( step, { "mark", "anchor", "release" }, nullptr, outError ) == false )
            return false;
        const bool bRelease = step.findAttribute( "release" ) != nullptr;
        if ( bRelease == ( step.findAttribute( "mark" ) != nullptr ) )
        {
            outError = step.describe() + ": give either mark (and anchor) or release=\"true\"";
            return false;
        }
        float2 anchor{ 0.5f, 0.5f };
        if ( readAnchor( step, anchor ) == false )
        {
            outError = step.describe() + ": anchor must be 'x,y' in 0..1";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::validateKey( const AutomationStep& step, string& outError )
    {
        if ( validateAttributes( step, { "key", "state" }, "key", outError ) == false )
            return false;
        EditorPressState state{ EditorPressState::Tap };
        if ( readPressState( step, state ) == false )
        {
            outError = step.describe() + ": state must be down or up, got '" + *step.findAttribute( "state" ) + "'";
            return false;
        }
        vector<string_view> listToken;
        if ( splitKeyChord( *step.findAttribute( "key" ), listToken ) == false )
        {
            outError = step.describe() + ": key must be names joined with '+' (e.g. Enter · Escape · F5 · Ctrl+Z), got '" + *step.findAttribute( "key" ) + "'";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::validateExpectObject( const AutomationStep& step, string& outError )
    {
        EditorExpectObjectArgs args;
        return validateAttributes( step, { "name", "count", "component", "selected" }, "name", outError ) && parseExpectObject( step, args, outError );
    }

    bool EditorScenarioStepValidation::validateDockLayout( const AutomationStep& step, string& outError )
    {
        if ( validateAttributes( step, { "minNodeSize" }, nullptr, outError ) == false )
            return false;
        uint32 value = 0;
        if ( readCount( step, "minNodeSize", value ) == false )
        {
            outError = step.describe() + ": minNodeSize must be pixels";
            return false;
        }
        return true;
    }

    bool EditorScenarioStepValidation::validateCapture( const AutomationStep& step, string& outError )
    {
        for ( const AutomationAttribute& attribute : step._listAttribute )
        {
            const string_view name{ attribute._name };
            if ( name == "file" )
                continue;
            float32    value{ 0.0f };
            const bool bLimit      = name == "maxMagentaFraction" || name == "maxClippedWindows";
            const bool bValidLimit = bLimit && StringUtil::parseFloat( attribute._value, value ) && value >= 0.0f;
            if ( bValidLimit )
                continue;
            outError = step.describe() + ": unknown or invalid attribute '" + attribute._name + "' (file · maxMagentaFraction · maxClippedWindows)";
            return false;
        }
        const string* pFile = step.findAttribute( "file" );
        if ( pFile != nullptr && StringUtil::endsWith( *pFile, ".png", true ) )
            return true;
        outError = step.describe() + ": needs file=\"….png\"";
        return false;
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
} // namespace sw::editor
