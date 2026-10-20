#include "pch.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolSession.h"

#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Reflection/EnumName.h"

namespace sw::editor
{
    namespace
    {
        const utf8* getMethodName( uint16 method )
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
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AdminToolSession::AdminToolSession()
        : _fake{}
        , _live{}
        , _pActive{ &_fake }
        , _lookup{}
        , _lookupAudit{}
        , _reports{}
        , _liveEvents{}
        , _statusLine{}
        , _completedCount{ 0 }
        , _pendingCount{ 0 }
        , _lookupAccountID{ kInvalidAccountID }
        , _lastResult{ AdminResult::Ok }
        , _lastMethod{ 0 }
        , _bHasLookup{ false }
        , _bLastReplayed{ false }
    {
    }

    void AdminToolSession::useFake()
    {
        _pActive = &_fake;
    }

    void AdminToolSession::useLiveClient( AdminClient* pClient )
    {
        _live.bind( pClient );
        _pActive = &_live;
    }

    void AdminToolSession::requestLookup( AccountID accountID )
    {
        AdminRequest request;
        request._accountID = accountID;
        send( AdminMethod::kLookupAccount, request, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestAdjust( AccountID accountID, string_view assetID, int64 amount, bool bRefund, string_view memo )
    {
        AdminRequest request;
        request._accountID = accountID;
        request._assetID   = string( assetID );
        request._amount    = amount;
        request._bRefund   = bRefund ? SW_TRUE : SW_FALSE;
        request._memo      = string( memo );
        send( AdminMethod::kAdjustAsset, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestSanction( AccountID accountID, ServiceSanctionKind kind, int64 durationSeconds, string_view reasonCode, string_view memo )
    {
        AdminRequest request;
        request._accountID    = accountID;
        request._sanctionKind = kind;
        request._reasonCode   = string( reasonCode );
        request._memo         = string( memo );
        request._untilMs      = durationSeconds > 0 ? WallClock::nowUnixMilliseconds() + durationSeconds * constant::kMillisecondsPerSecond : 0;
        send( AdminMethod::kSetSanction, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestAudit( string_view subjectPrefix )
    {
        AdminRequest request;
        request._subject  = string( subjectPrefix );
        request._maxCount = kAuditRequestCount;
        send( AdminMethod::kListAudit, request, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestReports( bool bOpen )
    {
        AdminRequest request;
        request._reportStatus = bOpen ? ServiceReportStatus::Open : ServiceReportStatus::Resolved;
        request._maxCount     = AdminProtocol::kMaxReportListCount;
        send( AdminMethod::kListReports, request, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestResolveReport( string_view reportKey, ServiceReportStatus status, string_view memo )
    {
        AdminRequest request;
        request._reportKey    = string( reportKey );
        request._reportStatus = status;
        request._memo         = string( memo );
        send( AdminMethod::kResolveReport, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestSetMaintenance( string_view scope, string_view messageKey, int64 durationSeconds, string_view memo )
    {
        const int64  nowMs = WallClock::nowUnixMilliseconds();
        AdminRequest request;
        request._scope      = string( scope );
        request._messageKey = string( messageKey );
        request._startMs    = nowMs;
        request._endMs      = durationSeconds > 0 ? nowMs + durationSeconds * constant::kMillisecondsPerSecond : 0;
        request._memo       = string( memo );
        send( AdminMethod::kSetMaintenance, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestClearMaintenance( string_view scope, string_view memo )
    {
        AdminRequest request;
        request._scope = string( scope );
        request._memo  = string( memo );
        send( AdminMethod::kClearMaintenance, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestPostNotice( uint64 noticeID, string_view text, int32 priority, int64 durationSeconds, string_view memo )
    {
        const int64  nowMs = WallClock::nowUnixMilliseconds();
        AdminRequest request;
        request._noticeID     = noticeID;
        request._body         = string( text );
        request._priority     = priority;
        request._bLiteralText = SW_TRUE;
        request._startMs      = nowMs;
        request._endMs        = durationSeconds > 0 ? nowMs + durationSeconds * constant::kMillisecondsPerSecond : 0;
        request._memo         = string( memo );
        send( AdminMethod::kPostNotice, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestRemoveNotice( uint64 noticeID, string_view memo )
    {
        AdminRequest request;
        request._noticeID = noticeID;
        request._memo     = string( memo );
        send( AdminMethod::kRemoveNotice, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestLiveEvents()
    {
        send( AdminMethod::kListLiveEvents, AdminRequest{}, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestPutLiveEvent( string_view definitionJSON, string_view memo )
    {
        AdminRequest request;
        request._eventJSON = string( definitionJSON );
        request._memo      = string( memo );
        send( AdminMethod::kPutLiveEvent, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestRemoveLiveEvent( string_view eventID, string_view memo )
    {
        AdminRequest request;
        request._eventID = string( eventID );
        request._memo    = string( memo );
        send( AdminMethod::kRemoveLiveEvent, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::send( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key )
    {
        ++_pendingCount;
        _pActive->sendCommand( method, request, key, AdminClient::ReplyDelegate::create<&AdminToolSession::onReply>( this ) );
    }

    void AdminToolSession::onReply( const AdminClient::Reply& reply )
    {
        if ( _pendingCount > 0 )
            --_pendingCount;
        ++_completedCount;
        _lastResult    = reply._reply._result;
        _lastMethod    = reply._method;
        _bLastReplayed = reply._reply._bReplayed == SW_TRUE;

        StringBuilder<constant::kMaxBuffer128> status;
        status.appendFormat( "%#: %#", getMethodName( reply._method ), getEnumName( reply._reply._result ) );
        if ( reply._errorCode != 0 )
            status.appendFormat( " (error %#)", reply._errorCode );
        _statusLine = string( status.view() );
        SW_LOG_INFO( "Admin command %#", _statusLine.c_str() );

        if ( reply._reply._result != AdminResult::Ok )
            return;
        if ( reply._method == AdminMethod::kLookupAccount )
        {
            _lookup          = reply._reply;
            _lookupAccountID = reply._reply._identity._accountID; // 실패한 조회는 보이는 계정을 바꾸지 않는다
            _bHasLookup      = true;
        }
        else if ( reply._method == AdminMethod::kListAudit )
        {
            _lookupAudit = reply._reply;
        }
        else if ( reply._method == AdminMethod::kListReports )
        {
            _reports = reply._reply;
        }
        else if ( reply._method == AdminMethod::kResolveReport )
        {
            requestReports( true ); // 처리한 신고가 열린 목록에서 빠진다
        }
        else if ( reply._method == AdminMethod::kListLiveEvents )
        {
            _liveEvents = reply._reply;
        }
        else if ( AdminMethod::isLiveEventMethod( reply._method ) )
        {
            requestLiveEvents(); // 서버가 다시 읽은 뒤의 목록이다. 다른 서버는 주기와 버스로 따라온다
        }
        else if ( _bHasLookup && _lookupAccountID != kInvalidAccountID )
        {
            requestLookup( _lookupAccountID ); // 잔액과 제재를 바뀐 값으로 새로 고친다
        }
    }

} // namespace sw::editor
