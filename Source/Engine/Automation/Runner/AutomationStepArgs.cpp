#include "pch.h"

#include "Engine/Automation/Runner/AutomationStepArgs.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    bool AutomationStepArgsUtil::readFloat( const AutomationStep& step, string_view name, float32& inoutValue, string& outError )
    {
        const string* pValue = step.findAttribute( name );
        if ( pValue == nullptr )
            return true;
        float32 value = 0.0f;
        if ( StringUtil::parseFloat( string_view{ *pValue }, value ) )
        {
            inoutValue = value;
            return true;
        }
        outError = step.describe() + ": " + string( name ) + " must be a number, got '" + *pValue + "'";
        return false;
    }

    bool AutomationStepArgsUtil::readUint( const AutomationStep& step, string_view name, uint32& inoutValue, string& outError )
    {
        const string* pValue = step.findAttribute( name );
        if ( pValue == nullptr )
            return true;
        int32 value = 0;
        if ( StringUtil::parseInt( string_view{ *pValue }, value ) && value >= 0 )
        {
            inoutValue = static_cast<uint32>( value );
            return true;
        }
        outError = step.describe() + ": " + string( name ) + " must be a non-negative integer, got '" + *pValue + "'";
        return false;
    }

    bool AutomationStepArgsUtil::parseNumberComparison( const AutomationStep& step, float32 nearTolerance, NumberComparison& outComparison, string& outError )
    {
        static constexpr std::pair<const utf8*, NumberRule> kArrRule[] = {
            { "equals",  NumberRule::Equals},
            {   "near",    NumberRule::Near},
            {"atLeast", NumberRule::AtLeast},
            { "atMost",  NumberRule::AtMost},
        };
        outComparison._tolerance = nearTolerance;
        for ( const std::pair<const utf8*, NumberRule>& entry : kArrRule )
        {
            const string* pText = step.findAttribute( entry.first );
            if ( pText == nullptr )
                continue;
            outComparison._rule = entry.second;
            outComparison._text = *pText;
            return readFloat( step, entry.first, outComparison._expected, outError ) && readFloat( step, "tolerance", outComparison._tolerance, outError );
        }
        outError = step.describe() + ": needs one of equals · near · atLeast · atMost";
        return false;
    }

    bool AutomationStepArgsUtil::matchesNumber( const NumberComparison& comparison, float64 value, string& outRule )
    {
        const float64 expected = static_cast<float64>( comparison._expected );
        switch ( comparison._rule )
        {
            case NumberRule::Equals:
            {
                outRule = "== " + comparison._text;
                return value == expected;
            }
            case NumberRule::Near:
            {
                outRule = "near " + comparison._text;
                return MathUtil::abs( value - expected ) <= static_cast<float64>( comparison._tolerance );
            }
            case NumberRule::AtLeast:
            {
                outRule = ">= " + comparison._text;
                return value >= expected;
            }
            case NumberRule::AtMost:
            {
                outRule = "<= " + comparison._text;
                return value <= expected;
            }
        }
        return false;
    }

    bool AutomationStepArgsUtil::parseWaitTimeout( const AutomationStep& step, WaitTimeoutArgs& outArgs, string& outError )
    {
        outArgs._bSeconds = step.findAttribute( "timeoutSeconds" ) != nullptr;
        return readUint( step, "timeoutFrames", outArgs._timeoutFrames, outError ) && readFloat( step, "timeoutSeconds", outArgs._timeoutSeconds, outError );
    }

    bool AutomationStepArgsUtil::parseExpectLog( const AutomationStep& step, ExpectLogArgs& outArgs, string& outError )
    {
        outArgs._bExactCount = step.findAttribute( "count" ) != nullptr;
        return readUint( step, "since", outArgs._since, outError ) && readUint( step, outArgs._bExactCount ? "count" : "atLeast", outArgs._expected, outError );
    }

    bool AutomationStepArgsUtil::parseExpectImage( const AutomationStep& step, ExpectImageArgs& outArgs, string& outError )
    {
        const string* pGolden = step.requireAttribute( "golden", outError );
        if ( pGolden == nullptr )
            return false;
        // 골든 이름은 파일 이름의 한 조각이다 — 경로 · 점을 받지 않는다(`<시나리오>.<이름>.<백엔드>.ppm` 이 하나로 읽혀야 한다).
        const bool bPlainName = pGolden->empty() == false && pGolden->find_first_of( "/\\.:" ) == string::npos;
        if ( bPlainName == false )
        {
            outError = step.describe() + ": golden must be a plain name (no path, no dot), got '" + *pGolden + "'";
            return false;
        }
        outArgs._golden       = *pGolden;
        const string* pRegion = step.findAttribute( "region" );
        if ( pRegion != nullptr && AutomationImageMetric::tryParseRegion( *pRegion, outArgs._region ) == false )
        {
            outError = step.describe() + ": region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1, got '" + *pRegion + "'";
            return false;
        }
        if ( readFloat( step, "tolerance", outArgs._tolerance, outError ) == false )
            return false;
        if ( outArgs._tolerance < 0.0f || outArgs._tolerance > 1.0f )
        {
            outError = step.describe() + ": tolerance is the allowed fraction of differing pixels (0..1)";
            return false;
        }
        return true;
    }

    bool AutomationStepArgsUtil::compareVariable( const GlobalVariableInfo& info, string_view expected, float32 tolerance, bool& outMatched )
    {
        switch ( info._type )
        {
            case GlobalVariableType::Boolean:
            {
                bool value = false;
                if ( StringUtil::tryParseBool( expected, value ) == false )
                    return false;
                outMatched = info.getValueAsBool() == value;
                return true;
            }
            case GlobalVariableType::Int32:
            case GlobalVariableType::Enum:
            {
                int32 value = 0;
                if ( StringUtil::parseInt( expected, value ) == false )
                    return false;
                outMatched = info.getValueAsInt() == value;
                return true;
            }
            case GlobalVariableType::Float:
            {
                float32 value = 0.0f;
                if ( StringUtil::parseFloat( expected, value ) == false )
                    return false;
                outMatched = MathUtil::abs( info.getValueAsFloat() - value ) <= tolerance;
                return true;
            }
            case GlobalVariableType::String:
            {
                outMatched = string_view{ info.getValueAsString() } == expected;
                return true;
            }
            case GlobalVariableType::Count:
            {
                break;
            }
        }
        return false;
    }

    string AutomationStepArgsUtil::makeValueText( const AutomationWatchValue& value )
    {
        return value._bText ? value._text : formatNumber( value._number );
    }

    string AutomationStepArgsUtil::formatNumber( float64 value )
    {
        string text = to_string( value );
        // 끝의 0 을 정리한다(1.000000 → 1)
        while ( text.find( '.' ) != string::npos && ( text.back() == '0' || text.back() == '.' ) )
        {
            const bool bDot = text.back() == '.';
            text.pop_back();
            if ( bDot )
                break;
        }
        return text;
    }
} // namespace sw
