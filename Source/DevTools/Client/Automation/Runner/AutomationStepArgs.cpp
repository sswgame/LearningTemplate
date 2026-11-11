#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationStepArgs.h"

#include "Core/Container/pair.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/AutomationRunnerParams.h"

namespace sw
{
    expected<void> AutomationStepArgsUtil::readFloat( const AutomationStep& step, string_view name, float32& inoutValue )
    {
        const string* pValue = step.findAttribute( name );
        if ( pValue == nullptr )
            return {};
        float32 value = 0.0f;
        if ( StringUtil::parseFloat( string_view{ *pValue }, value ) )
        {
            inoutValue = value;
            return {};
        }
        return unexpected( Error{ ErrorKind::InvalidArgument, "must be a number", name }.appendDetail( "=" ).appendDetail( *pValue ) );
    }

    expected<void> AutomationStepArgsUtil::readUint( const AutomationStep& step, string_view name, uint32& inoutValue )
    {
        const string* pValue = step.findAttribute( name );
        if ( pValue == nullptr )
            return {};
        int32 value = 0;
        if ( StringUtil::parseInt( string_view{ *pValue }, value ) && value >= 0 )
        {
            inoutValue = static_cast<uint32>( value );
            return {};
        }
        return unexpected( Error{ ErrorKind::InvalidArgument, "must be a non-negative integer", name }.appendDetail( "=" ).appendDetail( *pValue ) );
    }

    expected<AutomationStepArgsUtil::NumberComparison> AutomationStepArgsUtil::parseNumberComparison( const AutomationStep& step, float32 nearTolerance )
    {
        static constexpr pair<const utf8*, NumberRule> kArrRule[] = {
            { "equals",  NumberRule::Equals},
            {   "near",    NumberRule::Near},
            {"atLeast", NumberRule::AtLeast},
            { "atMost",  NumberRule::AtMost},
        };
        NumberComparison comparison;
        comparison._tolerance = nearTolerance;
        for ( const pair<const utf8*, NumberRule>& entry : kArrRule )
        {
            const string* pText = step.findAttribute( entry.first );
            if ( pText == nullptr )
                continue;
            comparison._rule          = entry.second;
            comparison._text          = *pText;
            const expected<void> read = readFloat( step, entry.first, comparison._expected ).and_then( [&]()
            { return readFloat( step, "tolerance", comparison._tolerance ); } );
            if ( read.has_value() == false )
                return unexpected( read.error() );
            return comparison;
        }
        return unexpected( Error{ ErrorKind::InvalidArgument, "needs one of equals · near · atLeast · atMost" } );
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

    expected<AutomationStepArgsUtil::WaitTimeoutArgs> AutomationStepArgsUtil::parseWaitTimeout( const AutomationStep& step )
    {
        WaitTimeoutArgs args;
        args._bSeconds            = step.findAttribute( "timeoutSeconds" ) != nullptr;
        args._bWallSeconds        = step.findAttribute( "timeoutWallSeconds" ) != nullptr;
        const expected<void> read = readUint( step, "timeoutFrames", args._timeoutFrames )
                                        .and_then( [&]()
        { return readFloat( step, "timeoutSeconds", args._timeoutSeconds ); } )
                                        .and_then( [&]()
        { return readFloat( step, "timeoutWallSeconds", args._timeoutWallSeconds ); } );
        if ( read.has_value() == false )
            return unexpected( read.error() );
        return args;
    }

    expected<AutomationStepArgsUtil::ExpectImageArgs> AutomationStepArgsUtil::parseExpectImage( const AutomationStep& step )
    {
        AutomationExpectImageParams params;
        const expected<void>        read = step.readProperties( params );
        if ( read.has_value() == false )
            return unexpected( read.error() );
        // 골든 이름은 파일 이름의 한 조각이다 — 경로 · 점을 받지 않는다(`<시나리오>.<이름>.<백엔드>.ppm` 이 하나로 읽혀야 한다).
        const bool bPlainName = params._golden.empty() == false && params._golden.find_first_of( "/\\.:" ) == string::npos;
        if ( bPlainName == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "golden must be a plain name (no path, no dot)", params._golden } );
        ExpectImageArgs args;
        args._golden = params._golden;
        if ( step.findAttribute( "tolerance" ) != nullptr )
            args._maxDifferingFraction = static_cast<float64>( params._tolerance );
        if ( step.findAttribute( "maxMeanDistance" ) != nullptr )
            args._maxMeanDistance = static_cast<float64>( params._maxMeanDistance );
        if ( step.findAttribute( "maxRegionPixels" ) != nullptr )
            args._maxDeepRegionPixels = params._maxRegionPixels;
        if ( params._region.empty() == false && AutomationImage::tryParseRegion( params._region, args._region ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "region must be x0,y0,x1,y1 in 0..1 with x0<x1 and y0<y1", params._region } );
        return args;
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
            case GlobalVariableType::DiskPath:
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
