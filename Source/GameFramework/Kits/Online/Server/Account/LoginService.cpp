#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/LoginService.h"

#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Online/Server/Account/LoginStoreLogic.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoginServiceInternal
        {
            /** @brief 돌아가며 다시 읽는 차례가 해시맵 순서에 기대지 않게(결정적) 계정 id 로 정렬한다. */
            struct SessionRefLess
            {
                bool operator()( const LoginSessionRef& left, const LoginSessionRef& right ) const { return left._accountId < right._accountId; }
            };

            static string makeNameKey( string_view displayName ) { return StringUtil::toLower( string( displayName ).c_str() ); }

            /** @brief 로그인 요청 하나를 저장소 스레드에서 돌리고, 결과를 서비스에 돌려줍니다. 입력은 모두 복사해 든다. */
            class LoginWork final : public IServiceStoreWork
            {
            public:
                LoginCredential   _credential;
                LoginSessionToken _token;
                hashed_string     _serverId;
                uint64            _accountId;

                LoginWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginSettings& settings, LoginOperation operation, uint64 requestTag, int64 nowMs )
                    : _credential{}
                    , _token{}
                    , _serverId{}
                    , _accountId{ 0 }
                    , _outcome{}
                    , _completion{}
                    , _settings{ settings }
                    , _pService{ pService }
                    , _pCrypto{ pCrypto }
                    , _nowMs{ nowMs }
                {
                    _completion._operation  = operation;
                    _completion._requestTag = requestTag;
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    LoginStoreLogic logic{ connection, *_pCrypto, _settings, _pService->getTicketAuthority(), _outcome };
                    switch ( _completion._operation )
                    {
                        case LoginOperation::Register:
                        {
                            uint64 accountId                   = 0;
                            _completion._result                = logic.registerAccount( _credential, &accountId );
                            _completion._identity._accountId   = accountId;
                            _completion._identity._displayName = _credential._loginName;
                            break;
                        }
                        case LoginOperation::Login:
                        {
                            _completion._result = logic.login( _credential, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::Resume:
                        {
                            _completion._result = logic.resumeSession( _token, _nowMs, _completion._grant );
                            break;
                        }
                        case LoginOperation::Validate:
                        {
                            _completion._result = logic.validateSession( _token, _nowMs, _completion._identity, &_completion._grant._revokeReason );
                            break;
                        }
                        case LoginOperation::Logout:
                        {
                            _completion._result = logic.logout( _token, _nowMs );
                            break;
                        }
                        case LoginOperation::IssueGameTicket:
                        {
                            _completion._result = logic.issueGameTicket( _token, _serverId, _nowMs, _completion._ticket );
                            break;
                        }
                        case LoginOperation::Revoke:
                        {
                            _completion._result = logic.revokeAccountSessions( _accountId, _nowMs );
                            break;
                        }
                    }
                    _credential._password.clear(); // 워커를 떠나면 비밀번호를 들고 있지 않는다(지우기 · wipe 는 하지 않는다 — 서버 관례)
                }

                void complete() override { _pService->applyCompletion( std::move( _completion ), _outcome ); }

            private:
                LoginStoreOutcome _outcome;
                LoginCompletion   _completion;
                LoginSettings     _settings; ///< 복사 — 서비스 설정이 바뀌어도 이 일은 맡긴 때의 값으로 돈다
                LoginService*     _pService;
                ILoginCrypto*     _pCrypto;
                int64             _nowMs;
            };

            /** @brief 끊김 · 세션 다시 읽기 — 완료가 없고 부작용만 있는 일입니다. */
            class LoginMaintenanceWork final : public IServiceStoreWork
            {
            public:
                vector<LoginSessionRef> _listOnline;
                uint64                  _disconnectedSessionId;

                LoginMaintenanceWork( LoginService* pService, ILoginCrypto* pCrypto, const LoginSettings& settings, int64 nowMs )
                    : _listOnline{}
                    , _disconnectedSessionId{ 0 }
                    , _outcome{}
                    , _settings{ settings }
                    , _pService{ pService }
                    , _pCrypto{ pCrypto }
                    , _nowMs{ nowMs }
                {
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    LoginStoreLogic logic{ connection, *_pCrypto, _settings, _pService->getTicketAuthority(), _outcome };
                    if ( _disconnectedSessionId != 0 )
                        logic.markDisconnected( _disconnectedSessionId, _nowMs );
                    if ( _listOnline.empty() == false )
                        logic.refreshSessions( _listOnline, _nowMs );
                }

                void complete() override
                {
                    LoginCompletion none;
                    none._requestTag = 0; // 꼬리표 0 은 완료 목록에 쌓지 않는다(applyCompletion)
                    _pService->applyCompletion( std::move( none ), _outcome );
                }

            private:
                LoginStoreOutcome _outcome;
                LoginSettings     _settings;
                LoginService*     _pService;
                ILoginCrypto*     _pCrypto;
                int64             _nowMs;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoginService::LoginService()
        : _eventBuffer{}
        , _completionBuffer{}
        , _ticketAuthority{}
        , _settings{}
        , _mapAccountToSession{}
        , _mapAccountToIdentity{}
        , _mapNameKeyToAccount{}
        , _mapClientToAttemptBucket{}
        , _pStore{ nullptr }
        , _pCrypto{ nullptr }
        , _refreshCursor{ 0 }
        , _pendingCount{ 0 }
    {
    }

    void LoginService::initialize( IServiceStore* pStore, ILoginCrypto* pCrypto, const LoginSettings& settings,
                                   const uint8 ( &arrTicketMasterKey )[LoginTicketAuthority::kMasterKeySize] )
    {
        _pStore   = pStore;
        _pCrypto  = pCrypto;
        _settings = settings;
        _ticketAuthority.initialize( pCrypto, arrTicketMasterKey );
        _mapClientToAttemptBucket.initialize( settings._attemptBurst, settings._attemptRefillMs, kMaxTrackedClientCount );
    }

    void LoginService::shutdown()
    {
        _ticketAuthority.shutdown();
        _mapAccountToSession.clear();
        _mapAccountToIdentity.clear();
        _mapNameKeyToAccount.clear();
        _eventBuffer.clear();
        _pStore  = nullptr;
        _pCrypto = nullptr;
    }

    void LoginService::registerAccount( const LoginCredential& credential, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Register, requestTag, nowMs );
        work->_credential                                = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::login( const LoginCredential& credential, uint64 clientKey, int64 nowMs, uint64 requestTag )
    {
        int64 retryAfterMs = 0;
        if ( consumeAttempt( clientKey, nowMs, retryAfterMs ) == false )
        {
            pushRateLimited( LoginOperation::Login, requestTag, retryAfterMs );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Login, requestTag, nowMs );
        work->_credential                                = credential;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::resumeSession( const LoginSessionToken& token, uint64 clientKey, int64 nowMs, uint64 requestTag )
    {
        int64 retryAfterMs = 0;
        if ( consumeAttempt( clientKey, nowMs, retryAfterMs ) == false )
        {
            pushRateLimited( LoginOperation::Resume, requestTag, retryAfterMs );
            return;
        }
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Resume, requestTag, nowMs );
        work->_token                                     = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::validateSession( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Validate, requestTag, nowMs );
        work->_token = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::logout( const LoginSessionToken& token, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Logout, requestTag, nowMs );
        work->_token                                     = token;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work =
            make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::IssueGameTicket, requestTag, nowMs );
        work->_token    = token;
        work->_serverId = serverId;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::revokeAccountSessions( uint64 accountId, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<LoginServiceInternal::LoginWork> work = make_unique<LoginServiceInternal::LoginWork>( this, _pCrypto, _settings, LoginOperation::Revoke, requestTag, nowMs );
        work->_accountId                                 = accountId;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::markDisconnected( uint64 sessionId, int64 nowMs )
    {
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        work->_disconnectedSessionId                                = sessionId;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void LoginService::refreshOnlineSessions( int64 nowMs, int32 maxCount )
    {
        if ( _mapAccountToSession.empty() || maxCount <= 0 )
            return;
        vector<LoginSessionRef> listOnline;
        listOnline.reserve( _mapAccountToSession.size() );
        for ( const auto& [accountId, sessionId] : _mapAccountToSession )
            listOnline.push_back( LoginSessionRef{ accountId, sessionId, LoginRevokeReason::None } );
        std::sort( listOnline.begin(), listOnline.end(), LoginServiceInternal::SessionRefLess{} );
        unique_ptr<LoginServiceInternal::LoginMaintenanceWork> work       = make_unique<LoginServiceInternal::LoginMaintenanceWork>( this, _pCrypto, _settings, nowMs );
        const int32                                            checkCount = std::min( maxCount, static_cast<int32>( listOnline.size() ) );
        for ( int32 checkIndex = 0; checkIndex < checkCount; ++checkIndex )
            work->_listOnline.push_back( listOnline[( _refreshCursor + static_cast<size_t>( checkIndex ) ) % listOnline.size()] );
        _refreshCursor = ( _refreshCursor + static_cast<size_t>( checkCount ) ) % listOnline.size();
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    bool LoginService::findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const
    {
        const auto identityIt = _mapAccountToIdentity.find( accountId );
        if ( identityIt == _mapAccountToIdentity.end() )
            return false;
        outIdentity = identityIt->second;
        return true;
    }

    bool LoginService::findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const
    {
        const auto nameIt = _mapNameKeyToAccount.find( LoginServiceInternal::makeNameKey( displayName ) );
        return nameIt != _mapNameKeyToAccount.end() && findIdentity( nameIt->second, outIdentity );
    }

    void LoginService::applyCompletion( LoginCompletion&& completion, const LoginStoreOutcome& outcome )
    {
        --_pendingCount;
        for ( const LoginSessionRef& revoked : outcome._listRevoked )
        {
            const auto onlineIt = _mapAccountToSession.find( revoked._accountId );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != revoked._sessionId )
                continue;
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ revoked._accountId, revoked._sessionId, revoked._reason, LoginEvent::Kind::Revoked } );
        }
        for ( const LoginSessionRef& offline : outcome._listOffline )
        {
            const auto onlineIt = _mapAccountToSession.find( offline._accountId );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second == offline._sessionId )
                _mapAccountToSession.erase( onlineIt );
        }
        for ( const LoginSessionRef& disconnected : outcome._listDisconnected )
        {
            const auto onlineIt = _mapAccountToSession.find( disconnected._accountId );
            if ( onlineIt == _mapAccountToSession.end() || onlineIt->second != disconnected._sessionId )
                continue;
            _mapAccountToSession.erase( onlineIt );
            _eventBuffer.push( LoginEvent{ disconnected._accountId, disconnected._sessionId, LoginRevokeReason::None, LoginEvent::Kind::Disconnected } );
        }
        for ( const LoginSessionRef& online : outcome._listOnline )
        {
            const auto onlineIt = _mapAccountToSession.find( online._accountId );
            if ( onlineIt != _mapAccountToSession.end() && onlineIt->second != online._sessionId )
                _eventBuffer.push( LoginEvent{ online._accountId, onlineIt->second, LoginRevokeReason::DuplicateLogin, LoginEvent::Kind::Revoked } );
            _mapAccountToSession[online._accountId] = online._sessionId;
        }
        for ( const LoginEvent& event : outcome._listEvent )
            _eventBuffer.push( event );
        // 신원 캐시 — 붙어 있는 계정만.
        const bool bGrantOperation = completion._operation == LoginOperation::Login || completion._operation == LoginOperation::Resume;
        if ( completion._result == LoginResult::Ok && bGrantOperation && completion._grant._identity._accountId != kInvalidAccountId )
        {
            const AccountIdentity& identity                                                  = completion._grant._identity;
            _mapAccountToIdentity[identity._accountId]                                       = identity;
            _mapNameKeyToAccount[LoginServiceInternal::makeNameKey( identity._displayName )] = identity._accountId;
        }
        removeOfflineIdentities();
        if ( completion._requestTag != 0 )
            _completionBuffer.push( std::move( completion ) );
    }

    bool LoginService::consumeAttempt( uint64 clientKey, int64 nowMs, int64& outRetryAfterMs ) { return _mapClientToAttemptBucket.tryConsume( clientKey, nowMs, outRetryAfterMs ); }

    void LoginService::pushRateLimited( LoginOperation operation, uint64 requestTag, int64 retryAfterMs )
    {
        LoginCompletion completion;
        completion._operation           = operation;
        completion._requestTag          = requestTag;
        completion._result              = LoginResult::RateLimited;
        completion._grant._retryAfterMs = retryAfterMs;
        _completionBuffer.push( std::move( completion ) );
    }

    void LoginService::removeOfflineIdentities()
    {
        for ( auto identityIt = _mapAccountToIdentity.begin(); identityIt != _mapAccountToIdentity.end(); )
        {
            if ( _mapAccountToSession.find( identityIt->first ) != _mapAccountToSession.end() )
            {
                ++identityIt;
                continue;
            }
            _mapNameKeyToAccount.erase( LoginServiceInternal::makeNameKey( identityIt->second._displayName ) );
            identityIt = _mapAccountToIdentity.erase( identityIt );
        }
    }
} // namespace sw
