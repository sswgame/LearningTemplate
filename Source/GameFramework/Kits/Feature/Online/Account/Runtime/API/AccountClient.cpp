#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetEndpointSecurity.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    namespace
    {
        struct AccountClientInternal
        {
            static bool isGrantOperation( AccountClientOperation operation )
            {
                return operation == AccountClientOperation::Login || operation == AccountClientOperation::GuestLogin ||
                       operation == AccountClientOperation::PlatformLogin || operation == AccountClientOperation::Resume;
            }

            /** @brief 서술자 번호를 요청 종류로 바꿉니다. 표의 메서드는 모두 짝이 있다. */
            static AccountClientOperation toOperation( uint16 method )
            {
                switch ( method )
                {
                    case AccountMethod::Register::kID:
                        return AccountClientOperation::Register;
                    case AccountMethod::Login::kID:
                        return AccountClientOperation::Login;
                    case AccountMethod::GuestLogin::kID:
                        return AccountClientOperation::GuestLogin;
                    case AccountMethod::PlatformLogin::kID:
                        return AccountClientOperation::PlatformLogin;
                    case AccountMethod::Resume::kID:
                        return AccountClientOperation::Resume;
                    case AccountMethod::Logout::kID:
                        return AccountClientOperation::Logout;
                    case AccountMethod::LinkCredential::kID:
                        return AccountClientOperation::LinkCredential;
                    case AccountMethod::LinkPlatform::kID:
                        return AccountClientOperation::LinkPlatform;
                    case AccountMethod::IssueGameTicket::kID:
                        return AccountClientOperation::IssueGameTicket;
                    case AccountMethod::UnlinkPlatform::kID:
                        return AccountClientOperation::UnlinkPlatform;
                    case AccountMethod::ListLinks::kID:
                        return AccountClientOperation::ListLinks;
                    case AccountMethod::RequestDeletion::kID:
                        return AccountClientOperation::RequestDeletion;
                    default:
                        return AccountClientOperation::CancelDeletion;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AccountClient::AccountClient()
        : _listReply{}
        , _listEvent{}
        , _clientInfo{}
        , _identity{}
        , _token{}
        , _pServiceClient{ nullptr }
        , _bLoggedIn{ SW_FALSE }
        , _bResuming{ SW_FALSE }
        , _bAutomaticResume{ SW_FALSE }
    {
    }

    void AccountClient::initialize( OnlineServiceClient* pClient, const AccountClientInfo& clientInfo )
    {
        OnlineProtocolClient::initialize( pClient );
        _pServiceClient = pClient;
        _clientInfo     = clientInfo;
    }

    uint64 AccountClient::registerAccount( string_view loginName, string_view password )
    {
        return call<AccountMethod::Register>( AccountCredentialRequest{ string( loginName ), string( password ) },
                                              OnlineReplyDelegate<AccountMethod::Register>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::login( string_view loginName, string_view password )
    {
        return call<AccountMethod::Login>( AccountLoginRequest{ string( loginName ), string( password ), _clientInfo }, OnlineReplyDelegate<AccountMethod::Login>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize] )
    {
        AccountGuestLoginRequest request;
        Memory::copy( request._arrDeviceSecret, arrDeviceSecret, LoginConstant::kDeviceSecretSize );
        request._clientInfo = _clientInfo;
        return call<AccountMethod::GuestLogin>( request, OnlineReplyDelegate<AccountMethod::GuestLogin>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::platformLogin( string_view provider, const vector<uint8>& ticketBytes )
    {
        return call<AccountMethod::PlatformLogin>( AccountPlatformRequest{ string( provider ), ticketBytes, _clientInfo },
                                                   OnlineReplyDelegate<AccountMethod::PlatformLogin>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::resume()
    {
        if ( _token.isEmpty() )
        {
            AccountClientReply& reply = _listReply.emplace_back();
            reply._requestID          = allocateRequestID();
            reply._operation          = AccountClientOperation::Resume;
            reply._result             = LoginResult::InvalidToken;
            return reply._requestID;
        }
        return sendResume( false );
    }

    uint64 AccountClient::logout()
    {
        return call<AccountMethod::Logout>( OnlineReplyDelegate<AccountMethod::Logout>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::linkCredential( string_view loginName, string_view password )
    {
        return call<AccountMethod::LinkCredential>( AccountCredentialRequest{ string( loginName ), string( password ) },
                                                    OnlineReplyDelegate<AccountMethod::LinkCredential>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::linkPlatform( string_view provider, const vector<uint8>& ticketBytes )
    {
        return call<AccountMethod::LinkPlatform>( AccountPlatformRequest{ string( provider ), ticketBytes, AccountClientInfo{} },
                                                  OnlineReplyDelegate<AccountMethod::LinkPlatform>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::issueGameTicket( string_view serverID )
    {
        return call<AccountMethod::IssueGameTicket>( AccountGameTicketRequest{ string( serverID ) }, OnlineReplyDelegate<AccountMethod::IssueGameTicket>::create<&AccountClient::onTicketReply>( this ) );
    }

    uint64 AccountClient::unlinkPlatform( string_view provider )
    {
        return call<AccountMethod::UnlinkPlatform>( AccountProviderRequest{ string( provider ) }, OnlineReplyDelegate<AccountMethod::UnlinkPlatform>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::listLinks()
    {
        return call<AccountMethod::ListLinks>( OnlineReplyDelegate<AccountMethod::ListLinks>::create<&AccountClient::onLinkReply>( this ) );
    }

    uint64 AccountClient::requestDeletion()
    {
        return call<AccountMethod::RequestDeletion>( OnlineReplyDelegate<AccountMethod::RequestDeletion>::create<&AccountClient::onGrantReply>( this ) );
    }

    uint64 AccountClient::cancelDeletion()
    {
        return call<AccountMethod::CancelDeletion>( OnlineReplyDelegate<AccountMethod::CancelDeletion>::create<&AccountClient::onGrantReply>( this ) );
    }

    int32 AccountClient::pollReplies( vector<AccountClientReply>& outListReply )
    {
        const int32 count = static_cast<int32>( _listReply.size() );
        for ( AccountClientReply& reply : _listReply )
        {
            outListReply.push_back( std::move( reply ) );
        }
        _listReply.clear();
        return count;
    }

    int32 AccountClient::pollEvents( vector<AccountClientEvent>& outListEvent )
    {
        const int32 count = static_cast<int32>( _listEvent.size() );
        for ( AccountClientEvent& event : _listEvent )
        {
            outListEvent.push_back( std::move( event ) );
        }
        _listEvent.clear();
        return count;
    }

    void AccountClient::makeConnectCredentials( const NetGameTicket& ticket, NetConnectCredentials& outCredentials )
    {
        static_assert( NetGameTicket::kTokenSize <= NetSecurityConstant::kMaxTokenSize );
        static_assert( NetGameTicket::kSecretSize == sizeof( outCredentials._secret._arrByte ) );
        Memory::copy( outCredentials._arrToken, ticket._arrToken, NetGameTicket::kTokenSize );
        outCredentials._tokenSize = NetGameTicket::kTokenSize;
        Memory::copy( outCredentials._secret._arrByte, ticket._arrSecret, NetGameTicket::kSecretSize );
    }

    void AccountClient::onPush( AccountMethod::Revoked, const AccountRevokedPush& body )
    {
        AccountClientEvent event;
        event._reason     = body._reason;
        event._reasonCode = body._reasonCode;
        _bLoggedIn        = SW_FALSE;
        _token            = LoginSessionToken{}; // 밀려난 세션은 돌아올 수 없으므로 다시 로그인해야 한다
        _listEvent.push_back( std::move( event ) );
    }

    void AccountClient::onClientReady( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE; // 새 연결이므로 서버는 아직 주체를 모른다
        if ( _token.isEmpty() == false && _bResuming == SW_FALSE )
            (void)sendResume( true );
    }

    void AccountClient::onClientDisconnected( OnlineServiceClient& client )
    {
        (void)client;
        _bLoggedIn = SW_FALSE;
    }

    uint64 AccountClient::sendResume( bool bAutomatic )
    {
        if ( _pServiceClient != nullptr )
            _pServiceClient->holdSessionRequests(); // 모든 키트의 세션 요청(이 키트 포함)은 재접속 응답 뒤에 간다. 재접속 자신은 익명이라 바로 간다
        _bResuming        = SW_TRUE;
        _bAutomaticResume = bAutomatic ? SW_TRUE : SW_FALSE;
        return call<AccountMethod::Resume>( AccountResumeRequest{ _token, _clientInfo }, OnlineReplyDelegate<AccountMethod::Resume>::create<&AccountClient::onGrantReply>( this ) );
    }

    AccountClientReply AccountClient::makeReply( uint64 requestID, uint16 method, uint16 errorCode, const vector<uint8>& errorDetail )
    {
        AccountClientReply reply;
        reply._requestID = requestID;
        reply._operation = AccountClientInternal::toOperation( method );
        reply._errorCode = errorCode;
        if ( errorCode == OnlineError::kOk )
            return reply;
        reply._result = LoginServiceResult::fail( errorCode );
        BitReader detail( errorDetail.data(), static_cast<int32>( errorDetail.size() ) );
        if ( errorCode == OnlineError::kUpdateRequired )
        {
            AccountUpdateDetail updateDetail;
            if ( errorDetail.empty() == false && AccountProtocol::MessageTypes::read( detail, updateDetail ) == FixedReadResult::Ok )
                reply._grant._storeURL = std::move( updateDetail._storeURL ); // 상세가 없거나 깨졌으면 상점 주소만 빈다(스키마 해시가 다를 때도 그렇다)
        }
        else if ( errorCode == OnlineError::kRateLimited && errorDetail.empty() == false )
        {
            reply._grant._retryAfterMs = detail.readVarUint63();
        }
        return reply;
    }

    void AccountClient::onGrantReply( const OnlineResult<AccountGrantReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result = result._reply._result;
            reply._grant  = result._reply._grant;
        }
        const bool bOk = reply._result == LoginResult::Ok;
        if ( bOk && AccountClientInternal::isGrantOperation( reply._operation ) )
        {
            _bLoggedIn = SW_TRUE;
            _token     = reply._grant._token;
            _identity  = reply._grant._identity;
        }
        if ( bOk && ( reply._operation == AccountClientOperation::LinkCredential || reply._operation == AccountClientOperation::LinkPlatform ) )
            _identity = reply._grant._identity;
        if ( bOk && ( reply._operation == AccountClientOperation::Logout || reply._operation == AccountClientOperation::RequestDeletion ) )
        {
            _bLoggedIn = SW_FALSE;
            _token     = LoginSessionToken{};
        }
        if ( reply._operation != AccountClientOperation::Resume )
        {
            _listReply.push_back( std::move( reply ) );
            return;
        }
        reply._bAutomatic = _bAutomaticResume;
        _listReply.push_back( reply );
        finishResume( reply );
    }

    void AccountClient::onTicketReply( const OnlineResult<AccountTicketReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result = result._reply._result;
            reply._ticket = result._reply._ticket;
        }
        _listReply.push_back( std::move( reply ) );
    }

    void AccountClient::onLinkReply( const OnlineResult<AccountLinkReply>& result )
    {
        AccountClientReply reply = makeReply( result._requestID, result._method, result._errorCode, result._errorDetail );
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._result      = result._reply._result;
            reply._linkSummary = result._reply._summary;
        }
        _listReply.push_back( std::move( reply ) );
    }

    void AccountClient::finishResume( const AccountClientReply& reply )
    {
        const bool bTokenDead = reply._result == LoginResult::InvalidToken || reply._result == LoginResult::Expired || reply._result == LoginResult::Revoked;
        if ( bTokenDead )
            _token = LoginSessionToken{};
        _bResuming        = SW_FALSE;
        _bAutomaticResume = SW_FALSE;
        if ( _pServiceClient == nullptr || _pServiceClient->isHoldingSessionRequests() == false )
            return;
        // 모은 세션 요청은 재접속의 결과로 끝난다 — 저장소 장애(공통 범주)를 "로그인 안 됨" 으로 바꾸면 산 세션을 죽었다고 알린다
        uint16 failureCode = OnlineError::kOk;
        if ( reply._result.isFailed() )
            failureCode = reply._result._errorCode;
        else if ( reply._result != LoginResult::Ok )
            failureCode = OnlineError::kUnauthenticated;
        _pServiceClient->releaseSessionRequests( failureCode );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct AccountClientRegistration
        {
            /** @brief 계정 클라이언트는 로그인에 빌드 글 · 플랫폼을 싣는다. */
            static unique_ptr<IOnlineClientService> create( OnlineServiceClient& client, const OnlineSessionInfo& info )
            {
                AccountClientInfo clientInfo;
                clientInfo._build                 = info._gameBuild;
                clientInfo._platform              = info._platform;
                unique_ptr<AccountClient> pClient = make_unique<AccountClient>();
                pClient->initialize( &client, clientInfo );
                return pClient;
            }
        };
    } // namespace

    SW_REGISTER_ONLINE_CLIENT_WITH( AccountClient, &AccountClientRegistration::create );
} // namespace sw
