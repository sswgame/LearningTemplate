#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolSession.h"

#include "Core/String/StringBuilder.h"
#include "Core/Time/Clock.h"
#include "Core/Time/WallClock.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/Model/AdminToolModel.h"

namespace sw::editor
{
    AdminToolSession::AdminToolSession()
        : _fake{ WallClock::nowUnixMilliseconds().count() }
        , _live{}
        , _pActive{ &_fake }
        , _connection{}
        , _onCredentials{}
        , _onFrame{}
        , _lookup{}
        , _lookupAudit{}
        , _reports{}
        , _liveEvents{}
        , _statusLine{}
        , _completedCount{ 0 }
        , _pendingCount{ 0 }
        , _lookupAccountID{ kInvalidAccountID }
        , _actedAccountID{ kInvalidAccountID }
        , _lastResult{ AdminResult::Ok }
        , _lastMethod{ 0 }
        , _bHasLookup{ SW_FALSE }
        , _bLastReplayed{ SW_FALSE }
    {
    }

    AdminToolSession::~AdminToolSession()
    {
        // 연결을 닫으면 날아가던 요청의 답(Cancelled)이 `onReply` 로 온다. 멤버 소멸 순서에 맡기면 `_connection` 보다 뒤에 선언한 결과 칸이 먼저 사라져 사라진 칸에 썼다.
        disconnectLive();
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

    expected<void> AdminToolSession::connectLive( string_view address, string_view gameBuild )
    {
        if ( expected<void> opened = _connection.open( address, gameBuild ); opened.has_value() == false )
        {
            useFake();
            _statusLine = "GM connection failed: " + string( opened.error().message() ) + " (" + string( opened.error().detail() ) + ")";
            return opened;
        }
        useLiveClient( _connection.getAdminClient() );
        _statusLine = "Connecting to the GM port...";
        return {};
    }

    expected<void> AdminToolSession::connectWithLogin( string_view address, string_view gameBuild, string_view loginName, string_view password )
    {
        const utf8* pRefusal = nullptr;
        if ( _onCredentials.isBound() == false )
            pRefusal = "no login handler is bound - the tool assembly must bind one (bindCredentialsHandler)";
        else if ( loginName.empty() || password.empty() )
            pRefusal = "the account name and the password are required";
        else if ( _onCredentials.get()( loginName, password ) == false )
            pRefusal = "the login handler refused the account name or the password";
        if ( pRefusal != nullptr )
        {
            _statusLine = string( "GM connection failed: " ) + pRefusal;
            return unexpected( Error{ ErrorKind::InvalidArgument, pRefusal } );
        }
        return connectLive( address, gameBuild );
    }

    void AdminToolSession::disconnectLive()
    {
        useFake(); // 클라이언트를 가리키는 제공자를 먼저 떼고 닫는다
        _live.bind( nullptr );
        _connection.close();
        _statusLine = "Back to the in-memory provider";
    }

    void AdminToolSession::updateConnection()
    {
        _connection.update( SystemMonotonicClock{}.nowMilliseconds() );
        if ( _onFrame.isBound() )
            _onFrame.get()();
    }

    AdminRequest AdminToolSession::makeLookupRequest( AccountID accountID )
    {
        AdminRequest request;
        request._accountID = accountID;
        return request;
    }

    AdminRequest AdminToolSession::makeReportsRequest( bool bOpen )
    {
        AdminRequest request;
        request._reportStatus = bOpen ? ServiceReportStatus::Open : ServiceReportStatus::Resolved;
        request._maxCount     = AdminProtocol::kMaxReportListCount;
        return request;
    }

    void AdminToolSession::requestLookup( AccountID accountID )
    {
        send( AdminMethod::LookupAccount::kID, makeLookupRequest( accountID ), NetIdempotencyKey{} );
    }

    void AdminToolSession::requestAdjust( AccountID accountID, string_view assetID, int64 amount, bool bRefund, string_view memo )
    {
        AdminRequest request;
        request._accountID = accountID;
        request._assetID   = string( assetID );
        request._amount    = amount;
        request._bRefund   = bRefund ? SW_TRUE : SW_FALSE;
        request._memo      = string( memo );
        _actedAccountID    = accountID;
        send( AdminMethod::AdjustAsset::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestSanction( AccountID accountID, ServiceSanctionKind kind, int64 durationSeconds, string_view reasonCode, string_view memo )
    {
        AdminRequest request;
        request._accountID    = accountID;
        request._sanctionKind = kind;
        request._reasonCode   = string( reasonCode );
        request._memo         = string( memo );
        // 예전에는 음수 기간이 0(해제)이 되고 큰 기간은 넘쳐 음수 시각이 되었다.
        if ( AdminToolModel::tryMakeEndMs( WallClock::nowUnixMilliseconds().count(), durationSeconds, request._untilMs ) == false )
        {
            rejectLocally( AdminMethod::SetSanction::kID );
            return;
        }
        _actedAccountID = accountID;
        send( AdminMethod::SetSanction::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestAudit( string_view subjectPrefix )
    {
        AdminRequest request;
        request._subject  = string( subjectPrefix );
        request._maxCount = kAuditRequestCount;
        send( AdminMethod::ListAudit::kID, request, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestReports( bool bOpen )
    {
        send( AdminMethod::ListReports::kID, makeReportsRequest( bOpen ), NetIdempotencyKey{} );
    }

    void AdminToolSession::requestResolveReport( string_view reportKey, ServiceReportStatus status, string_view memo )
    {
        AdminRequest request;
        request._reportKey    = string( reportKey );
        request._reportStatus = status;
        request._memo         = string( memo );
        send( AdminMethod::ResolveReport::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestSetMaintenance( string_view scope, string_view messageKey, int64 durationSeconds, string_view memo )
    {
        const int64  nowMs = WallClock::nowUnixMilliseconds().count();
        AdminRequest request;
        request._scope      = string( scope );
        request._messageKey = string( messageKey );
        request._startMs    = nowMs;
        request._memo       = string( memo );
        // 예전에는 음수 기간이 0(해제할 때까지)이 되어 끝나지 않는 점검이 걸렸다.
        if ( AdminToolModel::tryMakeEndMs( nowMs, durationSeconds, request._endMs ) == false )
        {
            rejectLocally( AdminMethod::SetMaintenance::kID );
            return;
        }
        send( AdminMethod::SetMaintenance::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestClearMaintenance( string_view scope, string_view memo )
    {
        AdminRequest request;
        request._scope = string( scope );
        request._memo  = string( memo );
        send( AdminMethod::ClearMaintenance::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestPostAnnouncement( uint64 announcementID, string_view text, int32 priority, int64 durationSeconds, string_view memo )
    {
        const int64  nowMs = WallClock::nowUnixMilliseconds().count();
        AdminRequest request;
        request._announcementID = announcementID;
        request._body           = string( text );
        request._priority       = priority;
        request._bLiteralText   = SW_TRUE;
        request._startMs        = nowMs;
        request._memo           = string( memo );
        if ( AdminToolModel::tryMakeEndMs( nowMs, durationSeconds, request._endMs ) == false )
        {
            rejectLocally( AdminMethod::PostAnnouncement::kID );
            return;
        }
        send( AdminMethod::PostAnnouncement::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestRemoveAnnouncement( uint64 announcementID, string_view memo )
    {
        AdminRequest request;
        request._announcementID = announcementID;
        request._memo           = string( memo );
        send( AdminMethod::RemoveAnnouncement::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestLiveEvents()
    {
        send( AdminMethod::ListLiveEvents::kID, AdminRequest{}, NetIdempotencyKey{} );
    }

    void AdminToolSession::requestPutLiveEvent( string_view definitionJSON, string_view memo )
    {
        AdminRequest request;
        request._eventJSON = string( definitionJSON );
        request._memo      = string( memo );
        send( AdminMethod::PutLiveEvent::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::requestRemoveLiveEvent( string_view eventID, string_view memo )
    {
        AdminRequest request;
        request._eventID = string( eventID );
        request._memo    = string( memo );
        send( AdminMethod::RemoveLiveEvent::kID, request, NetIdempotencyKey::makeRandom() );
    }

    void AdminToolSession::send( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key )
    {
        sendWith( method, request, key, AdminClient::ReplyDelegate::create<&AdminToolSession::onReply>( this ) );
    }

    void AdminToolSession::sendRefresh( uint16 method, const AdminRequest& request )
    {
        sendWith( method, request, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&AdminToolSession::onRefreshReply>( this ) );
    }

    void AdminToolSession::sendWith( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReplyDelegate )
    {
        ++_pendingCount;
        _fake.setNowMs( WallClock::nowUnixMilliseconds().count() ); // 메모리 제공자는 시계를 읽지 않는다 — 도구의 뿌리인 세션이 명령마다 넣는다
        _pActive->sendCommand( method, request, key, std::move( onReplyDelegate ) );
    }

    string AdminToolSession::makeReplyLine( const AdminClient::Reply& reply )
    {
        StringBuilder<constant::buffer::kMax128> line;
        if ( reply._errorCode != OnlineError::kOk )
            line.appendFormat( "%#: error %#", AdminToolModel::getMethodName( reply._method ), reply._errorCode ); // 공통 범주(권한 · 형식 · 장애)는 업무 결과가 없다
        else
            line.appendFormat( "%#: %#", AdminToolModel::getMethodName( reply._method ), getEnumName( reply._reply._result ) );
        return string( line.view() );
    }

    void AdminToolSession::rejectLocally( uint16 method )
    {
        _lastResult    = AdminServiceResult::fail( OnlineError::kInvalidRequest );
        _lastMethod    = method;
        _bLastReplayed = SW_FALSE;
        _statusLine    = string( AdminToolModel::getMethodName( method ) ) + ": InvalidRequest (the duration must be zero or a positive number that fits)";
        SW_LOG_INFO_ID( "Admin.CommandResult", "Admin command %#", _statusLine.c_str() );
    }

    void AdminToolSession::onReply( const AdminClient::Reply& reply )
    {
        if ( _pendingCount > 0 )
            --_pendingCount;
        ++_completedCount;
        _lastResult    = reply._errorCode != OnlineError::kOk ? AdminServiceResult::fail( reply._errorCode ) : AdminServiceResult{ reply._reply._result };
        _lastMethod    = reply._method;
        _bLastReplayed = reply._reply._bReplayed == SW_TRUE;

        _statusLine = makeReplyLine( reply );
        SW_LOG_INFO_ID( "Admin.CommandResult", "Admin command %#", _statusLine.c_str() );

        if ( _lastResult != AdminResult::Ok )
            return;
        storeResult( reply );
        // 새로 고침은 결과 칸을 덮지 않는 길로 보낸다. 예전에는 같은 길이라 지급 뒤의 상태 줄 · 결과 · 재전송 표시가 조회의 것이 되어 GM 이 명령의 답을 보지 못했다.
        if ( reply._method == AdminMethod::ResolveReport::kID )
        {
            sendRefresh( AdminMethod::ListReports::kID, makeReportsRequest( true ) ); // 처리한 신고가 열린 목록에서 빠진다
        }
        else if ( reply._method == AdminMethod::PutLiveEvent::kID || reply._method == AdminMethod::RemoveLiveEvent::kID )
        {
            sendRefresh( AdminMethod::ListLiveEvents::kID, AdminRequest{} ); // 서버가 다시 읽은 뒤의 목록이다. 다른 서버는 주기와 버스로 따라온다
        }
        else if ( reply._method == AdminMethod::AdjustAsset::kID || reply._method == AdminMethod::SetSanction::kID )
        {
            // 명령의 대상을 다시 읽는다. 예전에는 보이던 계정을 읽어, 다른 계정에 지급하면 화면이 그 결과를 보이지 않았고 조회 전이면 아무것도 읽지 않았다. 공지 · 점검 뒤에도 쓸데없이 읽었다.
            sendRefresh( AdminMethod::LookupAccount::kID, makeLookupRequest( _actedAccountID ) );
        }
    }

    void AdminToolSession::onRefreshReply( const AdminClient::Reply& reply )
    {
        if ( _pendingCount > 0 )
            --_pendingCount;
        ++_completedCount;
        const string line = makeReplyLine( reply );
        SW_LOG_INFO_ID( "Admin.CommandResult", "Admin command %#", line.c_str() );
        if ( reply._errorCode == OnlineError::kOk && reply._reply._result == AdminResult::Ok )
            storeResult( reply );
        else
            _statusLine += " - refresh failed: " + line; // 명령은 끝났고 화면이 낡았다는 뜻이다
    }

    void AdminToolSession::storeResult( const AdminClient::Reply& reply )
    {
        if ( reply._method == AdminMethod::LookupAccount::kID )
        {
            _lookup          = reply._reply;
            _lookupAccountID = reply._reply._identity._accountID; // 실패한 조회는 보이는 계정을 바꾸지 않는다
            _bHasLookup      = SW_TRUE;
        }
        else if ( reply._method == AdminMethod::ListAudit::kID )
        {
            _lookupAudit = reply._reply;
        }
        else if ( reply._method == AdminMethod::ListReports::kID )
        {
            _reports = reply._reply;
        }
        else if ( reply._method == AdminMethod::ListLiveEvents::kID )
        {
            _liveEvents = reply._reply;
        }
    }

} // namespace sw::editor
