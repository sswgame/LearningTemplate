#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/LoginStoreLogic.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "LoginStoreLogic" );

    namespace
    {
        struct LoginStoreLogicInternal
        {
            static constexpr uint8 kRecordFormat    = 1;
            static constexpr int32 kStoreRetryLimit = 4; ///< 조건부 쓰기가 다른 쓰기와 부딪혔을 때 다시 읽는 수
            static constexpr utf8  kDigestLabel[]   = "sw-login-token-digest-v1";

            static const hashed_string& getAccountTable()
            {
                static const hashed_string s_table{ "login_account" };
                return s_table;
            }

            static const hashed_string& getAccountIdTable()
            {
                static const hashed_string s_table{ "login_account_id" };
                return s_table;
            }

            static const hashed_string& getSessionTable()
            {
                static const hashed_string s_table{ "login_session" };
                return s_table;
            }

            static const hashed_string& getAccountSessionTable()
            {
                static const hashed_string s_table{ "login_account_session" };
                return s_table;
            }

            struct AccountRecord
            {
                NetPasswordHashParams _params{};
                string                _displayName{};
                uint64                _accountId{ 0 };
                int64                 _lockedUntilMs{ 0 };
                int32                 _failedCount{ 0 };
                uint8                 _arrSalt[LoginConstant::kSaltSize]{};
                uint8                 _arrPasswordHash[LoginConstant::kPasswordHashSize]{};
            };

            struct SessionRecord
            {
                uint64            _accountId{ 0 };
                int64             _issuedAtMs{ 0 };
                int64             _expiresAtMs{ 0 };
                int64             _resumeDeadlineMs{ 0 }; ///< 0 = 붙어 있다
                uint32            _rotation{ 0 };
                uint8             _arrTokenDigest[LoginConstant::kDigestSize]{};
                LoginRevokeReason _revokeReason{ LoginRevokeReason::None }; ///< None 이 아니면 묘비
            };

            static vector<uint8> encodeAccount( const AccountRecord& record )
            {
                BitWriter writer;
                writer.writeBits( kRecordFormat, 8 );
                writer.writeVarUint( record._accountId );
                ServiceKeyUtil::writeString( writer, record._displayName );
                writer.writeBytes( record._arrSalt, LoginConstant::kSaltSize );
                writer.writeBytes( record._arrPasswordHash, LoginConstant::kPasswordHashSize );
                writer.writeVarUint( record._params._memoryKiB );
                writer.writeVarUint( record._params._iterationCount );
                writer.writeVarUint( record._params._parallelism );
                writer.writeVarUint( static_cast<uint64>( record._failedCount ) );
                writer.writeVarInt( record._lockedUntilMs );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeAccount( const vector<uint8>& bytes, AccountRecord& outRecord )
            {
                BitReader reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                if ( reader.readBits( 8 ) != kRecordFormat )
                    return false;
                AccountRecord record;
                record._accountId = reader.readVarUint();
                if ( ServiceKeyUtil::readString( reader, LoginConstant::kMaxLoginNameSize, record._displayName ) == false )
                    return false;
                if ( reader.readBytes( record._arrSalt, LoginConstant::kSaltSize ) == false ||
                     reader.readBytes( record._arrPasswordHash, LoginConstant::kPasswordHashSize ) == false )
                    return false;
                record._params._memoryKiB      = static_cast<uint32>( reader.readVarUint() );
                record._params._iterationCount = static_cast<uint32>( reader.readVarUint() );
                record._params._parallelism    = static_cast<uint32>( reader.readVarUint() );
                record._failedCount            = static_cast<int32>( reader.readVarUint() );
                record._lockedUntilMs          = reader.readVarInt();
                if ( reader.hasOverflowed() )
                    return false;
                outRecord = std::move( record );
                return true;
            }

            static vector<uint8> encodeSession( const SessionRecord& record )
            {
                BitWriter writer;
                writer.writeBits( kRecordFormat, 8 );
                writer.writeVarUint( record._accountId );
                writer.writeVarInt( record._issuedAtMs );
                writer.writeVarInt( record._expiresAtMs );
                writer.writeVarInt( record._resumeDeadlineMs );
                writer.writeVarUint( record._rotation );
                writer.writeBytes( record._arrTokenDigest, LoginConstant::kDigestSize );
                writer.writeBits( static_cast<uint32>( record._revokeReason ), 8 );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeSession( const vector<uint8>& bytes, SessionRecord& outRecord )
            {
                BitReader reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                if ( reader.readBits( 8 ) != kRecordFormat )
                    return false;
                SessionRecord record;
                record._accountId        = reader.readVarUint();
                record._issuedAtMs       = reader.readVarInt();
                record._expiresAtMs      = reader.readVarInt();
                record._resumeDeadlineMs = reader.readVarInt();
                record._rotation         = static_cast<uint32>( reader.readVarUint() );
                if ( reader.readBytes( record._arrTokenDigest, LoginConstant::kDigestSize ) == false )
                    return false;
                const uint32 revokeReason = reader.readBits( 8 );
                if ( reader.hasOverflowed() || revokeReason > static_cast<uint32>( LoginRevokeReason::Administrative ) )
                    return false;
                record._revokeReason = static_cast<LoginRevokeReason>( revokeReason );
                outRecord            = record;
                return true;
            }

            static vector<uint8> encodeSessionLink( uint64 sessionId )
            {
                BitWriter writer;
                writer.writeVarUint( sessionId );
                return writer.releaseBytes();
            }

            static uint64 decodeSessionLink( const vector<uint8>& bytes )
            {
                BitReader    reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                const uint64 sessionId = reader.readVarUint();
                return reader.hasOverflowed() ? 0 : sessionId;
            }

            /** @brief ASCII 영숫자 · _ 3..16 자를 소문자 키로 바꿉니다. 아니면 false 입니다. */
            [[nodiscard]] static bool normalizeLoginName( string_view loginName, string& outKey )
            {
                const int32 size = static_cast<int32>( loginName.size() );
                if ( size < LoginConstant::kMinLoginNameSize || LoginConstant::kMaxLoginNameSize < size )
                    return false;
                outKey.clear();
                outKey.reserve( loginName.size() );
                for ( const utf8 ch : loginName )
                {
                    const bool bLetter = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                    const bool bDigit  = '0' <= ch && ch <= '9';
                    if ( bLetter == false && bDigit == false && ch != '_' )
                        return false;
                    outKey.push_back( StringUtil::toLowerChar( ch ) );
                }
                return true;
            }

            /** @brief 상수 시간 비교 — 첫 다른 바이트에서 멈추지 않는다. */
            static bool isEqualConstantTime( const uint8* pFirst, const uint8* pSecond, int32 size )
            {
                uint8 difference = 0;
                for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
                    difference = static_cast<uint8>( difference | ( pFirst[byteIndex] ^ pSecond[byteIndex] ) );
                return difference == 0;
            }

            [[nodiscard]] static bool computeTokenDigest( ILoginCrypto& crypto, const uint8 ( &arrSecret )[LoginConstant::kTokenSecretSize],
                                                          uint8 ( &outDigest )[LoginConstant::kDigestSize] )
            {
                return crypto.computeKeyedHash( arrSecret, LoginConstant::kTokenSecretSize, reinterpret_cast<const uint8*>( kDigestLabel ),
                                                static_cast<int32>( sizeof( kDigestLabel ) - 1 ), outDigest, LoginConstant::kDigestSize );
            }

            /** @brief 0 이 아닌 64 비트 난수입니다(0 은 "없음"). */
            [[nodiscard]] static bool makeNonZeroId( ILoginCrypto& crypto, uint64& outId )
            {
                for ( int32 attempt = 0; attempt < 4; ++attempt )
                {
                    uint8 arrByte[8];
                    if ( crypto.fillRandom( arrByte, 8 ) == false )
                        return false;
                    uint64 value = 0;
                    for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                        value |= static_cast<uint64>( arrByte[byteIndex] ) << ( byteIndex * 8 );
                    if ( value != 0 )
                    {
                        outId = value;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 세션이 지금 쓸 수 있는가입니다. 아니면 그 결과입니다. */
            static LoginResult evaluateSession( const SessionRecord& session, int64 nowMs )
            {
                if ( session._revokeReason != LoginRevokeReason::None )
                    return LoginResult::Revoked;
                if ( nowMs >= session._expiresAtMs )
                    return LoginResult::Expired;
                if ( session._resumeDeadlineMs != 0 && nowMs >= session._resumeDeadlineMs )
                    return LoginResult::Expired;
                return LoginResult::Ok;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoginStoreLogic::LoginStoreLogic( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginSettings& settings,
                                      const LoginTicketAuthority& ticketAuthority, LoginStoreOutcome& outOutcome )
        : _connection{ connection }
        , _crypto{ crypto }
        , _settings{ settings }
        , _ticketAuthority{ ticketAuthority }
        , _outcome{ outOutcome }
    {
    }

    LoginResult LoginStoreLogic::registerAccount( const LoginCredential& credential, uint64* pOutAccountId )
    {
        string nameKey;
        if ( LoginStoreLogicInternal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;
        const int32 passwordSize = static_cast<int32>( credential._password.size() );
        if ( passwordSize < LoginConstant::kMinPasswordSize || LoginConstant::kMaxPasswordSize < passwordSize )
            return LoginResult::InvalidPassword;

        LoginStoreLogicInternal::AccountRecord account;
        account._displayName = credential._loginName;
        account._params      = _settings._passwordHashParams;
        if ( _crypto.fillRandom( account._arrSalt, LoginConstant::kSaltSize ) == false )
            return LoginResult::StoreUnavailable;
        if ( _crypto.computePasswordHash( credential._password, account._arrSalt, LoginConstant::kSaltSize, account._params, account._arrPasswordHash,
                                          LoginConstant::kPasswordHashSize ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < LoginStoreLogicInternal::kStoreRetryLimit; ++attempt )
        {
            if ( LoginStoreLogicInternal::makeNonZeroId( _crypto, account._accountId ) == false )
                return LoginResult::StoreUnavailable;
            ServiceTransaction transaction;
            transaction.put( LoginStoreLogicInternal::getAccountTable(), nameKey, LoginStoreLogicInternal::encodeAccount( account ), ServiceRecord::kAbsentVersion );
            transaction.put( LoginStoreLogicInternal::getAccountIdTable(), ServiceKeyUtil::makeHex64( account._accountId ), vector<uint8>( nameKey.begin(), nameKey.end() ),
                             ServiceRecord::kAbsentVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult result = _connection.commit( transaction, &info );
            if ( result == ServiceStoreResult::Conflict && info._conflictIndex == 0 )
                return LoginResult::NameTaken;
            if ( result == ServiceStoreResult::Conflict )
                continue; // 계정 id 가 겹쳤다(2^-64) — 새 id 로. SQL 의 직렬화 실패(번호 −1)도 다시 본다
            if ( result != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            if ( pOutAccountId != nullptr )
                *pOutAccountId = account._accountId;
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::login( const LoginCredential& credential, int64 nowMs, LoginGrant& outGrant )
    {
        outGrant = LoginGrant{};
        string nameKey;
        if ( LoginStoreLogicInternal::normalizeLoginName( credential._loginName, nameKey ) == false )
        {
            burnPasswordHash( credential._password );
            return LoginResult::WrongCredentials;
        }

        for ( int32 attempt = 0; attempt < LoginStoreLogicInternal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord      accountRaw;
            ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getAccountTable(), nameKey, accountRaw );
            if ( readResult == ServiceStoreResult::NotFound )
            {
                burnPasswordHash( credential._password );
                return LoginResult::WrongCredentials;
            }
            if ( readResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            LoginStoreLogicInternal::AccountRecord account;
            if ( LoginStoreLogicInternal::decodeAccount( accountRaw._bytes, account ) == false )
            {
                SW_LOG_ERROR( "account record '%#' is corrupt", nameKey.c_str() );
                return LoginResult::StoreUnavailable;
            }
            if ( account._lockedUntilMs > nowMs )
            {
                outGrant._retryAfterMs = account._lockedUntilMs - nowMs;
                return LoginResult::AccountLocked;
            }
            uint8 arrHash[LoginConstant::kPasswordHashSize];
            if ( _crypto.computePasswordHash( credential._password, account._arrSalt, LoginConstant::kSaltSize, account._params, arrHash, LoginConstant::kPasswordHashSize ) ==
                 false )
                return LoginResult::StoreUnavailable;
            if ( LoginStoreLogicInternal::isEqualConstantTime( arrHash, account._arrPasswordHash, LoginConstant::kPasswordHashSize ) == false )
                return recordFailure( nameKey, accountRaw._bytes, accountRaw._version, nowMs, outGrant._retryAfterMs );

            ServiceTransaction transaction;
            // 매개변수가 바뀌었으면 새 소금 · 새 값으로 다시 해시한다(비밀번호를 아는 지금만 할 수 있다).
            const bool bRehash         = account._params != _settings._passwordHashParams;
            const bool bAccountChanged = bRehash || account._failedCount != 0 || account._lockedUntilMs != 0;
            if ( bRehash )
            {
                account._params        = _settings._passwordHashParams;
                const bool bSaltFilled = _crypto.fillRandom( account._arrSalt, LoginConstant::kSaltSize );
                if ( bSaltFilled == false || _crypto.computePasswordHash( credential._password, account._arrSalt, LoginConstant::kSaltSize, account._params,
                                                                          account._arrPasswordHash, LoginConstant::kPasswordHashSize ) == false )
                    return LoginResult::StoreUnavailable;
            }
            if ( bAccountChanged )
            {
                account._failedCount   = 0;
                account._lockedUntilMs = 0;
                transaction.put( LoginStoreLogicInternal::getAccountTable(), nameKey, LoginStoreLogicInternal::encodeAccount( account ), accountRaw._version );
            }

            // 옛 세션 — 정책대로 거절하거나 묘비로 바꾼다.
            const string  accountKey = ServiceKeyUtil::makeHex64( account._accountId );
            ServiceRecord linkRaw;
            readResult = _connection.readRecord( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, linkRaw );
            if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                return LoginResult::StoreUnavailable;
            const uint64 oldSessionId      = readResult == ServiceStoreResult::Ok ? LoginStoreLogicInternal::decodeSessionLink( linkRaw._bytes ) : 0;
            uint64       replacedSessionId = 0;
            if ( oldSessionId != 0 )
            {
                ServiceRecord oldSessionRaw;
                const string  oldSessionKey = ServiceKeyUtil::makeHex64( oldSessionId );
                readResult                  = _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), oldSessionKey, oldSessionRaw );
                if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                    return LoginResult::StoreUnavailable;
                LoginStoreLogicInternal::SessionRecord oldSession;
                const bool                             bOldReadable = readResult == ServiceStoreResult::Ok && LoginStoreLogicInternal::decodeSession( oldSessionRaw._bytes, oldSession );
                const bool                             bOldLive     = bOldReadable && LoginStoreLogicInternal::evaluateSession( oldSession, nowMs ) == LoginResult::Ok;
                const bool                             bReject      = bOldLive && _settings._duplicatePolicy == LoginDuplicatePolicy::RejectNew && oldSession._resumeDeadlineMs == 0;
                if ( bReject )
                    return LoginResult::AlreadyLoggedIn;
                if ( bOldLive )
                {
                    oldSession._revokeReason = LoginRevokeReason::DuplicateLogin;
                    oldSession._expiresAtMs  = std::min( oldSession._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
                    transaction.put( LoginStoreLogicInternal::getSessionTable(), oldSessionKey, LoginStoreLogicInternal::encodeSession( oldSession ), oldSessionRaw._version );
                    replacedSessionId = oldSessionId;
                }
            }

            // 새 세션.
            LoginSessionToken token;
            const bool        bIdMade = LoginStoreLogicInternal::makeNonZeroId( _crypto, token._sessionId );
            if ( bIdMade == false || _crypto.fillRandom( token._arrSecret, LoginConstant::kTokenSecretSize ) == false )
                return LoginResult::StoreUnavailable;
            LoginStoreLogicInternal::SessionRecord session;
            session._accountId   = account._accountId;
            session._issuedAtMs  = nowMs;
            session._expiresAtMs = nowMs + _settings._sessionLifetimeMs;
            if ( LoginStoreLogicInternal::computeTokenDigest( _crypto, token._arrSecret, session._arrTokenDigest ) == false )
                return LoginResult::StoreUnavailable;
            transaction.put( LoginStoreLogicInternal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionId ), LoginStoreLogicInternal::encodeSession( session ),
                             ServiceRecord::kAbsentVersion );
            transaction.put( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, LoginStoreLogicInternal::encodeSessionLink( token._sessionId ), linkRaw._version );

            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue; // 같은 계정이 그새 다른 곳에서 로그인했거나 실패 수가 바뀌었다 — 다시 읽는다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable; // 적용됐는지 모른다 — 클라이언트가 다시 로그인하면 그 세션을 밀어낸다

            if ( replacedSessionId != 0 )
                _outcome._listRevoked.push_back( LoginSessionRef{ account._accountId, replacedSessionId, LoginRevokeReason::DuplicateLogin } );
            _outcome._listOnline.push_back( LoginSessionRef{ account._accountId, token._sessionId, LoginRevokeReason::None } );
            _outcome._listEvent.push_back( LoginEvent{ account._accountId, token._sessionId, LoginRevokeReason::None, LoginEvent::Kind::LoggedIn } );
            outGrant._identity._accountId   = account._accountId;
            outGrant._identity._displayName = account._displayName;
            outGrant._token                 = token;
            outGrant._expiresAtMs           = session._expiresAtMs;
            outGrant._replacedSessionId     = replacedSessionId;
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant )
    {
        outGrant                = LoginGrant{};
        const string sessionKey = ServiceKeyUtil::makeHex64( token._sessionId );
        uint8        arrDigest[LoginConstant::kDigestSize];
        if ( LoginStoreLogicInternal::computeTokenDigest( _crypto, token._arrSecret, arrDigest ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < LoginStoreLogicInternal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), sessionKey, sessionRaw );
            if ( readResult == ServiceStoreResult::NotFound )
                return LoginResult::InvalidToken;
            if ( readResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            LoginStoreLogicInternal::SessionRecord session;
            if ( LoginStoreLogicInternal::decodeSession( sessionRaw._bytes, session ) == false )
                return LoginResult::StoreUnavailable;
            if ( LoginStoreLogicInternal::isEqualConstantTime( arrDigest, session._arrTokenDigest, LoginConstant::kDigestSize ) == false )
                return LoginResult::InvalidToken;
            const LoginResult state = LoginStoreLogicInternal::evaluateSession( session, nowMs );
            if ( state != LoginResult::Ok )
            {
                outGrant._revokeReason = session._revokeReason;
                return state;
            }

            // 재접속마다 비밀을 돌려 바꾼다 — 옛 토큰은 이 커밋으로 죽는다.
            LoginSessionToken rotated;
            rotated._sessionId     = token._sessionId;
            const bool bSecretMade = _crypto.fillRandom( rotated._arrSecret, LoginConstant::kTokenSecretSize );
            if ( bSecretMade == false || LoginStoreLogicInternal::computeTokenDigest( _crypto, rotated._arrSecret, session._arrTokenDigest ) == false )
                return LoginResult::StoreUnavailable;
            session._resumeDeadlineMs = 0;
            ++session._rotation;
            ServiceTransaction transaction;
            transaction.put( LoginStoreLogicInternal::getSessionTable(), sessionKey, LoginStoreLogicInternal::encodeSession( session ), sessionRaw._version );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue; // 같은 토큰으로 그새 돌아온 쪽이 있다 — 다시 읽으면 비밀이 달라 InvalidToken 이 된다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

            _outcome._listOnline.push_back( LoginSessionRef{ session._accountId, token._sessionId, LoginRevokeReason::None } );
            _outcome._listEvent.push_back( LoginEvent{ session._accountId, token._sessionId, LoginRevokeReason::None, LoginEvent::Kind::Resumed } );
            (void)readIdentity( session._accountId, outGrant._identity );
            outGrant._token       = rotated;
            outGrant._expiresAtMs = session._expiresAtMs;
            return LoginResult::Ok;
        }
        return LoginResult::InvalidToken;
    }

    LoginResult LoginStoreLogic::validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason )
    {
        ServiceRecord            sessionRaw;
        const ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionId ), sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::InvalidToken;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        LoginStoreLogicInternal::SessionRecord session;
        if ( LoginStoreLogicInternal::decodeSession( sessionRaw._bytes, session ) == false )
            return LoginResult::StoreUnavailable;
        uint8 arrDigest[LoginConstant::kDigestSize];
        if ( LoginStoreLogicInternal::computeTokenDigest( _crypto, token._arrSecret, arrDigest ) == false )
            return LoginResult::StoreUnavailable;
        if ( LoginStoreLogicInternal::isEqualConstantTime( arrDigest, session._arrTokenDigest, LoginConstant::kDigestSize ) == false )
            return LoginResult::InvalidToken;
        const LoginResult state = LoginStoreLogicInternal::evaluateSession( session, nowMs );
        if ( pOutRevokeReason != nullptr )
            *pOutRevokeReason = session._revokeReason;
        if ( state != LoginResult::Ok )
            return state;
        return readIdentity( session._accountId, outIdentity ) ? LoginResult::Ok : LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::logout( const LoginSessionToken& token, int64 nowMs )
    {
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        const string       accountKey = ServiceKeyUtil::makeHex64( identity._accountId );
        ServiceRecord      linkRaw;
        ServiceTransaction transaction;
        transaction.erase( LoginStoreLogicInternal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionId ) );
        const ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult == ServiceStoreResult::Ok && LoginStoreLogicInternal::decodeSessionLink( linkRaw._bytes ) == token._sessionId )
            transaction.erase( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, linkRaw._version );
        const ServiceStoreResult commitResult = _connection.commit( transaction );
        if ( commitResult != ServiceStoreResult::Ok && commitResult != ServiceStoreResult::Conflict )
            return LoginResult::StoreUnavailable;
        _outcome._listOffline.push_back( LoginSessionRef{ identity._accountId, token._sessionId, LoginRevokeReason::LoggedOut } );
        _outcome._listEvent.push_back( LoginEvent{ identity._accountId, token._sessionId, LoginRevokeReason::LoggedOut, LoginEvent::Kind::LoggedOut } );
        return LoginResult::Ok;
    }

    void LoginStoreLogic::markDisconnected( uint64 sessionId, int64 nowMs )
    {
        const string sessionKey = ServiceKeyUtil::makeHex64( sessionId );
        for ( int32 attempt = 0; attempt < LoginStoreLogicInternal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord sessionRaw;
            if ( _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), sessionKey, sessionRaw ) != ServiceStoreResult::Ok )
                return;
            LoginStoreLogicInternal::SessionRecord session;
            if ( LoginStoreLogicInternal::decodeSession( sessionRaw._bytes, session ) == false )
                return;
            if ( attempt == 0 )
                _outcome._listDisconnected.push_back( LoginSessionRef{ session._accountId, sessionId, LoginRevokeReason::None } );
            if ( session._revokeReason != LoginRevokeReason::None || session._resumeDeadlineMs != 0 )
                return;
            session._resumeDeadlineMs = nowMs + _settings._reconnectGraceMs;
            ServiceTransaction transaction;
            transaction.put( LoginStoreLogicInternal::getSessionTable(), sessionKey, LoginStoreLogicInternal::encodeSession( session ), sessionRaw._version );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                SW_LOG_WARNING( "session %# could not start its reconnect grace (store %#)", sessionKey.c_str(), static_cast<int32>( commitResult ) );
            return;
        }
    }

    LoginResult LoginStoreLogic::revokeAccountSessions( uint64 accountId, int64 nowMs )
    {
        const string       accountKey = ServiceKeyUtil::makeHex64( accountId );
        ServiceRecord      linkRaw;
        ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::Ok;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        const uint64  sessionId  = LoginStoreLogicInternal::decodeSessionLink( linkRaw._bytes );
        const string  sessionKey = ServiceKeyUtil::makeHex64( sessionId );
        ServiceRecord sessionRaw;
        readResult = _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), sessionKey, sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::Ok;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        LoginStoreLogicInternal::SessionRecord session;
        if ( LoginStoreLogicInternal::decodeSession( sessionRaw._bytes, session ) == false )
            return LoginResult::StoreUnavailable;
        session._revokeReason = LoginRevokeReason::Administrative;
        session._expiresAtMs  = std::min( session._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
        ServiceTransaction transaction;
        transaction.put( LoginStoreLogicInternal::getSessionTable(), sessionKey, LoginStoreLogicInternal::encodeSession( session ), sessionRaw._version );
        transaction.erase( LoginStoreLogicInternal::getAccountSessionTable(), accountKey, linkRaw._version );
        if ( _connection.commit( transaction ) != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        _outcome._listRevoked.push_back( LoginSessionRef{ accountId, sessionId, LoginRevokeReason::Administrative } );
        return LoginResult::Ok;
    }

    LoginResult LoginStoreLogic::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverId, int64 nowMs, NetGameTicket& outTicket )
    {
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        const bool bIssued = _ticketAuthority.issueTicket( identity._accountId, token._sessionId, serverId, nowMs + _settings._ticketLifetimeMs, outTicket );
        return bIssued ? LoginResult::Ok : LoginResult::StoreUnavailable;
    }

    void LoginStoreLogic::refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs )
    {
        for ( const LoginSessionRef& online : listOnline )
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( LoginStoreLogicInternal::getSessionTable(), ServiceKeyUtil::makeHex64( online._sessionId ), sessionRaw );
            if ( readResult == ServiceStoreResult::Unavailable )
                return; // 저장소가 아프면 아무도 끊지 않는다
            LoginStoreLogicInternal::SessionRecord session;
            const bool                             bReadable = readResult == ServiceStoreResult::Ok && LoginStoreLogicInternal::decodeSession( sessionRaw._bytes, session );
            if ( bReadable && LoginStoreLogicInternal::evaluateSession( session, nowMs ) == LoginResult::Ok )
                continue;
            const LoginRevokeReason reason = bReadable ? session._revokeReason : LoginRevokeReason::Administrative;
            _outcome._listRevoked.push_back( LoginSessionRef{ online._accountId, online._sessionId, reason } );
        }
    }

    bool LoginStoreLogic::readIdentity( uint64 accountId, AccountIdentity& outIdentity )
    {
        ServiceRecord nameRaw;
        if ( _connection.readRecord( LoginStoreLogicInternal::getAccountIdTable(), ServiceKeyUtil::makeHex64( accountId ), nameRaw ) != ServiceStoreResult::Ok )
            return false;
        const string nameKey( reinterpret_cast<const utf8*>( nameRaw._bytes.data() ), nameRaw._bytes.size() );
        return readIdentityByDisplayName( nameKey, outIdentity ) && outIdentity._accountId == accountId;
    }

    bool LoginStoreLogic::readIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity )
    {
        string nameKey;
        if ( LoginStoreLogicInternal::normalizeLoginName( displayName, nameKey ) == false )
            return false;
        ServiceRecord accountRaw;
        if ( _connection.readRecord( LoginStoreLogicInternal::getAccountTable(), nameKey, accountRaw ) != ServiceStoreResult::Ok )
            return false;
        LoginStoreLogicInternal::AccountRecord account;
        if ( LoginStoreLogicInternal::decodeAccount( accountRaw._bytes, account ) == false )
            return false;
        outIdentity._accountId   = account._accountId;
        outIdentity._displayName = account._displayName;
        return true;
    }

    LoginResult LoginStoreLogic::recordFailure( string_view nameKey, const vector<uint8>& accountBytes, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs )
    {
        LoginStoreLogicInternal::AccountRecord account;
        if ( LoginStoreLogicInternal::decodeAccount( accountBytes, account ) == false )
            return LoginResult::StoreUnavailable;
        ++account._failedCount;
        LoginResult result = LoginResult::WrongCredentials;
        if ( account._failedCount >= _settings._maxFailedCount )
        {
            account._failedCount   = 0;
            account._lockedUntilMs = nowMs + _settings._lockoutMs;
            outRetryAfterMs        = _settings._lockoutMs;
            result                 = LoginResult::AccountLocked;
        }
        ServiceTransaction transaction;
        transaction.put( LoginStoreLogicInternal::getAccountTable(), nameKey, LoginStoreLogicInternal::encodeAccount( account ), accountVersion );
        // 동시에 틀린 시도가 겹치면 하나는 세지 못한다(Conflict) — 시도 제한(버킷)이 따로 막으므로 다시 읽지 않는다.
        (void)_connection.commit( transaction );
        return result;
    }

    void LoginStoreLogic::burnPasswordHash( string_view password )
    {
        const uint8 arrSalt[LoginConstant::kSaltSize]{};
        uint8       arrHash[LoginConstant::kPasswordHashSize];
        const bool  bWithinLimit = password.size() <= static_cast<size_t>( LoginConstant::kMaxPasswordSize );
        (void)_crypto.computePasswordHash( bWithinLimit ? password : string_view{}, arrSalt, LoginConstant::kSaltSize, _settings._passwordHashParams, arrHash,
                                           LoginConstant::kPasswordHashSize );
    }
} // namespace sw
