#include "pch.h"

#include "Engine/Automation/Runner/AutomationLogPolicy.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

namespace sw
{
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
        _listExpectedID.clear();
        _listAllowedID.clear();
    }

    bool AutomationLogPolicy::isExemptMessage( string_view message )
    {
        return StringUtil::startsWith( message, kRunnerPrefix );
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
        // 선언은 id 로만 한다(문장은 다른 줄까지 가리거나 문구를 고치면 낡는다).
        if ( isDeclaredID( id ) )
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
