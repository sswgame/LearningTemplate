#include "pch.h"

#include "Engine/Automation/Runner/AutomationLogPolicy.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

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

    AutomationLogResult AutomationLogPolicy::classify( LogLevel level, string_view id, string_view message ) const
    {
        if ( isGatedLevel( level ) == false || isExemptMessage( message ) )
            return AutomationLogResult::NotGated;
        // 선언은 id 로만 한다(문장은 다른 줄까지 가리거나 문구를 고치면 낡는다).
        if ( isDeclaredID( id ) )
            return AutomationLogResult::Declared;
        if ( id.empty() )
            return AutomationLogResult::MissingID;
        return VectorUtil::contains( _listAllowedID, string( id ) ) ? AutomationLogResult::Allowed : AutomationLogResult::NotAllowed;
    }

    void AutomationLogPolicy::judge( LogLevel level, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const
    {
        const AutomationLogResult result = classify( level, id, message );
        if ( result == AutomationLogResult::Allowed )
        {
            const string idText{ id };
            if ( VectorUtil::contains( inoutJudgement._listAllowedID, idText ) == false )
                inoutJudgement._listAllowedID.push_back( idText );
            return;
        }
        if ( result != AutomationLogResult::MissingID && result != AutomationLogResult::NotAllowed )
            return;
        const bool bMissingID = result == AutomationLogResult::MissingID;
        if ( bMissingID )
            ++inoutJudgement._missingIDCount;
        else
            ++inoutJudgement._notAllowedCount;
        const string idText = bMissingID ? string( "(no log id)" ) : "(id=" + string( id ) + ")";
        inoutJudgement._listUnhandled.push_back( string( level == LogLevel::Error ? "[Error] " : "[Warning] " ) + idText + " " + string( message ) );
    }
} // namespace sw
