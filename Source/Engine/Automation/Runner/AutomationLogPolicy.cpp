#include "pch.h"

#include "Engine/Automation/Runner/AutomationLogPolicy.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

namespace sw
{
    void AutomationLogPolicy::declareExpected( string_view contains )
    {
        if ( contains.empty() )
            return;
        _listExpected.push_back( string( contains ) );
    }

    void AutomationLogPolicy::declareExpectedID( string_view id )
    {
        if ( id.empty() )
            return;
        _listExpectedID.push_back( string( id ) );
    }

    void AutomationLogPolicy::allowID( string_view id )
    {
        if ( id.empty() )
            return;
        _listAllowedID.push_back( string( id ) );
    }

    void AutomationLogPolicy::clear()
    {
        _listExpected.clear();
        _listExpectedID.clear();
        _listAllowedID.clear();
    }

    bool AutomationLogPolicy::isExemptMessage( string_view message )
    {
        return StringUtil::startsWith( message, kRunnerPrefix );
    }

    bool AutomationLogPolicy::isDeclared( string_view text ) const
    {
        return VectorUtil::containsIf( _listExpected, [&]( const string& expected )
        { return text == string_view( expected ); } );
    }

    bool AutomationLogPolicy::isDeclaredID( string_view id ) const
    {
        return id.empty() == false && VectorUtil::containsIf( _listExpectedID, [&]( const string& expected )
        { return string_view( expected ) == id; } );
    }

    AutomationLogVerdict AutomationLogPolicy::classify( LogLevel level, string_view id, string_view message ) const
    {
        if ( isGatedLevel( level ) == false || isExemptMessage( message ) )
            return AutomationLogVerdict::NotGated;
        // 선언은 줄을 정확히 가리킨다: id 가 있는 줄은 id 로만, id 없는 줄은 본문 전체가 같을 때만(부분 문자열은 다른 줄까지 가린다).
        if ( id.empty() ? isDeclared( message ) : isDeclaredID( id ) )
            return AutomationLogVerdict::Declared;
        if ( id.empty() )
            return AutomationLogVerdict::MissingID;
        return VectorUtil::contains( _listAllowedID, string( id ) ) ? AutomationLogVerdict::Allowed : AutomationLogVerdict::NotAllowed;
    }

    void AutomationLogPolicy::judge( LogLevel level, string_view caller, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const
    {
        const AutomationLogVerdict verdict = classify( level, id, message );
        if ( verdict == AutomationLogVerdict::Allowed )
        {
            const string idText{ id };
            if ( VectorUtil::contains( inoutJudgement._listAllowedID, idText ) == false )
                inoutJudgement._listAllowedID.push_back( idText );
            return;
        }
        if ( verdict != AutomationLogVerdict::MissingID && verdict != AutomationLogVerdict::NotAllowed )
            return;
        const bool bMissingID = verdict == AutomationLogVerdict::MissingID;
        if ( bMissingID )
            ++inoutJudgement._missingIDCount;
        else
            ++inoutJudgement._notAllowedCount;
        const string idText = bMissingID ? string( "(no log id)" ) : "(id=" + string( id ) + ")";
        inoutJudgement._listUnhandled.push_back( string( level == LogLevel::Error ? "[Error] [" : "[Warning] [" ) + string( caller ) + "] " + idText + " " + string( message ) );
    }
} // namespace sw
