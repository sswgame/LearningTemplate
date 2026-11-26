#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"

#include "Core/Encoding/HexUtil.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoginStoreLogicInternal
        {
            static constexpr int32 kMaxLinkCount       = 16;
            static constexpr int32 kDigestHexSize      = LoginConstant::kDigestSize * 2;
            static constexpr utf8  kDigestLabel[]      = "sw-login-token-digest-v1";
            static constexpr utf8  kGuestLabel[]       = "sw-guest-v1";
            static constexpr utf8  kSubjectLabel[]     = "sw-platform-subject-v1:";
            static constexpr utf8  kGuestNamePrefix[]  = "Guest-";
            static constexpr utf8  kPlayerNamePrefix[] = "Player";
            static constexpr int32 kMaxHintNameSize    = 12;

            static const ServiceRecordTable<LoginAccountRecord>& getAccountTable()
            {
                static const ServiceRecordTable<LoginAccountRecord> s_table{ "login_account" };
                return s_table;
            }

            static const ServiceRecordTable<LoginProfileRecord>& getAccountIDTable()
            {
                static const ServiceRecordTable<LoginProfileRecord> s_table{ "login_account_id" };
                return s_table;
            }

            static const ServiceRecordTable<LoginSessionRecord>& getSessionTable()
            {
                static const ServiceRecordTable<LoginSessionRecord> s_table{ "login_session" };
                return s_table;
            }

            static const ServiceIndexTable& getSessionExpiryTable()
            {
                static const ServiceIndexTable s_table{ "login_session_expiry" };
                return s_table;
            }

            static const ServiceLinkTable& getAccountSessionTable()
            {
                static const ServiceLinkTable s_table{ "login_account_session" };
                return s_table;
            }

            static const ServiceLinkTable& getGuestTable()
            {
                static const ServiceLinkTable s_table{ "account_guest" };
                return s_table;
            }

            static const ServiceLinkTable& getExternalTable()
            {
                static const ServiceLinkTable s_table{ "login_external" };
                return s_table;
            }

            static const ServiceRecordTable<LoginExternalSubjectRecord>& getAccountExternalTable()
            {
                static const ServiceRecordTable<LoginExternalSubjectRecord> s_table{ "login_account_external" };
                return s_table;
            }

            static const ServiceIndexTable& getAfterDeleteTable()
            {
                static const ServiceIndexTable s_table{ "account_after_delete" };
                return s_table;
            }

            static const ServiceIndexTable& getDeletionTable()
            {
                static const ServiceIndexTable s_table{ "account_deletion" };
                return s_table;
            }

            static const ServiceOpaqueTable& getAccountSecretTable()
            {
                static const ServiceOpaqueTable s_table{ "login_account_secret" };
                return s_table;
            }

            static const ServiceOpaqueTable& getRevokeTable()
            {
                static const ServiceOpaqueTable s_table{ "login_revoke_queue" };
                return s_table;
            }

            using AccountRecord = LoginAccountRecord;
            using ProfileRecord = LoginProfileRecord;
            using SessionRecord = LoginSessionRecord;

            /** @brief 세션을 지금 쓸 수 있는지 판정합니다. 쓸 수 없으면 그 이유를 결과로 반환합니다. */
            static LoginServiceResult evaluateSession( const SessionRecord& session, int64 nowMs )
            {
                if ( session._revokeReason != LoginRevokeReason::None )
                    return LoginResult::Revoked;
                if ( nowMs >= session._expiresAtMs )
                    return LoginResult::Expired;
                if ( session._resumeDeadlineMs != 0 && nowMs >= session._resumeDeadlineMs )
                    return LoginResult::Expired;
                return LoginResult::Ok;
            }

            [[nodiscard]] static bool decodeAccount( const ServiceRecord& raw, AccountRecord& outRecord ) { return getAccountTable().decode( raw, outRecord ); }

            [[nodiscard]] static bool decodeProfile( const ServiceRecord& raw, uint64 version, ProfileRecord& outRecord )
            {
                ProfileRecord record;
                if ( getAccountIDTable().decode( raw, record ) == false )
                    return false;
                record._version = version;
                outRecord       = std::move( record );
                return true;
            }

            [[nodiscard]] static bool decodeSession( const ServiceRecord& raw, SessionRecord& outRecord ) { return getSessionTable().decode( raw, outRecord ); }

            /** @brief 계정 → 외부 주체 연결의 주체 다이제스트를 읽습니다. 깨졌으면(표가 오류 로그를 남긴다) false 입니다. */
            [[nodiscard]] static bool decodeSubjectDigest( const ServiceRecord& raw, string& outDigestHex )
            {
                LoginExternalSubjectRecord record;
                if ( getAccountExternalTable().decode( raw, record ) == false )
                    return false;
                outDigestHex = std::move( record._subjectDigestHex );
                return true;
            }

            /**
             * @brief 세션 레코드를 쓰고 그 만료 시각의 TTL 색인 줄을 함께 넣습니다. 세션을 쓰는 모든 곳이 이 함수를 지난다 — 만료를 늦춘 세션은 새 줄이 맡는다.
             * @details 밀려난 세션(묘비) · 끝난 세션은 로그아웃하지 않으면 지우는 길이 없어 로그인마다 쌓였다. 줄은 만료 뒤 TTL 규칙(`makeSessionTTLRule`)이 지운다.
             */
            static void stageSession( ServiceTransaction& inoutTransaction, string_view sessionKey, const SessionRecord& session, uint64 expectedVersion )
            {
                inoutTransaction.put( getSessionTable(), sessionKey, session, expectedVersion );
                inoutTransaction.put( getSessionExpiryTable(), ServiceTTLRule::makeTTLIndexKey( session._expiresAtMs, sessionKey ), ServiceRecord::kAnyVersion );
            }

            /** @brief 세션 레코드의 만료 시각을 읽습니다(TTL 쓸기). 읽지 못하면 지우지 않는다. */
            [[nodiscard]] static bool readSessionExpiryMs( const ServiceRecord& record, int64& outTimeMs )
            {
                SessionRecord session;
                if ( decodeSession( record, session ) == false )
                    return false;
                outTimeMs = session._expiresAtMs;
                return true;
            }

            /** @brief ASCII 영숫자와 `_` 로 이루어진 3~16자 이름을 소문자 키로 바꿉니다. 규칙에 맞지 않으면 false 를 반환합니다. */
            [[nodiscard]] static bool normalizeLoginName( string_view loginName, string& outKey )
            {
                const int32 size = static_cast<int32>( loginName.size() );
                if ( size < LoginConstant::kMinLoginNameSize || LoginConstant::kMaxLoginNameSize < size )
                    return false;
                outKey.clear();
                outKey.reserve( loginName.size() );
                for ( const utf8 ch : loginName )
                {
                    const bool bLetter                   = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                    const bool bDigit                    = '0' <= ch && ch <= '9';
                    const bool bInvalidLoginKeyCharacter = bLetter == false && bDigit == false && ch != '_';
                    if ( bInvalidLoginKeyCharacter )
                        return false;
                    outKey.push_back( StringUtil::toLowerChar( ch ) );
                }
                return true;
            }

            static bool isValidPassword( string_view password )
            {
                const int32 size = static_cast<int32>( password.size() );
                return LoginConstant::kMinPasswordSize <= size && size <= LoginConstant::kMaxPasswordSize;
            }

            [[nodiscard]] static bool computeTokenDigest( ILoginCrypto& crypto, const uint8 ( &arrSecret )[LoginConstant::kTokenSecretSize],
                                                          uint8 ( &outDigest )[LoginConstant::kDigestSize] )
            {
                return crypto.computeKeyedHash( arrSecret, LoginConstant::kTokenSecretSize, reinterpret_cast<const uint8*>( kDigestLabel ),
                                                static_cast<int32>( sizeof( kDigestLabel ) - 1 ), outDigest, LoginConstant::kDigestSize );
            }

            [[nodiscard]] static bool computeGuestDigestHex( ILoginCrypto& crypto, const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], string& outHex )
            {
                uint8 arrDigest[LoginConstant::kDigestSize];
                if ( crypto.computeKeyedHash( arrDeviceSecret, LoginConstant::kDeviceSecretSize, reinterpret_cast<const uint8*>( kGuestLabel ),
                                              static_cast<int32>( sizeof( kGuestLabel ) - 1 ), arrDigest, LoginConstant::kDigestSize ) == false )
                    return false;
                outHex = HexUtil::encodeBytes( arrDigest, static_cast<size_t>( LoginConstant::kDigestSize ) );
                return true;
            }

            /** @brief 외부 주체의 다이제스트를 계산합니다. 키는 주체 문자열이고 정보는 라벨 뒤에 제공자 이름을 붙인 것이므로, 제공자가 다르면 같은 주체 문자열도 다른 다이제스트가 나옵니다. */
            [[nodiscard]] static bool computeSubjectDigestHex( ILoginCrypto& crypto, string_view provider, string_view subject, string& outHex )
            {
                StringBuilder<constant::buffer::kMax128> info;
                info.appendFormat( "%#%#", kSubjectLabel, provider );
                uint8 arrDigest[LoginConstant::kDigestSize];
                if ( crypto.computeKeyedHash( reinterpret_cast<const uint8*>( subject.data() ), static_cast<int32>( subject.size() ), reinterpret_cast<const uint8*>( info.c_str() ),
                                              static_cast<int32>( info.size() ), arrDigest, LoginConstant::kDigestSize ) == false )
                    return false;
                outHex = HexUtil::encodeBytes( arrDigest, static_cast<size_t>( LoginConstant::kDigestSize ) );
                return true;
            }

            static string makeExternalKey( string_view provider, string_view digestHex )
            {
                StringBuilder<constant::buffer::kMax256> key;
                key.appendFormat( "%#/%#", provider, digestHex );
                return string( key.view() );
            }

            static string makeAccountExternalKey( uint64 accountID, string_view provider )
            {
                StringBuilder<constant::buffer::kMax128> key;
                key.appendFormat( "%#/%#", ServiceKeyUtil::makeHex64( accountID ), provider );
                return string( key.view() );
            }

            /** @brief 철회 대기열의 키 `<제공자>/<계정 16진>` 입니다. */
            static string makeRevokeKey( string_view provider, uint64 accountID )
            {
                string key{ provider };
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, accountID );
                return key;
            }

            /** @brief 외부 로그인이 준 연동 비밀(이미 암호화됨)을 계정 옆 칸에 씁니다. 비밀이 없으면 아무것도 붙이지 않습니다(같은 제공자로 다시 로그인하면 덮어쓴다). */
            static void stageLinkSecret( ServiceTransaction& inoutTransaction, uint64 accountID, string_view provider, const vector<uint8>* pSealedSecret )
            {
                if ( pSealedSecret == nullptr || pSealedSecret->empty() )
                    return;
                inoutTransaction.put( getAccountSecretTable(), makeAccountExternalKey( accountID, provider ), *pSealedSecret );
            }

            /** @brief 계정의 연동 비밀이 있으면 칸에서 지우고 철회 대기열로 옮기는 쓰기를 붙입니다(탈퇴 · 연동 해제). 비밀이 없으면 아무것도 붙이지 않습니다. */
            static ServiceStoreResult stageSecretToRevokeQueue( IServiceStoreConnection& connection, ServiceTransaction& inoutTransaction, uint64 accountID, string_view provider )
            {
                const string             secretKey = makeAccountExternalKey( accountID, provider );
                ServiceRecord            raw;
                const ServiceStoreResult readResult = connection.readRecord( getAccountSecretTable(), secretKey, raw );
                if ( readResult == ServiceStoreResult::NotFound )
                    return ServiceStoreResult::Ok;
                if ( readResult != ServiceStoreResult::Ok )
                    return readResult;
                inoutTransaction.erase( getAccountSecretTable(), secretKey, raw._version );
                inoutTransaction.put( getRevokeTable(), makeRevokeKey( provider, accountID ), ServiceOpaqueTable::getBytes( raw ) );
                return ServiceStoreResult::Ok;
            }

            static string makeDeletionKey( int64 dueMs, uint64 accountID )
            {
                string key = ServiceKeyUtil::makeHex64( static_cast<uint64>( dueMs < 0 ? 0 : dueMs ) );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, accountID );
                return key;
            }

            /** @brief 표시 이름 끝에 붙이는 계정 id 꼬리입니다. 16진 문자열의 뒤쪽 @p digitCount 글자입니다. */
            static string makeIDSuffix( uint64 accountID, int32 digitCount )
            {
                const string hex = ServiceKeyUtil::makeHex64( accountID );
                return hex.substr( hex.size() - static_cast<size_t>( digitCount ) );
            }

            static string makeGuestDisplayName( uint64 accountID )
            {
                StringBuilder<constant::buffer::kMax64> name;
                name.appendFormat( "%#%#", kGuestNamePrefix, makeIDSuffix( accountID, 6 ) );
                return string( name.view() );
            }

            /** @brief 제공자가 준 이름에서 ASCII 영숫자와 `_` 만 12자까지 남기고, "-" 와 계정 id 꼬리 4자리를 붙입니다. 이름이 없으면 "Player" 를 씁니다. */
            static string makePlatformDisplayName( string_view hint, uint64 accountID )
            {
                string name;
                for ( const utf8 ch : hint )
                {
                    const bool bLetter        = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                    const bool bDigit         = '0' <= ch && ch <= '9';
                    const bool bHintCharacter = bLetter || bDigit || ch == '_';
                    if ( bHintCharacter )
                        name.push_back( ch );
                    if ( static_cast<int32>( name.size() ) >= kMaxHintNameSize )
                        break;
                }
                if ( name.empty() )
                    name = kPlayerNamePrefix;
                name.push_back( '-' );
                name += makeIDSuffix( accountID, 4 );
                return name;
            }

            /** @brief 0이 아닌 64비트 난수를 반환합니다. 0은 "없음" 을 뜻하기 때문입니다. */
            [[nodiscard]] static bool makeNonZeroID( ILoginCrypto& crypto, uint64& outID )
            {
                for ( int32 attempt = 0; attempt < 4; ++attempt )
                {
                    uint8 arrByte[8];
                    if ( crypto.fillRandom( arrByte, 8 ) == false )
                        return false;
                    uint64 value = 0;
                    for ( int32 byteIndex = 0; byteIndex < 8; ++byteIndex )
                    {
                        value |= static_cast<uint64>( arrByte[byteIndex] ) << ( byteIndex * 8 );
                    }
                    if ( value != 0 )
                    {
                        outID = value;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 프로필을 읽습니다. 없으면 NotFound 를 반환합니다. */
            static ServiceStoreResult readProfile( IServiceStoreConnection& connection, uint64 accountID, ProfileRecord& outProfile )
            {
                ServiceRecord            raw;
                const ServiceStoreResult read = connection.readRecord( getAccountIDTable(), ServiceKeyUtil::makeHex64( accountID ), raw );
                if ( read != ServiceStoreResult::Ok )
                    return read;
                if ( decodeProfile( raw, raw._version, outProfile ) == false )
                    return ServiceStoreResult::Unavailable;
                return ServiceStoreResult::Ok;
            }

            /** @brief 계정의 외부 연결(제공자에서 주체 다이제스트로 가는 맵)을 읽습니다. */
            static ServiceStoreResult listAccountLinks( IServiceStoreConnection& connection, uint64 accountID, vector<ServiceRecord>& outListLink )
            {
                string prefix = ServiceKeyUtil::makeHex64( accountID );
                prefix.push_back( '/' );
                return ServiceRecordReader::readPage( connection, getAccountExternalTable(), prefix, "", kMaxLinkCount, false, outListLink )._result;
            }

            static AccountIdentity makeIdentity( uint64 accountID, const ProfileRecord& profile )
            {
                AccountIdentity identity;
                identity._accountID   = accountID;
                identity._displayName = profile._displayName;
                identity._bGuest      = profile._bGuest;
                return identity;
            }

            static void fillGrantIdentity( uint64 accountID, const ProfileRecord& profile, const LoginSessionToken& token, int64 expiresAtMs, uint64 replacedSessionID,
                                           LoginGrant& outGrant )
            {
                outGrant._identity          = makeIdentity( accountID, profile );
                outGrant._token             = token;
                outGrant._expiresAtMs       = expiresAtMs;
                outGrant._replacedSessionID = replacedSessionID;
                outGrant._deletionDueMs     = profile._deletionDueMs;
            }

            static ServiceAuditEntry makeAuditEntry( uint64 accountID, const utf8* pAction, int64 nowMs )
            {
                ServiceAuditEntry entry;
                entry._actor   = ServiceAuditLog::makeAccountActor( accountID );
                entry._action  = pAction;
                entry._subject = entry._actor;
                entry._timeMs  = nowMs < 0 ? 0 : nowMs;
                return entry;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoginStoreLogic::LoginStoreLogic( IServiceStoreConnection& connection, ILoginCrypto& crypto, const LoginServiceConfig& settings,
                                      const LoginTicketAuthority& ticketAuthority, LoginStoreResult& outResult )
        : _connection{ connection }
        , _crypto{ crypto }
        , _settings{ settings }
        , _ticketAuthority{ ticketAuthority }
        , _result{ outResult }
    {
    }

    LoginServiceResult LoginStoreLogic::validateNewCredential( const LoginCredential& credential )
    {
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;
        if ( Internal::isValidPassword( credential._password.reveal() ) == false )
            return LoginResult::InvalidPassword;
        return LoginResult::Ok;
    }

    LoginServiceResult LoginStoreLogic::registerAccount( const LoginCredential& credential, const PasswordHashRecord& prepared, uint64* pOutAccountID )
    {
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;

        Internal::AccountRecord account;
        account._displayName = credential._loginName;
        account.setHashParams( prepared._params );
        Memory::copy( account._arrSalt, prepared._arrSalt, LoginConstant::kSaltSize );
        Memory::copy( account._passwordHash.reveal(), prepared._arrHash, LoginConstant::kPasswordHashSize );

        Internal::ProfileRecord profile;
        profile._nameKey     = nameKey;
        profile._displayName = credential._loginName;
        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.Register", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            if ( Internal::makeNonZeroID( _crypto, account._accountID ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountTable(), nameKey, account, ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( account._accountID ), profile, ServiceRecord::kAbsentVersion );
            Ledger::requireNoTombstone( account._accountID, transaction ); // 지운 계정의 id 는 다시 쓰지 않는다 — 겹치면 아래 Conflict 로 새 id 를 뽑는다
            ServiceCommitInfo        info;
            const ServiceStoreResult result = attempt.commit( transaction, &info );
            if ( result == ServiceStoreResult::Conflict && info._conflictIndex == 0 )
                return LoginResult::NameTaken;
            if ( result == ServiceStoreResult::Conflict )
                return nullopt; // 계정 id 가 겹쳤으므로(확률 2^-64) 새 id 로 다시 시도한다. SQL 의 직렬화 실패(버전 -1)도 다시 본다
            if ( result != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( pOutAccountID != nullptr )
                *pOutAccountID = account._accountID;
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::readLoginChallenge( string_view loginName, int64 nowMs, PasswordHashChallenge& outChallenge, LoginGrant& outGrant )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        outChallenge   = PasswordHashChallenge{};
        // 없는 이름의 가짜 문제 — 설정 매개변수와 빈 솔트로 해시를 한 번 치른다(예전 `burnPasswordHash` 와 같은 비용 · 같은 시간).
        outChallenge._params = _settings._passwordHashParams;
        if ( Internal::normalizeLoginName( loginName, outChallenge._nameKey ) == false )
            return LoginResult::Ok;
        ServiceRecord            accountRaw;
        const ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountTable(), outChallenge._nameKey, accountRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::Ok;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        Internal::AccountRecord account;
        if ( Internal::decodeAccount( accountRaw, account ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        if ( account._lockedUntilMs > nowMs )
        {
            outGrant._retryAfterMs = account._lockedUntilMs - nowMs;
            return LoginResult::AccountLocked; // 잠긴 계정에는 해시를 치르지 않는다(지금과 같다)
        }
        outChallenge._params = account.getHashParams();
        Memory::copy( outChallenge._arrSalt, account._arrSalt, LoginConstant::kSaltSize );
        outChallenge._bKnownAccount = SW_TRUE;
        outChallenge._bRehash       = account.getHashParams() != _settings._passwordHashParams ? SW_TRUE : SW_FALSE;
        return LoginResult::Ok;
    }

    LoginServiceResult LoginStoreLogic::commitLogin( const PasswordHashChallenge& challenge, const PasswordHashResult& hashResult, int64 nowMs, LoginGrant& outGrant,
                                                     bool& outbHashStale )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        outbHashStale  = false;
        if ( challenge._bKnownAccount == SW_FALSE )
            return LoginResult::WrongCredentials;
        const string& nameKey = challenge._nameKey;

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.Login", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            ServiceRecord            accountRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountTable(), nameKey, accountRaw );
            if ( readResult == ServiceStoreResult::NotFound )
                return LoginResult::WrongCredentials; // 해시하는 사이 계정이 지워졌다
            if ( readResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            Internal::AccountRecord account;
            if ( Internal::decodeAccount( accountRaw, account ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            // 계산한 해시는 솔트 · 매개변수에만 달렸다 — 같으면 그대로 쓰고, 다르면(그사이 비밀번호 · 매개변수가 바뀌었다) 해시 단계부터 다시 한다.
            const bool bSameInputs = account.getHashParams() == challenge._params && Memory::compare( account._arrSalt, challenge._arrSalt, LoginConstant::kSaltSize ) == 0;
            if ( bSameInputs == false )
            {
                outbHashStale = true;
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            }
            if ( account._lockedUntilMs > nowMs )
            {
                outGrant._retryAfterMs = account._lockedUntilMs - nowMs;
                return LoginResult::AccountLocked;
            }
            if ( SecureUtil::equalsConstantTime( hashResult._arrHash, account._passwordHash.reveal(), LoginConstant::kPasswordHashSize ) == false )
                return recordFailure( nameKey, account, accountRaw._version, nowMs, outGrant._retryAfterMs );

            const LoginServiceResult sanction = evaluateSanction( account._accountID, nowMs, outGrant );
            if ( sanction != LoginResult::Ok )
                return sanction;
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, account._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            ServiceTransaction transaction;
            // 매개변수가 바뀐 계정이면 해시 단계가 새 솔트 · 설정 매개변수로 계산해 두었다 — 그 값으로 바꾼다. 그사이 설정이 또 바뀌었으면 다음 로그인이 한다.
            const bool bRehash         = hashResult._bHasRehash == SW_TRUE && hashResult._rehash._params == _settings._passwordHashParams && account.getHashParams() != _settings._passwordHashParams;
            const bool bAccountChanged = bRehash || account._failedCount != 0 || account._lockedUntilMs != 0;
            if ( bRehash )
            {
                account.setHashParams( hashResult._rehash._params );
                Memory::copy( account._arrSalt, hashResult._rehash._arrSalt, LoginConstant::kSaltSize );
                Memory::copy( account._passwordHash.reveal(), hashResult._rehash._arrHash, LoginConstant::kPasswordHashSize );
            }
            if ( bAccountChanged )
            {
                account._failedCount   = 0;
                account._lockedUntilMs = 0;
                transaction.put( Internal::getAccountTable(), nameKey, account, accountRaw._version );
            }

            LoginSessionToken        token;
            int64                    expiresAtMs       = 0;
            uint64                   replacedSessionID = 0;
            const LoginServiceResult staged            = stageSessionOpen( account._accountID, nowMs, transaction, token, expiresAtMs, replacedSessionID );
            if ( staged != LoginResult::Ok )
                return staged;
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt; // 같은 계정이 그사이 다른 곳에서 로그인했거나 실패 횟수가 바뀌었으므로 다시 읽는다(솔트 · 매개변수가 같으면 계산한 해시를 다시 쓴다)
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable ); // 적용됐는지 알 수 없다. 클라이언트가 다시 로그인하면 그 세션을 밀어낸다

            recordSessionOpened( account._accountID, token, replacedSessionID );
            Internal::fillGrantIdentity( account._accountID, profile, token, expiresAtMs, replacedSessionID, outGrant );
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], int64 nowMs, LoginGrant& outGrant )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        string digestHex;
        if ( Internal::computeGuestDigestHex( _crypto, arrDeviceSecret, digestHex ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.GuestLogin", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            ServiceRecord            guestRaw;
            const ServiceStoreResult guestRead = _connection.readRecord( Internal::getGuestTable(), digestHex, guestRaw );
            if ( guestRead != ServiceStoreResult::Ok && guestRead != ServiceStoreResult::NotFound )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            ServiceTransaction      transaction;
            Internal::ProfileRecord profile;
            uint64                  accountID = 0;
            const bool              bCreate   = guestRead == ServiceStoreResult::NotFound;
            if ( bCreate )
            {
                if ( Internal::makeNonZeroID( _crypto, accountID ) == false )
                    return LoginServiceResult::fail( OnlineError::kUnavailable );
                profile._displayName    = Internal::makeGuestDisplayName( accountID );
                profile._guestDigestHex = digestHex;
                profile._bGuest         = SW_TRUE;
                transaction.put( Internal::getGuestTable(), digestHex, accountID, ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( accountID ), profile, ServiceRecord::kAbsentVersion );
                Ledger::requireNoTombstone( accountID, transaction ); // 지운 계정의 id 는 다시 쓰지 않는다
            }
            else
            {
                if ( Internal::getGuestTable().decode( digestHex, guestRaw, accountID ) == false || Internal::readProfile( _connection, accountID, profile ) != ServiceStoreResult::Ok )
                    return LoginServiceResult::fail( OnlineError::kUnavailable );
                const LoginServiceResult sanction = evaluateSanction( accountID, nowMs, outGrant );
                if ( sanction != LoginResult::Ok )
                    return sanction;
            }

            LoginSessionToken        token;
            int64                    expiresAtMs       = 0;
            uint64                   replacedSessionID = 0;
            const LoginServiceResult staged            = stageSessionOpen( accountID, nowMs, transaction, token, expiresAtMs, replacedSessionID );
            if ( staged != LoginResult::Ok )
                return staged;
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt; // 같은 장치가 그사이 계정을 만들었거나 다른 곳에서 로그인했으므로 다시 읽는다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            recordSessionOpened( accountID, token, replacedSessionID );
            Internal::fillGrantIdentity( accountID, profile, token, expiresAtMs, replacedSessionID, outGrant );
            outGrant._bCreated = bCreate ? SW_TRUE : SW_FALSE;
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::platformLogin( string_view provider, string_view subject, string_view displayNameHint, const vector<uint8>* pSealedLinkSecret, int64 nowMs,
                                                       LoginGrant& outGrant )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        if ( AccountUtil::isValidLowerToken( provider, LoginConstant::kMaxProviderNameSize ) == false || subject.empty() )
            return LoginServiceResult::fail( OnlineError::kInvalidRequest );
        string digestHex;
        if ( Internal::computeSubjectDigestHex( _crypto, provider, subject, digestHex ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        const string externalKey = Internal::makeExternalKey( provider, digestHex );

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.PlatformLogin", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            ServiceRecord            externalRaw;
            const ServiceStoreResult externalRead = _connection.readRecord( Internal::getExternalTable(), externalKey, externalRaw );
            if ( externalRead != ServiceStoreResult::Ok && externalRead != ServiceStoreResult::NotFound )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            ServiceTransaction      transaction;
            Internal::ProfileRecord profile;
            uint64                  accountID = 0;
            const bool              bCreate   = externalRead == ServiceStoreResult::NotFound;
            if ( bCreate )
            {
                if ( Internal::makeNonZeroID( _crypto, accountID ) == false )
                    return LoginServiceResult::fail( OnlineError::kUnavailable );
                profile._displayName = Internal::makePlatformDisplayName( displayNameHint, accountID );
                transaction.put( Internal::getExternalTable(), externalKey, accountID, ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountExternalTable(), Internal::makeAccountExternalKey( accountID, provider ), LoginExternalSubjectRecord{ digestHex, {} },
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( accountID ), profile, ServiceRecord::kAbsentVersion );
                Ledger::requireNoTombstone( accountID, transaction ); // 지운 계정의 id 는 다시 쓰지 않는다
            }
            else
            {
                if ( Internal::getExternalTable().decode( externalKey, externalRaw, accountID ) == false || Internal::readProfile( _connection, accountID, profile ) != ServiceStoreResult::Ok )
                    return LoginServiceResult::fail( OnlineError::kUnavailable );
                const LoginServiceResult sanction = evaluateSanction( accountID, nowMs, outGrant );
                if ( sanction != LoginResult::Ok )
                    return sanction;
            }

            LoginSessionToken        token;
            int64                    expiresAtMs       = 0;
            uint64                   replacedSessionID = 0;
            const LoginServiceResult staged            = stageSessionOpen( accountID, nowMs, transaction, token, expiresAtMs, replacedSessionID );
            if ( staged != LoginResult::Ok )
                return staged;
            Internal::stageLinkSecret( transaction, accountID, provider, pSealedLinkSecret );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            recordSessionOpened( accountID, token, replacedSessionID );
            Internal::fillGrantIdentity( accountID, profile, token, expiresAtMs, replacedSessionID, outGrant );
            outGrant._bCreated = bCreate ? SW_TRUE : SW_FALSE;
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant )
    {
        using Internal          = LoginStoreLogicInternal;
        outGrant                = LoginGrant{};
        const string sessionKey = ServiceKeyUtil::makeHex64( token._sessionID );
        uint8        arrDigest[LoginConstant::kDigestSize];
        if ( Internal::computeTokenDigest( _crypto, token._secret.reveal(), arrDigest ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.ResumeSession", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw );
            if ( readResult == ServiceStoreResult::NotFound )
                return LoginResult::InvalidToken;
            if ( readResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            Internal::SessionRecord session;
            if ( Internal::decodeSession( sessionRaw, session ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( SecureUtil::equalsConstantTime( arrDigest, session._tokenDigest.reveal(), LoginConstant::kDigestSize ) == false )
                return LoginResult::InvalidToken;
            const LoginServiceResult state = Internal::evaluateSession( session, nowMs );
            if ( state != LoginResult::Ok )
            {
                outGrant._revokeReason = session._revokeReason;
                return state;
            }
            const LoginServiceResult sanction = evaluateSanction( session._accountID, nowMs, outGrant );
            if ( sanction != LoginResult::Ok )
                return sanction;
            // 신원은 비밀을 바꾸기 **전에** 읽는다. 못 읽었는데 빈 신원으로 Ok 를 주면 안 되고, 클라이언트가 가진 토큰도 살려 둬야 하기 때문이다. validateSession 과 같은 결과이다.
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, session._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            // 재접속할 때마다 비밀을 새로 바꾼다. 이전 토큰은 이 커밋으로 무효가 된다.
            LoginSessionToken rotated;
            rotated._sessionID     = token._sessionID;
            const bool bSecretMade = _crypto.fillRandom( rotated._secret.reveal(), LoginConstant::kTokenSecretSize );
            if ( bSecretMade == false || Internal::computeTokenDigest( _crypto, rotated._secret.reveal(), session._tokenDigest.reveal() ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            session._resumeDeadlineMs = 0;
            ++session._rotation;
            ServiceTransaction transaction;
            Internal::stageSession( transaction, sessionKey, session, sessionRaw._version );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt; // 같은 토큰으로 그사이 돌아온 쪽이 있다. 다시 읽으면 비밀이 달라서 InvalidToken 이 된다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );

            _result._listOnline.push_back( LoginSessionRef{ session._accountID, token._sessionID, LoginRevokeReason::None } );
            _result._listEvent.push_back( LoginEvent{ session._accountID, token._sessionID, LoginRevokeReason::None, LoginEvent::Kind::Resumed } );
            outGrant._identity      = Internal::makeIdentity( session._accountID, profile );
            outGrant._deletionDueMs = profile._deletionDueMs;
            outGrant._token         = rotated;
            outGrant._expiresAtMs   = session._expiresAtMs;
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason )
    {
        using Internal = LoginStoreLogicInternal;
        ServiceRecord            sessionRaw;
        const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionID ), sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::InvalidToken;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        Internal::SessionRecord session;
        if ( Internal::decodeSession( sessionRaw, session ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        uint8 arrDigest[LoginConstant::kDigestSize];
        if ( Internal::computeTokenDigest( _crypto, token._secret.reveal(), arrDigest ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        if ( SecureUtil::equalsConstantTime( arrDigest, session._tokenDigest.reveal(), LoginConstant::kDigestSize ) == false )
            return LoginResult::InvalidToken;
        const LoginServiceResult state = Internal::evaluateSession( session, nowMs );
        if ( pOutRevokeReason != nullptr )
            *pOutRevokeReason = session._revokeReason;
        if ( state != LoginResult::Ok )
            return state;
        return readIdentity( _connection, session._accountID, outIdentity ) == ServiceStoreResult::Ok ? LoginResult::Ok : LoginServiceResult::fail( OnlineError::kUnavailable );
    }

    LoginServiceResult LoginStoreLogic::logout( const LoginSessionToken& token, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        const string             accountKey = ServiceKeyUtil::makeHex64( identity._accountID );
        const LoginServiceResult erased     = ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.Logout", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
            {
            ServiceRecord      linkRaw;
            ServiceTransaction transaction;
            transaction.erase( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionID ) );
            const ServiceStoreResult readResult      = attempt.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
            uint64                   linkedSessionID = 0;
            if ( readResult == ServiceStoreResult::Ok && Internal::getAccountSessionTable().decode( accountKey, linkRaw, linkedSessionID ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( linkedSessionID == token._sessionID )
                transaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt; // 다른 쪽이 같은 계정의 세션을 건드렸다 — 지우지 않고 Ok 로 답하면 로그아웃한 토큰이 살아 있다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            return LoginServiceResult{ LoginResult::Ok };
        } );
        if ( erased != LoginResult::Ok )
            return erased;
        _result._listOffline.push_back( LoginSessionRef{ identity._accountID, token._sessionID, LoginRevokeReason::LoggedOut } );
        _result._listEvent.push_back( LoginEvent{ identity._accountID, token._sessionID, LoginRevokeReason::LoggedOut, LoginEvent::Kind::LoggedOut } );
        return LoginResult::Ok;
    }

    void LoginStoreLogic::markDisconnected( uint64 sessionID, int64 nowMs )
    {
        using Internal          = LoginStoreLogicInternal;
        const string sessionKey = ServiceKeyUtil::makeHex64( sessionID );
        bool         bReported  = false;
        const auto   start      = [&]( ServiceCommitAttempt& attempt ) -> optional<ServiceStoreResult>
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = attempt.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw );
            if ( readResult != ServiceStoreResult::Ok )
                return readResult;
            Internal::SessionRecord session;
            if ( Internal::decodeSession( sessionRaw, session ) == false )
                return ServiceStoreResult::Invalid;
            if ( bReported == false )
                _result._listDisconnected.push_back( LoginSessionRef{ session._accountID, sessionID, LoginRevokeReason::None } );
            bReported = true;
            if ( session._revokeReason != LoginRevokeReason::None || session._resumeDeadlineMs != 0 )
                return ServiceStoreResult::Ok;
            session._resumeDeadlineMs = nowMs + _settings._reconnectGraceMs;
            ServiceTransaction transaction;
            Internal::stageSession( transaction, sessionKey, session, sessionRaw._version );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            return commitResult;
        };
        const ServiceStoreResult started = ServiceCommitRunner::run<ServiceStoreResult>( _connection, "Account.MarkDisconnected", start );
        if ( started != ServiceStoreResult::Ok && started != ServiceStoreResult::NotFound ) // 없는 세션은 이미 로그아웃한 것이다
            SW_LOG_WARNING_ID( "LoginStoreLogic.SessionCouldNotStart", "session %# could not start its reconnect grace (store %#)", sessionKey.c_str(), static_cast<int32>( started ) );
    }

    LoginServiceResult LoginStoreLogic::revokeAccountSessions( uint64 accountID, LoginRevokeReason reason, int64 nowMs )
    {
        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.RevokeSessions", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            ServiceTransaction       transaction;
            uint64                   sessionID = 0;
            const LoginServiceResult staged    = stageSessionRevoke( accountID, reason, nowMs, transaction, sessionID );
            if ( staged != LoginResult::Ok )
                return staged;
            if ( sessionID == 0 )
                return LoginResult::Ok;
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            _result._listRevoked.push_back( LoginSessionRef{ accountID, sessionID, reason } );
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::issueGameTicket( const LoginSessionToken& token, const hashed_string& serverID, int64 nowMs, NetGameTicket& outTicket )
    {
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        const bool bIssued = _ticketAuthority.issueTicket( identity._accountID, token._sessionID, serverID, nowMs + _settings._ticketLifetimeMs, outTicket );
        return bIssued ? LoginResult::Ok : LoginServiceResult::fail( OnlineError::kUnavailable );
    }

    LoginServiceResult LoginStoreLogic::linkCredential( const LoginSessionToken& token, const LoginCredential& credential, const PasswordHashRecord& prepared, int64 nowMs,
                                                        AccountIdentity& outIdentity )
    {
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;

        Internal::AccountRecord account;
        account._accountID   = identity._accountID;
        account._displayName = credential._loginName;
        account.setHashParams( prepared._params );
        Memory::copy( account._arrSalt, prepared._arrSalt, LoginConstant::kSaltSize );
        Memory::copy( account._passwordHash.reveal(), prepared._arrHash, LoginConstant::kPasswordHashSize );

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.LinkCredential", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( profile._nameKey.empty() == false )
                return LoginResult::AlreadyLinked;
            const uint64 profileVersion = profile._version;
            profile._nameKey            = nameKey;
            profile._displayName        = credential._loginName;
            profile._bGuest             = SW_FALSE;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountTable(), nameKey, account, ServiceRecord::kAbsentVersion );
            if ( _settings._bKeepGuestDeviceAfterLink == SW_FALSE && profile._guestDigestHex.empty() == false )
            {
                transaction.erase( Internal::getGuestTable(), profile._guestDigestHex );
                profile._guestDigestHex.clear();
            }
            transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( identity._accountID ), profile, profileVersion );
            ServiceAuditLog::stageEntry( transaction, Internal::makeAuditEntry( identity._accountID, "account.link.credential", nowMs ), identity._accountID,
                                         profileVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult commitResult = attempt.commit( transaction, &info );
            if ( commitResult == ServiceStoreResult::Conflict && info._conflictIndex == 0 )
                return LoginResult::AlreadyLinked; // 이름이 다른 계정의 것이므로 합치지 않는다
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            outIdentity = Internal::makeIdentity( identity._accountID, profile );
            return LoginResult::Ok;
        } );
    }

    ServiceStoreResult LoginStoreLogic::readIsLinked( IServiceStoreConnection& connection, ILoginCrypto& crypto, uint64 accountID, string_view provider, string_view subject, bool& outbLinked )
    {
        using Internal = LoginStoreLogicInternal;
        outbLinked     = false;
        if ( AccountUtil::isValidLowerToken( provider, LoginConstant::kMaxProviderNameSize ) == false || subject.empty() )
            return ServiceStoreResult::Ok; // 이런 제공자 · 주체로는 연동할 수 없다
        string digestHex;
        if ( Internal::computeSubjectDigestHex( crypto, provider, subject, digestHex ) == false )
            return ServiceStoreResult::Unavailable;
        ServiceRecord            raw;
        const ServiceStoreResult read = connection.readRecord( Internal::getAccountExternalTable(), Internal::makeAccountExternalKey( accountID, provider ), raw );
        if ( read == ServiceStoreResult::NotFound )
            return ServiceStoreResult::Ok;
        if ( read != ServiceStoreResult::Ok )
            return read;
        string stored;
        if ( Internal::decodeSubjectDigest( raw, stored ) == false )
            return ServiceStoreResult::Corrupt;
        outbLinked = stored == digestHex;
        return ServiceStoreResult::Ok;
    }

    LoginServiceResult LoginStoreLogic::linkPlatform( const LoginSessionToken& token, string_view provider, string_view subject, const vector<uint8>* pSealedLinkSecret, int64 nowMs,
                                                      AccountIdentity& outIdentity )
    {
        using Internal = LoginStoreLogicInternal;
        if ( AccountUtil::isValidLowerToken( provider, LoginConstant::kMaxProviderNameSize ) == false || subject.empty() )
            return LoginServiceResult::fail( OnlineError::kInvalidRequest );
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        string digestHex;
        if ( Internal::computeSubjectDigestHex( _crypto, provider, subject, digestHex ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );

        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.LinkPlatform", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            const uint64 profileVersion = profile._version;
            profile._bGuest             = SW_FALSE;
            ServiceTransaction transaction;
            transaction.put( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ), identity._accountID,
                             ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountExternalTable(), Internal::makeAccountExternalKey( identity._accountID, provider ),
                             LoginExternalSubjectRecord{ digestHex, {} }, ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( identity._accountID ), profile, profileVersion );
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountID, "account.link.platform", nowMs );
            audit._after            = string( provider );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountID, profileVersion );
            Internal::stageLinkSecret( transaction, identity._accountID, provider, pSealedLinkSecret ); // 쓰기 번호가 뒤라서 위의 충돌 번호 판정(0 · 1)을 흔들지 않는다
            ServiceCommitInfo        info;
            const ServiceStoreResult commitResult = attempt.commit( transaction, &info );
            const bool               bTaken       = commitResult == ServiceStoreResult::Conflict && ( info._conflictIndex == 0 || info._conflictIndex == 1 );
            if ( bTaken )
                return LoginResult::AlreadyLinked; // 그 주체가 다른 계정의 것이거나 이 계정에 그 제공자가 이미 있다
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            outIdentity = Internal::makeIdentity( identity._accountID, profile );
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.UnlinkPlatform", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            vector<ServiceRecord> listLink;
            if ( Internal::listAccountLinks( _connection, identity._accountID, listLink ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            const string         linkKey = Internal::makeAccountExternalKey( identity._accountID, provider );
            const ServiceRecord* pLink   = nullptr;
            for ( const ServiceRecord& link : listLink )
            {
                if ( link._key == linkKey )
                    pLink = &link;
            }
            if ( pLink == nullptr )
                return LoginResult::NotLinked;
            const int32 methodCount = static_cast<int32>( listLink.size() ) + ( profile._nameKey.empty() ? 0 : 1 );
            if ( methodCount <= 1 )
                return LoginResult::LastLoginMethod;
            string digestHex;
            if ( Internal::decodeSubjectDigest( *pLink, digestHex ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            ServiceTransaction transaction;
            transaction.erase( Internal::getAccountExternalTable(), linkKey, pLink->_version );
            transaction.erase( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ) );
            if ( Internal::stageSecretToRevokeQueue( _connection, transaction, identity._accountID, provider ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountID, "account.unlink.platform", nowMs );
            audit._before           = string( provider );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountID, pLink->_version );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginServiceResult::fail( OnlineError::kUnavailable );
        } );
    }

    LoginServiceResult LoginStoreLogic::listLinks( const LoginSessionToken& token, int64 nowMs, AccountLinkSummary& outSummary )
    {
        using Internal = LoginStoreLogicInternal;
        outSummary     = AccountLinkSummary{};
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        Internal::ProfileRecord profile;
        if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        vector<ServiceRecord> listLink;
        if ( Internal::listAccountLinks( _connection, identity._accountID, listLink ) != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        const size_t prefixSize = ServiceKeyUtil::makeHex64( identity._accountID ).size() + 1;
        for ( const ServiceRecord& link : listLink )
        {
            outSummary._listProvider.push_back( AccountLinkedProvider{ string( link._key.substr( prefixSize ) ) } );
        }
        outSummary._bHasCredential = profile._nameKey.empty() ? SW_FALSE : SW_TRUE;
        outSummary._bGuest         = profile._bGuest;
        outSummary._deletionDueMs  = profile._deletionDueMs;
        return LoginResult::Ok;
    }

    LoginServiceResult LoginStoreLogic::requestDeletion( const LoginSessionToken& token, int64 nowMs, int64& outDueMs )
    {
        using Internal = LoginStoreLogicInternal;
        outDueMs       = 0;
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.RequestDeletion", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            const uint64 profileVersion = profile._version;
            if ( profile._deletionDueMs == 0 )
                profile._deletionDueMs = nowMs + _settings._deletionGraceMs;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( identity._accountID ), profile, profileVersion );
            transaction.put( Internal::getDeletionTable(), Internal::makeDeletionKey( profile._deletionDueMs, identity._accountID ) );
            uint64                   sessionID = 0;
            const LoginServiceResult staged    = stageSessionRevoke( identity._accountID, LoginRevokeReason::AccountDeleted, nowMs, transaction, sessionID );
            if ( staged != LoginResult::Ok )
                return staged;
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountID, "account.delete.request", nowMs );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountID, static_cast<uint64>( profile._deletionDueMs ) );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( sessionID != 0 )
                _result._listRevoked.push_back( LoginSessionRef{ identity._accountID, sessionID, LoginRevokeReason::AccountDeleted } );
            outDueMs = profile._deletionDueMs;
            return LoginResult::Ok;
        } );
    }

    LoginServiceResult LoginStoreLogic::cancelDeletion( const LoginSessionToken& token, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity          identity;
        const LoginServiceResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        return ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.CancelDeletion", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountID, profile ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            if ( profile._deletionDueMs == 0 )
                return LoginResult::Ok;
            const uint64 profileVersion = profile._version;
            const int64  dueMs          = profile._deletionDueMs;
            profile._deletionDueMs      = 0;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountIDTable(), ServiceKeyUtil::makeHex64( identity._accountID ), profile, profileVersion );
            transaction.erase( Internal::getDeletionTable(), Internal::makeDeletionKey( dueMs, identity._accountID ) );
            ServiceAuditLog::stageEntry( transaction, Internal::makeAuditEntry( identity._accountID, "account.delete.cancel", nowMs ), identity._accountID, profileVersion );
            const ServiceStoreResult commitResult = attempt.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                return nullopt;
            return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginServiceResult::fail( OnlineError::kUnavailable );
        } );
    }

    int32 LoginStoreLogic::purgeDueDeletions( int64 nowMs, int32 maxCount, const vector<const IAfterDeleteAccount*>& listAfterDeleteAccount )
    {
        using Internal = LoginStoreLogicInternal;
        if ( maxCount <= 0 )
            return 0;
        vector<ServiceRecord> listDue;
        if ( ServiceRecordReader::readPage( _connection, Internal::getDeletionTable(), "", "", maxCount, false, listDue )._result != ServiceStoreResult::Ok )
            return 0;
        int32 purgedCount = 0;
        for ( const ServiceRecord& due : listDue )
        {
            uint64       dueMs     = 0;
            uint64       accountID = 0;
            const size_t slash     = due._key.find( '/' );
            const bool   bParsed   = slash != string::npos && ServiceKeyUtil::parseHex64( string_view( due._key ).substr( 0, slash ), dueMs ) &&
                                 ServiceKeyUtil::parseHex64( string_view( due._key ).substr( slash + 1 ), accountID );
            if ( bParsed == false )
            {
                SW_LOG_WARNING_ID( "LoginStoreLogic.AccountDeletionKeyMalformed", "account deletion key '%#' is malformed", due._key.c_str() );
                continue;
            }
            if ( static_cast<int64>( dueMs ) > nowMs )
                break; // 키 순서가 예약 시각 순서이므로 뒤의 항목은 모두 아직 지울 때가 아니다
            const LoginServiceResult purged = purgeAccount( accountID, due._key, nowMs );
            if ( purged.hasError( OnlineError::kUnavailable ) )
                break;
            purgedCount += purged == LoginResult::Ok ? 1 : 0;
        }
        runAfterDeleteAccount( nowMs, maxCount, listAfterDeleteAccount );
        // 지운 계정의 잔액을 소각한다. 이번 회차에 지운 계정과 지난 회차에 다 태우지 못한 계정을 함께 본다
        int32 burnedCount = 0;
        (void)Ledger::burnDeletedAccounts( _connection, nowMs, maxCount, burnedCount ); // 저장소 문제면 소각 대기에 남아 다음 회차에 다시 본다
        return purgedCount;
    }

    void LoginStoreLogic::runAfterDeleteAccount( int64 nowMs, int32 maxCount, const vector<const IAfterDeleteAccount*>& listAfterDeleteAccount )
    {
        using Internal = LoginStoreLogicInternal;
        vector<ServiceRecord> listQueued;
        if ( ServiceRecordReader::readPage( _connection, Internal::getAfterDeleteTable(), "", "", maxCount, false, listQueued )._result != ServiceStoreResult::Ok )
            return;
        for ( const ServiceRecord& queued : listQueued )
        {
            uint64 accountID = 0;
            if ( ServiceKeyUtil::parseHex64( queued._key, accountID ) == false || accountID == 0 )
            {
                SW_LOG_WARNING_ID( "LoginStoreLogic.AfterDeleteKeyMalformed", "account after-delete key '%#' is malformed", queued._key.c_str() );
                continue;
            }
            bool bDone = true;
            for ( const IAfterDeleteAccount* pHook : listAfterDeleteAccount )
            {
                const ServiceStoreResult result = pHook->afterDeleteAccount( _connection, accountID, nowMs );
                if ( result == ServiceStoreResult::Unavailable )
                    return; // 저장소가 아프다 — 다음 회차에 처음부터 다시 본다(훅은 멱등)
                bDone = bDone && result == ServiceStoreResult::Ok;
            }
            if ( bDone == false )
                continue;
            ServiceTransaction done;
            done.erase( Internal::getAfterDeleteTable(), queued._key, queued._version );
            if ( ServiceCommitRunner::commitOnce( _connection, done ) == ServiceStoreResult::Unavailable )
                return;
        }
    }

    LoginServiceResult LoginStoreLogic::purgeAccount( uint64 accountID, string_view deletionKey, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        Internal::ProfileRecord  profile;
        const ServiceStoreResult profileRead = Internal::readProfile( _connection, accountID, profile );
        const bool               bStale      = profileRead == ServiceStoreResult::NotFound || ( profileRead == ServiceStoreResult::Ok && profile._deletionDueMs == 0 );
        if ( bStale )
        {
            // 취소했거나 이미 지운 계정의 낡은 인덱스이므로 인덱스만 지운다.
            ServiceTransaction cleanup;
            cleanup.erase( Internal::getDeletionTable(), deletionKey );
            return ServiceCommitRunner::commitOnce( _connection, cleanup ) == ServiceStoreResult::Unavailable ? LoginServiceResult::fail( OnlineError::kUnavailable ) : LoginResult::NotLinked;
        }
        if ( profileRead != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        vector<ServiceRecord> listLink;
        if ( Internal::listAccountLinks( _connection, accountID, listLink ) != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );

        const string       accountKey = ServiceKeyUtil::makeHex64( accountID );
        ServiceTransaction transaction;
        transaction.erase( Internal::getAccountIDTable(), accountKey, profile._version );
        transaction.erase( Internal::getDeletionTable(), deletionKey );
        if ( profile._nameKey.empty() == false )
            transaction.erase( Internal::getAccountTable(), profile._nameKey );
        if ( profile._guestDigestHex.empty() == false )
            transaction.erase( Internal::getGuestTable(), profile._guestDigestHex );
        const size_t prefixSize = accountKey.size() + 1;
        for ( const ServiceRecord& link : listLink )
        {
            const string provider = link._key.substr( prefixSize );
            string       digestHex;
            if ( Internal::decodeSubjectDigest( link, digestHex ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable ); // 외부 연결을 남긴 채 계정을 지우지 않는다
            transaction.erase( Internal::getAccountExternalTable(), link._key );
            transaction.erase( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ) );
            // 연동 비밀(애플의 refresh token)은 계정과 함께 사라지기 전에 철회 대기열로 옮긴다 — 철회는 서비스가 제공자에 알린다.
            if ( Internal::stageSecretToRevokeQueue( _connection, transaction, accountID, provider ) != ServiceStoreResult::Ok )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
        }
        ServiceRecord            linkRaw;
        const ServiceStoreResult linkRead = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( linkRead == ServiceStoreResult::Ok )
        {
            uint64 sessionID = 0;
            if ( Internal::getAccountSessionTable().decode( accountKey, linkRaw, sessionID ) == false )
                return LoginServiceResult::fail( OnlineError::kUnavailable ); // 세션을 놓친 채 계정을 지우지 않는다
            transaction.erase( Internal::getAccountSessionTable(), accountKey );
            transaction.erase( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( sessionID ) );
        }
        // 원장 묘비와 소각 대기를 계정 지우기와 한 트랜잭션에 넣는다 — 탈퇴가 그사이 취소돼 이 커밋이 지면 잔액도 그대로다(Nakama DeleteAccount 의 지갑 · user_tombstone)
        Ledger::stageAccountDeletion( accountID, nowMs, transaction );
        transaction.put( Internal::getAfterDeleteTable(), accountKey ); // 키트 훅은 커밋 뒤 대기열로 돈다(`runAfterDeleteAccount`)
        ServiceAuditEntry audit = Internal::makeAuditEntry( accountID, "account.delete", nowMs );
        audit._actor            = ServiceAuditLog::kSystemActor;
        ServiceAuditLog::stageEntry( transaction, audit, accountID, static_cast<uint64>( profile._deletionDueMs ) );
        const ServiceStoreResult commitResult = ServiceCommitRunner::commitOnce( _connection, transaction );
        if ( commitResult == ServiceStoreResult::Conflict )
            return LoginResult::NotLinked; // 그사이 취소했거나 로그인했으므로 다음 정리에서 다시 본다
        return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginServiceResult::fail( OnlineError::kUnavailable );
    }

    void LoginStoreLogic::refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        for ( const LoginSessionRef& online : listOnline )
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( online._sessionID ), sessionRaw );
            if ( readResult == ServiceStoreResult::Unavailable )
                return; // 저장소에 문제가 있으면 아무도 끊지 않는다
            Internal::SessionRecord session;
            const bool              bReadable = readResult == ServiceStoreResult::Ok && Internal::decodeSession( sessionRaw, session );
            if ( bReadable && Internal::evaluateSession( session, nowMs ) == LoginResult::Ok )
                continue;
            const LoginRevokeReason reason = bReadable ? session._revokeReason : LoginRevokeReason::Administrative;
            _result._listRevoked.push_back( LoginSessionRef{ online._accountID, online._sessionID, reason } );
        }
    }

    ServiceStoreResult LoginStoreLogic::listRevocations( IServiceStoreConnection& connection, int32 maxCount, vector<LoginRevocationEntry>& outListEntry )
    {
        using Internal = LoginStoreLogicInternal;
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult listResult = ServiceRecordReader::readPage( connection, Internal::getRevokeTable(), "", "", maxCount, false, listRecord )._result;
        if ( listResult != ServiceStoreResult::Ok )
            return listResult;
        for ( ServiceRecord& record : listRecord )
        {
            const size_t slash     = record._key.find( '/' );
            uint64       accountID = 0;
            if ( slash == string::npos || ServiceKeyUtil::parseHex64( string_view( record._key ).substr( slash + 1 ), accountID ) == false )
            {
                SW_LOG_WARNING_ID( "LoginStoreLogic.LoginRevokeQueueKey", "login revoke queue key '%#' is malformed", record._key.c_str() );
                continue;
            }
            LoginRevocationEntry& entry = outListEntry.emplace_back();
            entry._key                  = record._key;
            entry._provider             = record._key.substr( 0, slash );
            entry._accountID            = accountID;
            entry._sealedSecret         = ServiceOpaqueTable::takeBytes( record );
        }
        return ServiceStoreResult::Ok;
    }

    ServiceTTLRule LoginStoreLogic::makeSessionTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = LoginStoreLogicInternal::getSessionTable();
        rule._ttlIndexTable   = LoginStoreLogicInternal::getSessionExpiryTable();
        rule._expireAfterMs   = expireAfterMs;
        rule._pfnReadTime     = &LoginStoreLogicInternal::readSessionExpiryMs; // 지우기 전에 만료를 다시 본다 - 늘린 세션은 남긴다
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&LoginStoreLogicInternal::readSessionExpiryMs>();
        rule._backfillVersion = 0;
        return rule;
    }

    ServiceStoreResult LoginStoreLogic::eraseRevocation( IServiceStoreConnection& connection, string_view key )
    {
        ServiceTransaction transaction;
        transaction.erase( LoginStoreLogicInternal::getRevokeTable(), key );
        return ServiceCommitRunner::commitOnce( connection, transaction );
    }

    ServiceStoreResult LoginStoreLogic::readIdentity( IServiceStoreConnection& connection, uint64 accountID, AccountIdentity& outIdentity )
    {
        LoginStoreLogicInternal::ProfileRecord profile;
        const ServiceStoreResult               readResult = LoginStoreLogicInternal::readProfile( connection, accountID, profile );
        if ( readResult != ServiceStoreResult::Ok )
            return readResult;
        outIdentity = LoginStoreLogicInternal::makeIdentity( accountID, profile );
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult LoginStoreLogic::readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity )
    {
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( displayName, nameKey ) == false )
            return ServiceStoreResult::NotFound;
        ServiceRecord            accountRaw;
        const ServiceStoreResult readResult = connection.readRecord( Internal::getAccountTable(), nameKey, accountRaw );
        if ( readResult != ServiceStoreResult::Ok )
            return readResult;
        Internal::AccountRecord account;
        if ( Internal::decodeAccount( accountRaw, account ) == false )
            return ServiceStoreResult::Unavailable;
        return readIdentity( connection, account._accountID, outIdentity );
    }

    LoginServiceResult LoginStoreLogic::recordFailure( string_view nameKey, const LoginAccountRecord& readAccount, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs )
    {
        using Internal                  = LoginStoreLogicInternal;
        Internal::AccountRecord current = readAccount;
        uint64                  version = accountVersion;
        // 틀린 시도가 동시에 겹치면(워커 여럿 · 서버 여럿) 조건부 쓰기가 Conflict 다 — 다시 읽어 센다. 버리면 겹친 만큼 덜 세어 잠금을 비켜 간다(감사 O-14).
        // 주소마다의 시도 버킷은 프로세스 메모리라 서버 여럿에서는 이 계정 단위 셈이 유일한 막이다.
        const LoginServiceResult counted = ServiceCommitRunner::run<LoginServiceResult>( _connection, "Account.RecordFailure", [&]( ServiceCommitAttempt& attempt ) -> optional<LoginServiceResult>
        {
            Internal::AccountRecord account = current;
            if ( account._lockedUntilMs > nowMs )
            {
                outRetryAfterMs = account._lockedUntilMs - nowMs; // 겹친 시도가 먼저 잠갔다
                return LoginResult::AccountLocked;
            }
            ++account._failedCount;
            LoginServiceResult result = LoginResult::WrongCredentials;
            if ( account._failedCount >= _settings._maxFailedCount )
            {
                account._failedCount   = 0;
                account._lockedUntilMs = nowMs + _settings._lockoutMs;
                outRetryAfterMs        = _settings._lockoutMs;
                result                 = LoginResult::AccountLocked;
            }
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountTable(), nameKey, account, version );
            const ServiceStoreResult committed = attempt.commit( transaction );
            if ( committed != ServiceStoreResult::Conflict )
                return result; // 저장소 장애면 세지 못했어도 같은 답이다 — 있는 계정만 다른 답을 내면 계정 열거가 된다
            const ServiceStoreResult reread = Internal::getAccountTable().read( _connection, nameKey, current, &version );
            if ( reread != ServiceStoreResult::Ok )
                return LoginResult::WrongCredentials; // 지워졌거나 읽지 못했다 — 없는 계정과 같은 답이다
            return nullopt;
        } );
        if ( counted.hasError( OnlineError::kBusy ) )
            return LoginResult::WrongCredentials; // 겹침이 상한을 넘었다. 다른 답(kBusy)은 계정이 있다는 신호라 쓰지 않는다
        return counted;
    }

    LoginServiceResult LoginStoreLogic::evaluateSanction( uint64 accountID, int64 nowMs, LoginGrant& outGrant )
    {
        ServiceSanctionState state;
        if ( ServiceSanction::readState( _connection, accountID, state ) != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        const int64 blockedUntilMs = ServiceSanction::getLoginBlockedUntilMs( state, nowMs );
        if ( blockedUntilMs == 0 )
            return LoginResult::Ok;
        outGrant._sanctionUntilMs    = blockedUntilMs;
        outGrant._sanctionReasonCode = state._reasonCode;
        return LoginResult::AccountSuspended;
    }

    LoginServiceResult LoginStoreLogic::stageSessionOpen( uint64 accountID, int64 nowMs, ServiceTransaction& inoutTransaction, LoginSessionToken& outToken, int64& outExpiresAtMs,
                                                          uint64& outReplacedSessionID )
    {
        using Internal                = LoginStoreLogicInternal;
        outReplacedSessionID          = 0;
        const string       accountKey = ServiceKeyUtil::makeHex64( accountID );
        ServiceRecord      linkRaw;
        ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        uint64 oldSessionID = 0;
        if ( readResult == ServiceStoreResult::Ok && Internal::getAccountSessionTable().decode( accountKey, linkRaw, oldSessionID ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable ); // 깨진 연결을 없음으로 보면 앞 세션이 산 채로 새 세션이 열린다
        if ( oldSessionID != 0 )
        {
            ServiceRecord oldSessionRaw;
            const string  oldSessionKey = ServiceKeyUtil::makeHex64( oldSessionID );
            readResult                  = _connection.readRecord( Internal::getSessionTable(), oldSessionKey, oldSessionRaw );
            if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                return LoginServiceResult::fail( OnlineError::kUnavailable );
            Internal::SessionRecord oldSession;
            const bool              bOldReadable = readResult == ServiceStoreResult::Ok && Internal::decodeSession( oldSessionRaw, oldSession );
            const bool              bOldLive     = bOldReadable && Internal::evaluateSession( oldSession, nowMs ) == LoginResult::Ok;
            const bool              bReject      = bOldLive && _settings._duplicatePolicy == LoginDuplicatePolicy::RejectNew && oldSession._resumeDeadlineMs == 0;
            if ( bReject )
                return LoginResult::AlreadyLoggedIn;
            if ( bOldLive )
            {
                oldSession._revokeReason = LoginRevokeReason::DuplicateLogin;
                oldSession._expiresAtMs  = MathUtil::min( oldSession._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
                Internal::stageSession( inoutTransaction, oldSessionKey, oldSession, oldSessionRaw._version );
                outReplacedSessionID = oldSessionID;
            }
        }

        const bool bIDMade = Internal::makeNonZeroID( _crypto, outToken._sessionID );
        if ( bIDMade == false || _crypto.fillRandom( outToken._secret.reveal(), LoginConstant::kTokenSecretSize ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        Internal::SessionRecord session;
        session._accountID   = accountID;
        session._issuedAtMs  = nowMs;
        session._expiresAtMs = nowMs + _settings._sessionLifetimeMs;
        if ( Internal::computeTokenDigest( _crypto, outToken._secret.reveal(), session._tokenDigest.reveal() ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        Internal::stageSession( inoutTransaction, ServiceKeyUtil::makeHex64( outToken._sessionID ), session, ServiceRecord::kAbsentVersion );
        inoutTransaction.put( Internal::getAccountSessionTable(), accountKey, outToken._sessionID, linkRaw._version );
        outExpiresAtMs = session._expiresAtMs;
        return LoginResult::Ok;
    }

    void LoginStoreLogic::recordSessionOpened( uint64 accountID, const LoginSessionToken& token, uint64 replacedSessionID )
    {
        if ( replacedSessionID != 0 )
            _result._listRevoked.push_back( LoginSessionRef{ accountID, replacedSessionID, LoginRevokeReason::DuplicateLogin } );
        _result._listOnline.push_back( LoginSessionRef{ accountID, token._sessionID, LoginRevokeReason::None } );
        _result._listEvent.push_back( LoginEvent{ accountID, token._sessionID, LoginRevokeReason::None, LoginEvent::Kind::LoggedIn } );
    }

    LoginServiceResult LoginStoreLogic::stageSessionRevoke( uint64 accountID, LoginRevokeReason reason, int64 nowMs, ServiceTransaction& inoutTransaction, uint64& outSessionID )
    {
        using Internal                = LoginStoreLogicInternal;
        outSessionID                  = 0;
        const string       accountKey = ServiceKeyUtil::makeHex64( accountID );
        ServiceRecord      linkRaw;
        ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::Ok;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        uint64 sessionID = 0;
        if ( Internal::getAccountSessionTable().decode( accountKey, linkRaw, sessionID ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable ); // 없는 세션을 지운 셈 치면 정지 · 강제 종료가 산 세션을 남긴다
        const string  sessionKey = ServiceKeyUtil::makeHex64( sessionID );
        ServiceRecord sessionRaw;
        readResult = _connection.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
        {
            inoutTransaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
            return LoginResult::Ok;
        }
        if ( readResult != ServiceStoreResult::Ok )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        Internal::SessionRecord session;
        if ( Internal::decodeSession( sessionRaw, session ) == false )
            return LoginServiceResult::fail( OnlineError::kUnavailable );
        session._revokeReason = reason;
        session._expiresAtMs  = MathUtil::min( session._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
        Internal::stageSession( inoutTransaction, sessionKey, session, sessionRaw._version );
        inoutTransaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
        outSessionID = sessionID;
        return LoginResult::Ok;
    }
} // namespace sw
