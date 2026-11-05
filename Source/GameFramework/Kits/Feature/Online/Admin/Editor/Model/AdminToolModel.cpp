#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/Model/AdminToolModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include <limits>

namespace sw::editor
{
    bool AdminToolModel::parseAccountID( string_view text, AccountID& outAccountID )
    {
        uint64 value = 0;
        if ( StringUtil::parseUint64( text, value, 16 ) == false || value == kInvalidAccountID )
            return false;
        outAccountID = value;
        return true;
    }

    string AdminToolModel::formatAccountID( AccountID accountID )
    {
        StringBuilder<constant::buffer::kMax32> text;
        text.appendFormat( "%#", Fmt( accountID, Format( 16, Format::Padding::Zero ).hex() ) );
        return string( text.view() );
    }

    string AdminToolModel::formatBalance( const LedgerBalance& balance )
    {
        StringBuilder<constant::buffer::kMax64> text;
        text.appendFormat( "%#%#", balance._amount, balance.isDebt() ? " (debt)" : "" );
        return string( text.view() );
    }

    string AdminToolModel::formatClock( int64 unixMs )
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

    string AdminToolModel::formatSanction( int64 untilMs, int64 nowMs )
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

    bool AdminToolModel::tryMakeEndMs( int64 nowMs, int64 durationSeconds, int64& outEndMs )
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

    const utf8* AdminToolModel::getMethodName( uint16 method )
    {
        if ( method == AdminMethod::kLookupAccount )
            return "LookupAccount";
        if ( method == AdminMethod::kAdjustAsset )
            return "AdjustAsset";
        if ( method == AdminMethod::kSetSanction )
            return "SetSanction";
        if ( method == AdminMethod::kListAudit )
            return "ListAudit";
        if ( method == AdminMethod::kListReports || method == AdminMethod::kResolveReport )
            return "Report";
        if ( AdminMethod::isDirectoryMethod( method ) )
            return "ServerDirectory";
        if ( AdminMethod::isLiveEventMethod( method ) )
            return "LiveEvent";
        return "Command";
    }
} // namespace sw::editor
