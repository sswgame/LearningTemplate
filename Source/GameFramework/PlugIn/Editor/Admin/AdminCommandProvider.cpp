#include "pch.h"

#include "GameFramework/PlugIn/Editor/Admin/AdminCommandProvider.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

namespace sw::editor
{
    namespace
    {
        struct AdminCommandProviderInternal
        {
            static constexpr int64 kSeedPlayerGold = 100;

            static void deliver( const AdminClient::ReplyDelegate& onReply, uint16 method, const NetIdempotencyKey& key, uint16 errorCode, const AdminReply& reply )
            {
                if ( onReply.isBound() == false )
                    return;
                AdminClient::Reply clientReply;
                clientReply._reply          = reply;
                clientReply._idempotencyKey = key;
                clientReply._requestID      = 0;
                clientReply._method         = method;
                clientReply._errorCode      = errorCode;
                onReply( clientReply );
            }

            static string formatNumber( int64 value )
            {
                StringBuilder<constant::kMaxBuffer64> text;
                text.appendFormat( "%#", value );
                return string( text.view() );
            }

            /** @brief 서버 감사 기록의 주체 모양(`acct.<16진>/…`)입니다. 패널의 감사 열람이 같은 접두어로 거릅니다. */
            static string makeAccountSubject( AccountID accountID )
            {
                StringBuilder<constant::kMaxBuffer64> text;
                text.appendFormat( "acct.%#/admin", Fmt( accountID, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.view() );
            }

            static LedgerBalance* findBalance( vector<LedgerBalance>& inoutListBalance, string_view assetID )
            {
                return VectorUtil::findIf( inoutListBalance, [&]( LedgerBalance& balance )
                { return string_view( balance._assetID ) == assetID; } );
            }

            static bool isRoleAtLeast( AdminRole role, AdminRole required )
            {
                return static_cast<uint8>( required ) < static_cast<uint8>( AdminRole::Count ) && static_cast<uint8>( role ) >= static_cast<uint8>( required );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AdminClientProvider::AdminClientProvider()
        : _pClient{ nullptr }
    {
    }

    void AdminClientProvider::bind( AdminClient* pClient )
    {
        _pClient = pClient;
    }

    void AdminClientProvider::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReply )
    {
        if ( _pClient == nullptr )
        {
            AdminReply reply;
            reply._result = AdminResult::Unavailable;
            AdminCommandProviderInternal::deliver( onReply, method, key, OnlineError::kUnavailable, reply );
            return;
        }
        (void)_pClient->sendCommand( method, request, key, onReply ); // 요청 번호는 응답 콜백이 받는다
    }
} // namespace sw::editor

namespace sw::editor
{
    FakeAdminProvider::FakeAdminProvider( int64 nowMs )
        : _listAccount{}
        , _listAudit{}
        , _listRecorded{}
        , _nowMs{ nowMs }
        , _actingRole{ AdminRole::Operator }
    {
        FakeAdminAccount playerOne;
        playerOne._accountID   = kSeedPlayerID;
        playerOne._displayName = "Player One";
        LedgerBalance gold;
        gold._assetID = "cur.gold";
        gold._amount  = AdminCommandProviderInternal::kSeedPlayerGold;
        gold._version = 1;
        playerOne._listBalance.push_back( gold );
        _listAccount.push_back( std::move( playerOne ) );

        FakeAdminAccount playerTwo;
        playerTwo._accountID   = kSeedOtherID;
        playerTwo._displayName = "Player Two";
        _listAccount.push_back( std::move( playerTwo ) );
    }

    const FakeAdminAccount* FakeAdminProvider::findAccount( AccountID accountID ) const
    {
        return VectorUtil::findBy<&FakeAdminAccount::_accountID>( _listAccount, accountID );
    }

    FakeAdminAccount* FakeAdminProvider::findMutableAccount( AccountID accountID )
    {
        return VectorUtil::findBy<&FakeAdminAccount::_accountID>( _listAccount, accountID );
    }

    void FakeAdminProvider::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, AdminClient::ReplyDelegate onReply )
    {
        const bool              bMutating = AdminMethod::isMutating( method );
        const NetIdempotencyKey usedKey   = bMutating && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        AdminReply              reply;
        if ( bMutating )
        {
            for ( const RecordedReply& recorded : _listRecorded )
            {
                if ( recorded._key == usedKey )
                {
                    reply            = recorded._reply;
                    reply._bReplayed = SW_TRUE;
                    AdminCommandProviderInternal::deliver( onReply, method, usedKey, 0, reply );
                    return;
                }
            }
        }

        reply._result = execute( method, request, reply );
        if ( bMutating && reply._result == AdminResult::Ok )
        {
            RecordedReply recorded;
            recorded._key   = usedKey;
            recorded._reply = reply;
            _listRecorded.push_back( std::move( recorded ) );
        }
        AdminCommandProviderInternal::deliver( onReply, method, usedKey, 0, reply );
    }

    AdminResult FakeAdminProvider::execute( uint16 method, const AdminRequest& request, AdminReply& outReply )
    {
        const AdminRole required = AdminProtocol::getRequiredRole( method, request._sanctionKind );
        if ( AdminCommandProviderInternal::isRoleAtLeast( _actingRole, required ) == false )
            return AdminResult::Forbidden;
        if ( AdminMethod::isMutating( method ) )
        {
            const bool bMemoValid = request._memo.empty() == false && static_cast<int32>( request._memo.size() ) <= AdminProtocol::kMaxMemoSize;
            if ( bMemoValid == false )
                return AdminResult::InvalidRequest;
        }
        if ( method == AdminMethod::kLookupAccount )
            return executeLookup( request, outReply );
        if ( method == AdminMethod::kAdjustAsset )
            return executeAdjust( request, outReply );
        if ( method == AdminMethod::kSetSanction )
            return executeSanction( request, outReply );
        if ( method == AdminMethod::kListAudit )
            return executeListAudit( request, outReply );
        return AdminResult::Unavailable; // 우편, 캠페인, 권한은 서버 저장소가 필요하다
    }

    AdminResult FakeAdminProvider::executeLookup( const AdminRequest& request, AdminReply& outReply ) const
    {
        const FakeAdminAccount* pFound = nullptr;
        if ( request._accountID != kInvalidAccountID )
        {
            pFound = findAccount( request._accountID );
        }
        else if ( request._displayName.empty() == false )
        {
            for ( const FakeAdminAccount& account : _listAccount )
            {
                if ( account._displayName == request._displayName )
                {
                    pFound = &account;
                    break;
                }
            }
        }
        if ( pFound == nullptr )
            return AdminResult::UnknownAccount;
        outReply._identity._accountID   = pFound->_accountID;
        outReply._identity._displayName = pFound->_displayName;
        outReply._listBalance           = pFound->_listBalance;
        outReply._sanction              = pFound->_sanction;
        outReply._bOnline               = SW_FALSE;
        return AdminResult::Ok;
    }

    AdminResult FakeAdminProvider::executeAdjust( const AdminRequest& request, AdminReply& outReply )
    {
        FakeAdminAccount* pAccount = findMutableAccount( request._accountID );
        if ( pAccount == nullptr )
            return AdminResult::UnknownAccount;
        const bool bAssetValid = request._assetID.empty() == false && static_cast<int32>( request._assetID.size() ) <= LedgerConstant::kMaxAssetIDSize;
        if ( bAssetValid == false || request._amount == 0 )
            return AdminResult::InvalidRequest;
        const int64 magnitude = request._amount < 0 ? -request._amount : request._amount;
        if ( magnitude > kMaxAdjustAmount )
            return AdminResult::CapExceeded;

        LedgerBalance* pBalance = AdminCommandProviderInternal::findBalance( pAccount->_listBalance, request._assetID );
        const int64    before   = pBalance != nullptr ? pBalance->_amount : 0;
        const int64    after    = before + request._amount;
        const bool     bRefund  = request._bRefund == SW_TRUE;
        if ( after < 0 && bRefund == false )
            return AdminResult::InsufficientFunds;
        if ( after > LedgerConstant::kMaxBalance )
            return AdminResult::CapExceeded;

        if ( pBalance == nullptr )
        {
            LedgerBalance created;
            created._assetID = request._assetID;
            pAccount->_listBalance.push_back( std::move( created ) );
            pBalance = &pAccount->_listBalance.back();
        }
        pBalance->_amount = after;
        ++pBalance->_version;

        appendAudit( "admin.adjust", request, AdminCommandProviderInternal::formatNumber( before ), AdminCommandProviderInternal::formatNumber( after ) );
        outReply._listBalance = pAccount->_listBalance;
        return AdminResult::Ok;
    }

    AdminResult FakeAdminProvider::executeSanction( const AdminRequest& request, AdminReply& outReply )
    {
        FakeAdminAccount* pAccount = findMutableAccount( request._accountID );
        if ( pAccount == nullptr )
            return AdminResult::UnknownAccount;
        if ( request._untilMs < 0 || request._sanctionKind == ServiceSanctionKind::Count )
            return AdminResult::InvalidRequest;

        const int32 kindIndex = static_cast<int32>( request._sanctionKind );
        const int64 before    = pAccount->_sanction._arrUntilMs[kindIndex];
        const bool  bBan      = request._sanctionKind == ServiceSanctionKind::Ban && request._untilMs != 0;
        const int64 after     = bBan ? ServiceSanctionState::kPermanentMs : request._untilMs;

        pAccount->_sanction._arrUntilMs[kindIndex] = after;
        pAccount->_sanction._reasonCode            = request._reasonCode;
        ++pAccount->_sanction._version;

        appendAudit( "admin.sanction", request, AdminCommandProviderInternal::formatNumber( before ), AdminCommandProviderInternal::formatNumber( after ) );
        outReply._sanction = pAccount->_sanction;
        return AdminResult::Ok;
    }

    AdminResult FakeAdminProvider::executeListAudit( const AdminRequest& request, AdminReply& outReply ) const
    {
        const int32 limit = request._maxCount > 0 && request._maxCount <= AdminProtocol::kMaxListCount ? request._maxCount : AdminProtocol::kMaxListCount;
        // 최근 기록이 먼저 온다.
        for ( vector<ServiceAuditEntry>::const_reverse_iterator it = _listAudit.rbegin(); it != _listAudit.rend(); ++it )
        {
            if ( static_cast<int32>( outReply._listAudit.size() ) >= limit )
                break;
            if ( request._subject.empty() == false && StringUtil::startsWith( it->_subject, request._subject ) == false )
                continue;
            outReply._listAudit.push_back( *it );
        }
        return AdminResult::Ok;
    }

    void FakeAdminProvider::appendAudit( const utf8* pAction, const AdminRequest& request, string_view before, string_view after )
    {
        ServiceAuditEntry entry;
        entry._actor   = "gm.fake";
        entry._action  = pAction;
        entry._subject = AdminCommandProviderInternal::makeAccountSubject( request._accountID );
        entry._before  = string( before );
        entry._after   = string( after );
        entry._memo    = request._memo;
        entry._timeMs  = getNowMs();
        _listAudit.push_back( std::move( entry ) );
    }
} // namespace sw::editor
