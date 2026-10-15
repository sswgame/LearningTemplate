#include "pch.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminToolSession.h"

#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Reflection/EnumName.h"

namespace sw::editor
{
    namespace
    {
        struct AdminToolSessionInternal
        {

            static const utf8* getMethodName( uint16 method )
            {
                if ( method == AdminMethod::kLookupAccount )
                    return "LookupAccount";
                if ( method == AdminMethod::kAdjustAsset )
                    return "AdjustAsset";
                if ( method == AdminMethod::kSetSanction )
                    return "SetSanction";
                if ( method == AdminMethod::kListAudit )
                    return "ListAudit";
                return "Command";
            }
        };
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
        status.appendFormat( "%#: %#", AdminToolSessionInternal::getMethodName( reply._method ), getEnumName( reply._reply._result ) );
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
        else if ( _bHasLookup && _lookupAccountID != kInvalidAccountID )
        {
            requestLookup( _lookupAccountID ); // 잔액과 제재를 바뀐 값으로 새로 고친다
        }
    }

} // namespace sw::editor
