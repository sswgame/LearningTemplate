#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/Model/AdminToolUtil.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include <limits>

namespace sw::editor
{
    bool AdminToolUtil::parseAccountID( string_view text, AccountID& outAccountID )
    {
        uint64 value = 0;
        if ( StringUtil::parseUint64( text, value, 16 ) == false || value == kInvalidAccountID )
            return false;
        outAccountID = value;
        return true;
    }

    string AdminToolUtil::formatAccountID( AccountID accountID )
    {
        StringBuilder<constant::buffer::kMax32> text;
        text.appendFormat( "%#", Fmt( accountID, Format( 16, Format::Padding::Zero ).hex() ) );
        return string( text.view() );
    }

    string AdminToolUtil::formatBalance( const LedgerBalance& balance )
    {
        StringBuilder<constant::buffer::kMax64> text;
        text.appendFormat( "%#%#", balance._amount, balance.isDebt() ? " (debt)" : "" );
        return string( text.view() );
    }

    string AdminToolUtil::formatClock( int64 unixMs )
    {
        // 서버가 준 값이다 - 1970 앞의 시각에도 음수 칸이 나오지 않게 하루 안으로 접는다.
        const int64                             secondOfDay = ( unixMs / constant::time::kMillisecondsPerSecond % constant::time::kSecondsPerDay + constant::time::kSecondsPerDay ) % constant::time::kSecondsPerDay;
        const Format                            twoDigits( 2, Format::Padding::Zero ); // 예전에는 채움 없이 "9:5:7" 이었다
        StringBuilder<constant::buffer::kMax32> text;
        text.appendFormat( "%#:%#:%#", Fmt( static_cast<int32>( secondOfDay / constant::time::kSecondsPerHour ), twoDigits ),
                           Fmt( static_cast<int32>( secondOfDay % constant::time::kSecondsPerHour / constant::time::kSecondsPerMinute ), twoDigits ),
                           Fmt( static_cast<int32>( secondOfDay % constant::time::kSecondsPerMinute ), twoDigits ) );
        return string( text.view() );
    }

    string AdminToolUtil::formatSanction( int64 untilMs, int64 nowMs )
    {
        if ( untilMs == 0 )
            return "none";
        if ( untilMs == ServiceSanctionState::kPermanentMs )
            return "permanent";
        const int64 remainingSeconds = ( untilMs - nowMs ) / constant::time::kMillisecondsPerSecond;
        if ( remainingSeconds <= 0 )
            return "expired";
        StringBuilder<constant::buffer::kMax64> text;
        text.appendFormat( "%# s left", remainingSeconds );
        return string( text.view() );
    }

    bool AdminToolUtil::tryMakeEndMs( int64 nowMs, int64 durationSeconds, int64& outEndMs )
    {
        if ( durationSeconds < 0 )
            return false;
        if ( durationSeconds == 0 )
        {
            outEndMs = 0;
            return true;
        }
        const int64 maxSeconds = ( std::numeric_limits<int64>::max() - nowMs ) / constant::time::kMillisecondsPerSecond;
        if ( durationSeconds > maxSeconds )
            return false;
        outEndMs = nowMs + durationSeconds * constant::time::kMillisecondsPerSecond;
        return true;
    }

    const utf8* AdminToolUtil::getErrorName( uint16 errorCode )
    {
        switch ( errorCode )
        {
            case OnlineError::kUnauthenticated:
                return "Unauthenticated";
            case OnlineError::kForbidden:
                return "Forbidden";
            case OnlineError::kRateLimited:
                return "RateLimited";
            case OnlineError::kInvalidRequest:
                return "InvalidRequest";
            case OnlineError::kUnavailable:
                return "Unavailable";
            case OnlineError::kVersionMismatch:
                return "VersionMismatch";
            case OnlineError::kUpdateRequired:
                return "UpdateRequired";
            case OnlineError::kNotFound:
                return "NotFound";
            case OnlineError::kConflict:
                return "Conflict";
            case OnlineError::kFeatureDisabled:
                return "FeatureDisabled";
            case OnlineError::kBusy:
                return "Busy";
            case OnlineError::kInternal:
                return "Internal";
            default:
                return "Error";
        }
    }

    const utf8* AdminToolUtil::getMethodName( uint16 method )
    {
        if ( method == AdminMethod::LookupAccount::kID )
            return "LookupAccount";
        if ( method == AdminMethod::AdjustAsset::kID )
            return "AdjustAsset";
        if ( method == AdminMethod::SetSanction::kID )
            return "SetSanction";
        if ( method == AdminMethod::ListAudit::kID )
            return "ListAudit";
        if ( method == AdminMethod::ListReports::kID || method == AdminMethod::ResolveReport::kID )
            return "Report";
        if ( AdminMethod::isDirectoryMethod( method ) )
            return "ServerDirectory";
        if ( AdminMethod::isLiveEventMethod( method ) )
            return "LiveEvent";
        return "Command";
    }
} // namespace sw::editor
