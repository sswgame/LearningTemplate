#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/LoginStoreLogic.h"

#include "Core/Network/BitStream.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
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
            static constexpr uint8 kRecordFormat       = 1;
            static constexpr int32 kStoreRetryLimit    = 4; ///< 조건부 쓰기가 다른 쓰기와 부딪혔을 때 다시 읽는 수
            static constexpr int32 kMaxLinkCount       = 16;
            static constexpr int32 kDigestHexSize      = LoginConstant::kDigestSize * 2;
            static constexpr utf8  kDigestLabel[]      = "sw-login-token-digest-v1";
            static constexpr utf8  kGuestLabel[]       = "sw-guest-v1";
            static constexpr utf8  kSubjectLabel[]     = "sw-platform-subject-v1:";
            static constexpr utf8  kHexDigit[]         = "0123456789abcdef";
            static constexpr utf8  kGuestNamePrefix[]  = "Guest-";
            static constexpr utf8  kPlayerNamePrefix[] = "Player";
            static constexpr int32 kMaxHintNameSize    = 12;

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

            static const hashed_string& getGuestTable()
            {
                static const hashed_string s_table{ "account_guest" };
                return s_table;
            }

            static const hashed_string& getExternalTable()
            {
                static const hashed_string s_table{ "login_external" };
                return s_table;
            }

            static const hashed_string& getAccountExternalTable()
            {
                static const hashed_string s_table{ "login_account_external" };
                return s_table;
            }

            static const hashed_string& getDeletionTable()
            {
                static const hashed_string s_table{ "account_deletion" };
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

            /** @brief 계정 프로필 — 모든 계정(이름 · 게스트 · 외부)에 하나. */
            struct ProfileRecord
            {
                string _nameKey{}; ///< 이름 · 비밀번호가 있으면 소문자 이름 키, 아니면 빈 글
                string _displayName{};
                string _guestDigestHex{}; ///< 게스트 장치 다이제스트(지울 때 그 레코드를 찾는다), 없으면 빈 글
                int64  _deletionDueMs{ 0 };
                uint64 _version{ 0 };
                uint8  _bGuest{ SW_FALSE };
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

            static vector<uint8> encodeProfile( const ProfileRecord& record )
            {
                BitWriter writer;
                writer.writeBits( kRecordFormat, 8 );
                ServiceKeyUtil::writeString( writer, record._nameKey );
                ServiceKeyUtil::writeString( writer, record._displayName );
                ServiceKeyUtil::writeString( writer, record._guestDigestHex );
                writer.writeVarInt( record._deletionDueMs );
                writer.writeBool( record._bGuest == SW_TRUE );
                return writer.releaseBytes();
            }

            [[nodiscard]] static bool decodeProfile( const vector<uint8>& bytes, uint64 version, ProfileRecord& outRecord )
            {
                BitReader reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                if ( reader.readBits( 8 ) != kRecordFormat )
                    return false;
                ProfileRecord record;
                const bool    bTextOk = ServiceKeyUtil::readString( reader, LoginConstant::kMaxLoginNameSize, record._nameKey ) &&
                                     ServiceKeyUtil::readString( reader, LoginConstant::kMaxDisplayNameSize, record._displayName ) &&
                                     ServiceKeyUtil::readString( reader, kDigestHexSize, record._guestDigestHex );
                if ( bTextOk == false )
                    return false;
                record._deletionDueMs = reader.readVarInt();
                record._bGuest        = reader.readBool() ? SW_TRUE : SW_FALSE;
                record._version       = version;
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
                if ( reader.hasOverflowed() || revokeReason > static_cast<uint32>( LoginRevokeReason::AccountDeleted ) )
                    return false;
                record._revokeReason = static_cast<LoginRevokeReason>( revokeReason );
                outRecord            = record;
                return true;
            }

            static vector<uint8> encodeId( uint64 id )
            {
                BitWriter writer;
                writer.writeVarUint( id );
                return writer.releaseBytes();
            }

            static uint64 decodeId( const vector<uint8>& bytes )
            {
                BitReader    reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                const uint64 id = reader.readVarUint();
                return reader.hasOverflowed() ? 0 : id;
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

            static bool isValidPassword( string_view password )
            {
                const int32 size = static_cast<int32>( password.size() );
                return LoginConstant::kMinPasswordSize <= size && size <= LoginConstant::kMaxPasswordSize;
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

            static string makeHex( const uint8* pBytes, int32 size )
            {
                string hex;
                hex.reserve( static_cast<size_t>( size ) * 2 );
                for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
                {
                    hex.push_back( kHexDigit[pBytes[byteIndex] >> 4] );
                    hex.push_back( kHexDigit[pBytes[byteIndex] & 0xFu] );
                }
                return hex;
            }

            [[nodiscard]] static bool computeGuestDigestHex( ILoginCrypto& crypto, const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], string& outHex )
            {
                uint8 arrDigest[LoginConstant::kDigestSize];
                if ( crypto.computeKeyedHash( arrDeviceSecret, LoginConstant::kDeviceSecretSize, reinterpret_cast<const uint8*>( kGuestLabel ),
                                              static_cast<int32>( sizeof( kGuestLabel ) - 1 ), arrDigest, LoginConstant::kDigestSize ) == false )
                    return false;
                outHex = makeHex( arrDigest, LoginConstant::kDigestSize );
                return true;
            }

            /** @brief 외부 주체 다이제스트 — 키 = 주체 글, 정보 = 라벨 ‖ 제공자 이름(제공자가 다르면 같은 주체 글도 다른 다이제스트). */
            [[nodiscard]] static bool computeSubjectDigestHex( ILoginCrypto& crypto, string_view provider, string_view subject, string& outHex )
            {
                string info{ kSubjectLabel };
                info += provider;
                uint8 arrDigest[LoginConstant::kDigestSize];
                if ( crypto.computeKeyedHash( reinterpret_cast<const uint8*>( subject.data() ), static_cast<int32>( subject.size() ), reinterpret_cast<const uint8*>( info.data() ),
                                              static_cast<int32>( info.size() ), arrDigest, LoginConstant::kDigestSize ) == false )
                    return false;
                outHex = makeHex( arrDigest, LoginConstant::kDigestSize );
                return true;
            }

            static string makeExternalKey( string_view provider, string_view digestHex )
            {
                string key{ provider };
                key.push_back( '/' );
                key += digestHex;
                return key;
            }

            static string makeAccountExternalKey( uint64 accountId, string_view provider )
            {
                string key = ServiceKeyUtil::makeHex64( accountId );
                key.push_back( '/' );
                key += provider;
                return key;
            }

            static string makeDeletionKey( int64 dueMs, uint64 accountId )
            {
                string key = ServiceKeyUtil::makeHex64( static_cast<uint64>( dueMs < 0 ? 0 : dueMs ) );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, accountId );
                return key;
            }

            /** @brief 표시 이름 끝에 붙이는 계정 id 꼬리 — 16 진 뒤 @p digitCount 자리입니다. */
            static string makeIdSuffix( uint64 accountId, int32 digitCount )
            {
                const string hex = ServiceKeyUtil::makeHex64( accountId );
                return hex.substr( hex.size() - static_cast<size_t>( digitCount ) );
            }

            static string makeGuestDisplayName( uint64 accountId ) { return string( kGuestNamePrefix ) + makeIdSuffix( accountId, 6 ); }

            /** @brief 제공자가 준 이름의 ASCII 영숫자 · _ 만 12 자까지 + "-" + 계정 id 꼬리 4 자리입니다(없으면 "Player"). */
            static string makePlatformDisplayName( string_view hint, uint64 accountId )
            {
                string name;
                for ( const utf8 ch : hint )
                {
                    const bool bLetter = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' );
                    const bool bDigit  = '0' <= ch && ch <= '9';
                    if ( bLetter || bDigit || ch == '_' )
                        name.push_back( ch );
                    if ( static_cast<int32>( name.size() ) >= kMaxHintNameSize )
                        break;
                }
                if ( name.empty() )
                    name = kPlayerNamePrefix;
                name.push_back( '-' );
                name += makeIdSuffix( accountId, 4 );
                return name;
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

            /** @brief 프로필을 읽습니다. 없으면 NotFound. */
            static ServiceStoreResult readProfile( IServiceStoreConnection& connection, uint64 accountId, ProfileRecord& outProfile )
            {
                ServiceRecord            raw;
                const ServiceStoreResult read = connection.readRecord( getAccountIdTable(), ServiceKeyUtil::makeHex64( accountId ), raw );
                if ( read != ServiceStoreResult::Ok )
                    return read;
                if ( decodeProfile( raw._bytes, raw._version, outProfile ) == false )
                {
                    SW_LOG_ERROR( "account profile %# is corrupt", ServiceKeyUtil::makeHex64( accountId ).c_str() );
                    return ServiceStoreResult::Unavailable;
                }
                return ServiceStoreResult::Ok;
            }

            /** @brief 계정의 외부 연결(제공자 → 주체 다이제스트)을 읽습니다. */
            static ServiceStoreResult listAccountLinks( IServiceStoreConnection& connection, uint64 accountId, vector<ServiceRecord>& outListLink )
            {
                string prefix = ServiceKeyUtil::makeHex64( accountId );
                prefix.push_back( '/' );
                return connection.listRecords( getAccountExternalTable(), prefix, "", kMaxLinkCount, false, outListLink );
            }

            static AccountIdentity makeIdentity( uint64 accountId, const ProfileRecord& profile )
            {
                AccountIdentity identity;
                identity._accountId   = accountId;
                identity._displayName = profile._displayName;
                identity._bGuest      = profile._bGuest;
                return identity;
            }

            static void fillGrantIdentity( uint64 accountId, const ProfileRecord& profile, const LoginSessionToken& token, int64 expiresAtMs, uint64 replacedSessionId,
                                           LoginGrant& outGrant )
            {
                outGrant._identity          = makeIdentity( accountId, profile );
                outGrant._token             = token;
                outGrant._expiresAtMs       = expiresAtMs;
                outGrant._replacedSessionId = replacedSessionId;
                outGrant._deletionDueMs     = profile._deletionDueMs;
            }

            static ServiceAuditEntry makeAuditEntry( uint64 accountId, const utf8* pAction, int64 nowMs )
            {
                ServiceAuditEntry entry;
                entry._actor   = "acct." + ServiceKeyUtil::makeHex64( accountId );
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
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;
        if ( Internal::isValidPassword( credential._password ) == false )
            return LoginResult::InvalidPassword;

        Internal::AccountRecord account;
        account._displayName = credential._loginName;
        account._params      = _settings._passwordHashParams;
        if ( _crypto.fillRandom( account._arrSalt, LoginConstant::kSaltSize ) == false )
            return LoginResult::StoreUnavailable;
        if ( _crypto.computePasswordHash( credential._password, account._arrSalt, LoginConstant::kSaltSize, account._params, account._arrPasswordHash,
                                          LoginConstant::kPasswordHashSize ) == false )
            return LoginResult::StoreUnavailable;

        Internal::ProfileRecord profile;
        profile._nameKey     = nameKey;
        profile._displayName = credential._loginName;
        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            if ( Internal::makeNonZeroId( _crypto, account._accountId ) == false )
                return LoginResult::StoreUnavailable;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountTable(), nameKey, Internal::encodeAccount( account ), ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( account._accountId ), Internal::encodeProfile( profile ), ServiceRecord::kAbsentVersion );
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
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
        {
            burnPasswordHash( credential._password );
            return LoginResult::WrongCredentials;
        }

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord            accountRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountTable(), nameKey, accountRaw );
            if ( readResult == ServiceStoreResult::NotFound )
            {
                burnPasswordHash( credential._password );
                return LoginResult::WrongCredentials;
            }
            if ( readResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            Internal::AccountRecord account;
            if ( Internal::decodeAccount( accountRaw._bytes, account ) == false )
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
            if ( Internal::isEqualConstantTime( arrHash, account._arrPasswordHash, LoginConstant::kPasswordHashSize ) == false )
                return recordFailure( nameKey, accountRaw._bytes, accountRaw._version, nowMs, outGrant._retryAfterMs );

            const LoginResult sanction = evaluateSanction( account._accountId, nowMs, outGrant );
            if ( sanction != LoginResult::Ok )
                return sanction;
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, account._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

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
                transaction.put( Internal::getAccountTable(), nameKey, Internal::encodeAccount( account ), accountRaw._version );
            }
            transaction.requireVersion( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( account._accountId ), profile._version );

            LoginSessionToken token;
            int64             expiresAtMs       = 0;
            uint64            replacedSessionId = 0;
            const LoginResult staged            = stageSessionOpen( account._accountId, nowMs, transaction, token, expiresAtMs, replacedSessionId );
            if ( staged != LoginResult::Ok )
                return staged;
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue; // 같은 계정이 그새 다른 곳에서 로그인했거나 실패 수가 바뀌었다 — 다시 읽는다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable; // 적용됐는지 모른다 — 클라이언트가 다시 로그인하면 그 세션을 밀어낸다

            recordSessionOpened( account._accountId, token, replacedSessionId );
            Internal::fillGrantIdentity( account._accountId, profile, token, expiresAtMs, replacedSessionId, outGrant );
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::guestLogin( const uint8 ( &arrDeviceSecret )[LoginConstant::kDeviceSecretSize], int64 nowMs, LoginGrant& outGrant )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        string digestHex;
        if ( Internal::computeGuestDigestHex( _crypto, arrDeviceSecret, digestHex ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord            guestRaw;
            const ServiceStoreResult guestRead = _connection.readRecord( Internal::getGuestTable(), digestHex, guestRaw );
            if ( guestRead != ServiceStoreResult::Ok && guestRead != ServiceStoreResult::NotFound )
                return LoginResult::StoreUnavailable;

            ServiceTransaction      transaction;
            Internal::ProfileRecord profile;
            uint64                  accountId = 0;
            const bool              bCreate   = guestRead == ServiceStoreResult::NotFound;
            if ( bCreate )
            {
                if ( Internal::makeNonZeroId( _crypto, accountId ) == false )
                    return LoginResult::StoreUnavailable;
                profile._displayName    = Internal::makeGuestDisplayName( accountId );
                profile._guestDigestHex = digestHex;
                profile._bGuest         = SW_TRUE;
                transaction.put( Internal::getGuestTable(), digestHex, Internal::encodeId( accountId ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( accountId ), Internal::encodeProfile( profile ), ServiceRecord::kAbsentVersion );
            }
            else
            {
                accountId = Internal::decodeId( guestRaw._bytes );
                if ( accountId == 0 || Internal::readProfile( _connection, accountId, profile ) != ServiceStoreResult::Ok )
                    return LoginResult::StoreUnavailable;
                const LoginResult sanction = evaluateSanction( accountId, nowMs, outGrant );
                if ( sanction != LoginResult::Ok )
                    return sanction;
                transaction.requireVersion( Internal::getGuestTable(), digestHex, guestRaw._version );
            }

            LoginSessionToken token;
            int64             expiresAtMs       = 0;
            uint64            replacedSessionId = 0;
            const LoginResult staged            = stageSessionOpen( accountId, nowMs, transaction, token, expiresAtMs, replacedSessionId );
            if ( staged != LoginResult::Ok )
                return staged;
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue; // 같은 장치가 그새 계정을 만들었거나 다른 곳에서 로그인했다 — 다시 읽는다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

            recordSessionOpened( accountId, token, replacedSessionId );
            Internal::fillGrantIdentity( accountId, profile, token, expiresAtMs, replacedSessionId, outGrant );
            outGrant._bCreated = bCreate ? SW_TRUE : SW_FALSE;
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::platformLogin( string_view provider, string_view subject, string_view displayNameHint, int64 nowMs, LoginGrant& outGrant )
    {
        using Internal = LoginStoreLogicInternal;
        outGrant       = LoginGrant{};
        if ( AccountUtil::isValidLowerToken( provider, LoginConstant::kMaxProviderNameSize ) == false || subject.empty() )
            return LoginResult::InvalidRequest;
        string digestHex;
        if ( Internal::computeSubjectDigestHex( _crypto, provider, subject, digestHex ) == false )
            return LoginResult::StoreUnavailable;
        const string externalKey = Internal::makeExternalKey( provider, digestHex );

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord            externalRaw;
            const ServiceStoreResult externalRead = _connection.readRecord( Internal::getExternalTable(), externalKey, externalRaw );
            if ( externalRead != ServiceStoreResult::Ok && externalRead != ServiceStoreResult::NotFound )
                return LoginResult::StoreUnavailable;

            ServiceTransaction      transaction;
            Internal::ProfileRecord profile;
            uint64                  accountId = 0;
            const bool              bCreate   = externalRead == ServiceStoreResult::NotFound;
            if ( bCreate )
            {
                if ( Internal::makeNonZeroId( _crypto, accountId ) == false )
                    return LoginResult::StoreUnavailable;
                profile._displayName = Internal::makePlatformDisplayName( displayNameHint, accountId );
                transaction.put( Internal::getExternalTable(), externalKey, Internal::encodeId( accountId ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountExternalTable(), Internal::makeAccountExternalKey( accountId, provider ), vector<uint8>( digestHex.begin(), digestHex.end() ),
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( accountId ), Internal::encodeProfile( profile ), ServiceRecord::kAbsentVersion );
            }
            else
            {
                accountId = Internal::decodeId( externalRaw._bytes );
                if ( accountId == 0 || Internal::readProfile( _connection, accountId, profile ) != ServiceStoreResult::Ok )
                    return LoginResult::StoreUnavailable;
                const LoginResult sanction = evaluateSanction( accountId, nowMs, outGrant );
                if ( sanction != LoginResult::Ok )
                    return sanction;
                transaction.requireVersion( Internal::getExternalTable(), externalKey, externalRaw._version );
            }

            LoginSessionToken token;
            int64             expiresAtMs       = 0;
            uint64            replacedSessionId = 0;
            const LoginResult staged            = stageSessionOpen( accountId, nowMs, transaction, token, expiresAtMs, replacedSessionId );
            if ( staged != LoginResult::Ok )
                return staged;
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

            recordSessionOpened( accountId, token, replacedSessionId );
            Internal::fillGrantIdentity( accountId, profile, token, expiresAtMs, replacedSessionId, outGrant );
            outGrant._bCreated = bCreate ? SW_TRUE : SW_FALSE;
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::resumeSession( const LoginSessionToken& token, int64 nowMs, LoginGrant& outGrant )
    {
        using Internal          = LoginStoreLogicInternal;
        outGrant                = LoginGrant{};
        const string sessionKey = ServiceKeyUtil::makeHex64( token._sessionId );
        uint8        arrDigest[LoginConstant::kDigestSize];
        if ( Internal::computeTokenDigest( _crypto, token._arrSecret, arrDigest ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw );
            if ( readResult == ServiceStoreResult::NotFound )
                return LoginResult::InvalidToken;
            if ( readResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            Internal::SessionRecord session;
            if ( Internal::decodeSession( sessionRaw._bytes, session ) == false )
                return LoginResult::StoreUnavailable;
            if ( Internal::isEqualConstantTime( arrDigest, session._arrTokenDigest, LoginConstant::kDigestSize ) == false )
                return LoginResult::InvalidToken;
            const LoginResult state = Internal::evaluateSession( session, nowMs );
            if ( state != LoginResult::Ok )
            {
                outGrant._revokeReason = session._revokeReason;
                return state;
            }
            const LoginResult sanction = evaluateSanction( session._accountId, nowMs, outGrant );
            if ( sanction != LoginResult::Ok )
                return sanction;
            // 신원은 비밀을 바꾸기 **전에** 읽는다 — 못 읽으면 빈 신원으로 Ok 를 주지 않고, 클라이언트가 든 토큰도 살려 둔다(validateSession 과 같은 결과).
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, session._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

            // 재접속마다 비밀을 돌려 바꾼다 — 옛 토큰은 이 커밋으로 죽는다.
            LoginSessionToken rotated;
            rotated._sessionId     = token._sessionId;
            const bool bSecretMade = _crypto.fillRandom( rotated._arrSecret, LoginConstant::kTokenSecretSize );
            if ( bSecretMade == false || Internal::computeTokenDigest( _crypto, rotated._arrSecret, session._arrTokenDigest ) == false )
                return LoginResult::StoreUnavailable;
            session._resumeDeadlineMs = 0;
            ++session._rotation;
            ServiceTransaction transaction;
            transaction.put( Internal::getSessionTable(), sessionKey, Internal::encodeSession( session ), sessionRaw._version );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue; // 같은 토큰으로 그새 돌아온 쪽이 있다 — 다시 읽으면 비밀이 달라 InvalidToken 이 된다
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;

            _outcome._listOnline.push_back( LoginSessionRef{ session._accountId, token._sessionId, LoginRevokeReason::None } );
            _outcome._listEvent.push_back( LoginEvent{ session._accountId, token._sessionId, LoginRevokeReason::None, LoginEvent::Kind::Resumed } );
            outGrant._identity      = Internal::makeIdentity( session._accountId, profile );
            outGrant._deletionDueMs = profile._deletionDueMs;
            outGrant._token         = rotated;
            outGrant._expiresAtMs   = session._expiresAtMs;
            return LoginResult::Ok;
        }
        return LoginResult::InvalidToken;
    }

    LoginResult LoginStoreLogic::validateSession( const LoginSessionToken& token, int64 nowMs, AccountIdentity& outIdentity, LoginRevokeReason* pOutRevokeReason )
    {
        using Internal = LoginStoreLogicInternal;
        ServiceRecord            sessionRaw;
        const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionId ), sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::InvalidToken;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        Internal::SessionRecord session;
        if ( Internal::decodeSession( sessionRaw._bytes, session ) == false )
            return LoginResult::StoreUnavailable;
        uint8 arrDigest[LoginConstant::kDigestSize];
        if ( Internal::computeTokenDigest( _crypto, token._arrSecret, arrDigest ) == false )
            return LoginResult::StoreUnavailable;
        if ( Internal::isEqualConstantTime( arrDigest, session._arrTokenDigest, LoginConstant::kDigestSize ) == false )
            return LoginResult::InvalidToken;
        const LoginResult state = Internal::evaluateSession( session, nowMs );
        if ( pOutRevokeReason != nullptr )
            *pOutRevokeReason = session._revokeReason;
        if ( state != LoginResult::Ok )
            return state;
        return readIdentity( _connection, session._accountId, outIdentity ) == ServiceStoreResult::Ok ? LoginResult::Ok : LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::logout( const LoginSessionToken& token, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        const string       accountKey = ServiceKeyUtil::makeHex64( identity._accountId );
        ServiceRecord      linkRaw;
        ServiceTransaction transaction;
        transaction.erase( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( token._sessionId ) );
        const ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult == ServiceStoreResult::Ok && Internal::decodeId( linkRaw._bytes ) == token._sessionId )
            transaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
        const ServiceStoreResult commitResult = _connection.commit( transaction );
        if ( commitResult != ServiceStoreResult::Ok && commitResult != ServiceStoreResult::Conflict )
            return LoginResult::StoreUnavailable;
        _outcome._listOffline.push_back( LoginSessionRef{ identity._accountId, token._sessionId, LoginRevokeReason::LoggedOut } );
        _outcome._listEvent.push_back( LoginEvent{ identity._accountId, token._sessionId, LoginRevokeReason::LoggedOut, LoginEvent::Kind::LoggedOut } );
        return LoginResult::Ok;
    }

    void LoginStoreLogic::markDisconnected( uint64 sessionId, int64 nowMs )
    {
        using Internal          = LoginStoreLogicInternal;
        const string sessionKey = ServiceKeyUtil::makeHex64( sessionId );
        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            ServiceRecord sessionRaw;
            if ( _connection.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw ) != ServiceStoreResult::Ok )
                return;
            Internal::SessionRecord session;
            if ( Internal::decodeSession( sessionRaw._bytes, session ) == false )
                return;
            if ( attempt == 0 )
                _outcome._listDisconnected.push_back( LoginSessionRef{ session._accountId, sessionId, LoginRevokeReason::None } );
            if ( session._revokeReason != LoginRevokeReason::None || session._resumeDeadlineMs != 0 )
                return;
            session._resumeDeadlineMs = nowMs + _settings._reconnectGraceMs;
            ServiceTransaction transaction;
            transaction.put( Internal::getSessionTable(), sessionKey, Internal::encodeSession( session ), sessionRaw._version );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                SW_LOG_WARNING( "session %# could not start its reconnect grace (store %#)", sessionKey.c_str(), static_cast<int32>( commitResult ) );
            return;
        }
    }

    LoginResult LoginStoreLogic::revokeAccountSessions( uint64 accountId, LoginRevokeReason reason, int64 nowMs )
    {
        for ( int32 attempt = 0; attempt < LoginStoreLogicInternal::kStoreRetryLimit; ++attempt )
        {
            ServiceTransaction transaction;
            uint64             sessionId = 0;
            const LoginResult  staged    = stageSessionRevoke( accountId, reason, nowMs, transaction, sessionId );
            if ( staged != LoginResult::Ok )
                return staged;
            if ( sessionId == 0 )
                return LoginResult::Ok;
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            _outcome._listRevoked.push_back( LoginSessionRef{ accountId, sessionId, reason } );
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
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

    LoginResult LoginStoreLogic::linkCredential( const LoginSessionToken& token, const LoginCredential& credential, int64 nowMs, AccountIdentity& outIdentity )
    {
        using Internal = LoginStoreLogicInternal;
        string nameKey;
        if ( Internal::normalizeLoginName( credential._loginName, nameKey ) == false )
            return LoginResult::InvalidName;
        if ( Internal::isValidPassword( credential._password ) == false )
            return LoginResult::InvalidPassword;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;

        Internal::AccountRecord account;
        account._accountId   = identity._accountId;
        account._displayName = credential._loginName;
        account._params      = _settings._passwordHashParams;
        if ( _crypto.fillRandom( account._arrSalt, LoginConstant::kSaltSize ) == false )
            return LoginResult::StoreUnavailable;
        if ( _crypto.computePasswordHash( credential._password, account._arrSalt, LoginConstant::kSaltSize, account._params, account._arrPasswordHash,
                                          LoginConstant::kPasswordHashSize ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            if ( profile._nameKey.empty() == false )
                return LoginResult::AlreadyLinked;
            const uint64 profileVersion = profile._version;
            profile._nameKey            = nameKey;
            profile._displayName        = credential._loginName;
            profile._bGuest             = SW_FALSE;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountTable(), nameKey, Internal::encodeAccount( account ), ServiceRecord::kAbsentVersion );
            if ( _settings._bKeepGuestDeviceAfterLink == SW_FALSE && profile._guestDigestHex.empty() == false )
            {
                transaction.erase( Internal::getGuestTable(), profile._guestDigestHex );
                profile._guestDigestHex.clear();
            }
            transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( identity._accountId ), Internal::encodeProfile( profile ), profileVersion );
            ServiceAuditLog::stageEntry( transaction, Internal::makeAuditEntry( identity._accountId, "account.link.credential", nowMs ), identity._accountId,
                                         profileVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult commitResult = _connection.commit( transaction, &info );
            if ( commitResult == ServiceStoreResult::Conflict && info._conflictIndex == 0 )
                return LoginResult::AlreadyLinked; // 이름이 다른 계정 것이다 — 합치지 않는다
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            outIdentity = Internal::makeIdentity( identity._accountId, profile );
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::linkPlatform( const LoginSessionToken& token, string_view provider, string_view subject, int64 nowMs, AccountIdentity& outIdentity )
    {
        using Internal = LoginStoreLogicInternal;
        if ( AccountUtil::isValidLowerToken( provider, LoginConstant::kMaxProviderNameSize ) == false || subject.empty() )
            return LoginResult::InvalidRequest;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        string digestHex;
        if ( Internal::computeSubjectDigestHex( _crypto, provider, subject, digestHex ) == false )
            return LoginResult::StoreUnavailable;

        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            const uint64 profileVersion = profile._version;
            profile._bGuest             = SW_FALSE;
            ServiceTransaction transaction;
            transaction.put( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ), Internal::encodeId( identity._accountId ),
                             ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountExternalTable(), Internal::makeAccountExternalKey( identity._accountId, provider ),
                             vector<uint8>( digestHex.begin(), digestHex.end() ), ServiceRecord::kAbsentVersion );
            transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( identity._accountId ), Internal::encodeProfile( profile ), profileVersion );
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountId, "account.link.platform", nowMs );
            audit._after            = string( provider );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountId, profileVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult commitResult = _connection.commit( transaction, &info );
            const bool               bTaken       = commitResult == ServiceStoreResult::Conflict && ( info._conflictIndex == 0 || info._conflictIndex == 1 );
            if ( bTaken )
                return LoginResult::AlreadyLinked; // 그 주체가 다른 계정 것이거나 이 계정에 그 제공자가 이미 있다
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            outIdentity = Internal::makeIdentity( identity._accountId, profile );
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::unlinkPlatform( const LoginSessionToken& token, string_view provider, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            vector<ServiceRecord> listLink;
            if ( Internal::listAccountLinks( _connection, identity._accountId, listLink ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            const string         linkKey = Internal::makeAccountExternalKey( identity._accountId, provider );
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
            const string       digestHex( reinterpret_cast<const utf8*>( pLink->_bytes.data() ), pLink->_bytes.size() );
            ServiceTransaction transaction;
            transaction.erase( Internal::getAccountExternalTable(), linkKey, pLink->_version );
            transaction.erase( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ) );
            transaction.requireVersion( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( identity._accountId ), profile._version );
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountId, "account.unlink.platform", nowMs );
            audit._before           = string( provider );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountId, pLink->_version );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginResult::StoreUnavailable;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::listLinks( const LoginSessionToken& token, int64 nowMs, AccountLinkSummary& outSummary )
    {
        using Internal = LoginStoreLogicInternal;
        outSummary     = AccountLinkSummary{};
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        Internal::ProfileRecord profile;
        if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        vector<ServiceRecord> listLink;
        if ( Internal::listAccountLinks( _connection, identity._accountId, listLink ) != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        const size_t prefixSize = ServiceKeyUtil::makeHex64( identity._accountId ).size() + 1;
        for ( const ServiceRecord& link : listLink )
            outSummary._listProvider.push_back( link._key.substr( prefixSize ) );
        outSummary._bHasCredential = profile._nameKey.empty() ? SW_FALSE : SW_TRUE;
        outSummary._bGuest         = profile._bGuest;
        outSummary._deletionDueMs  = profile._deletionDueMs;
        return LoginResult::Ok;
    }

    LoginResult LoginStoreLogic::requestDeletion( const LoginSessionToken& token, int64 nowMs, int64& outDueMs )
    {
        using Internal = LoginStoreLogicInternal;
        outDueMs       = 0;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            const uint64 profileVersion = profile._version;
            if ( profile._deletionDueMs == 0 )
                profile._deletionDueMs = nowMs + _settings._deletionGraceMs;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( identity._accountId ), Internal::encodeProfile( profile ), profileVersion );
            transaction.put( Internal::getDeletionTable(), Internal::makeDeletionKey( profile._deletionDueMs, identity._accountId ), vector<uint8>{} );
            uint64            sessionId = 0;
            const LoginResult staged    = stageSessionRevoke( identity._accountId, LoginRevokeReason::AccountDeleted, nowMs, transaction, sessionId );
            if ( staged != LoginResult::Ok )
                return staged;
            ServiceAuditEntry audit = Internal::makeAuditEntry( identity._accountId, "account.delete.request", nowMs );
            ServiceAuditLog::stageEntry( transaction, audit, identity._accountId, static_cast<uint64>( profile._deletionDueMs ) );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            if ( commitResult != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            if ( sessionId != 0 )
                _outcome._listRevoked.push_back( LoginSessionRef{ identity._accountId, sessionId, LoginRevokeReason::AccountDeleted } );
            outDueMs = profile._deletionDueMs;
            return LoginResult::Ok;
        }
        return LoginResult::StoreUnavailable;
    }

    LoginResult LoginStoreLogic::cancelDeletion( const LoginSessionToken& token, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        AccountIdentity   identity;
        const LoginResult state = validateSession( token, nowMs, identity );
        if ( state != LoginResult::Ok )
            return state;
        for ( int32 attempt = 0; attempt < Internal::kStoreRetryLimit; ++attempt )
        {
            Internal::ProfileRecord profile;
            if ( Internal::readProfile( _connection, identity._accountId, profile ) != ServiceStoreResult::Ok )
                return LoginResult::StoreUnavailable;
            if ( profile._deletionDueMs == 0 )
                return LoginResult::Ok;
            const uint64 profileVersion = profile._version;
            const int64  dueMs          = profile._deletionDueMs;
            profile._deletionDueMs      = 0;
            ServiceTransaction transaction;
            transaction.put( Internal::getAccountIdTable(), ServiceKeyUtil::makeHex64( identity._accountId ), Internal::encodeProfile( profile ), profileVersion );
            transaction.erase( Internal::getDeletionTable(), Internal::makeDeletionKey( dueMs, identity._accountId ) );
            ServiceAuditLog::stageEntry( transaction, Internal::makeAuditEntry( identity._accountId, "account.delete.cancel", nowMs ), identity._accountId, profileVersion );
            const ServiceStoreResult commitResult = _connection.commit( transaction );
            if ( commitResult == ServiceStoreResult::Conflict )
                continue;
            return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginResult::StoreUnavailable;
        }
        return LoginResult::StoreUnavailable;
    }

    int32 LoginStoreLogic::purgeDueDeletions( int64 nowMs, int32 maxCount )
    {
        using Internal = LoginStoreLogicInternal;
        if ( maxCount <= 0 )
            return 0;
        vector<ServiceRecord> listDue;
        if ( _connection.listRecords( Internal::getDeletionTable(), "", "", maxCount, false, listDue ) != ServiceStoreResult::Ok )
            return 0;
        int32 purgedCount = 0;
        for ( const ServiceRecord& due : listDue )
        {
            uint64       dueMs     = 0;
            uint64       accountId = 0;
            const size_t slash     = due._key.find( '/' );
            const bool   bParsed   = slash != string::npos && ServiceKeyUtil::parseHex64( string_view( due._key ).substr( 0, slash ), dueMs ) &&
                                 ServiceKeyUtil::parseHex64( string_view( due._key ).substr( slash + 1 ), accountId );
            if ( bParsed == false )
            {
                SW_LOG_WARNING( "account deletion key '%#' is malformed", due._key.c_str() );
                continue;
            }
            if ( static_cast<int64>( dueMs ) > nowMs )
                break; // 키 순서 = 예약 시각 순서 — 뒤는 모두 아직이다
            const LoginResult purged = purgeAccount( accountId, due._key, nowMs );
            if ( purged == LoginResult::StoreUnavailable )
                break;
            purgedCount += purged == LoginResult::Ok ? 1 : 0;
        }
        return purgedCount;
    }

    LoginResult LoginStoreLogic::purgeAccount( uint64 accountId, string_view deletionKey, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        Internal::ProfileRecord  profile;
        const ServiceStoreResult profileRead = Internal::readProfile( _connection, accountId, profile );
        const bool               bStale      = profileRead == ServiceStoreResult::NotFound || ( profileRead == ServiceStoreResult::Ok && profile._deletionDueMs == 0 );
        if ( bStale )
        {
            // 취소했거나 이미 지운 계정의 낡은 색인 — 색인만 지운다.
            ServiceTransaction cleanup;
            cleanup.erase( Internal::getDeletionTable(), deletionKey );
            return _connection.commit( cleanup ) == ServiceStoreResult::Unavailable ? LoginResult::StoreUnavailable : LoginResult::NotLinked;
        }
        if ( profileRead != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        vector<ServiceRecord> listLink;
        if ( Internal::listAccountLinks( _connection, accountId, listLink ) != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;

        const string       accountKey = ServiceKeyUtil::makeHex64( accountId );
        ServiceTransaction transaction;
        transaction.erase( Internal::getAccountIdTable(), accountKey, profile._version );
        transaction.erase( Internal::getDeletionTable(), deletionKey );
        if ( profile._nameKey.empty() == false )
            transaction.erase( Internal::getAccountTable(), profile._nameKey );
        if ( profile._guestDigestHex.empty() == false )
            transaction.erase( Internal::getGuestTable(), profile._guestDigestHex );
        const size_t prefixSize = accountKey.size() + 1;
        for ( const ServiceRecord& link : listLink )
        {
            const string provider = link._key.substr( prefixSize );
            const string digestHex( reinterpret_cast<const utf8*>( link._bytes.data() ), link._bytes.size() );
            transaction.erase( Internal::getAccountExternalTable(), link._key );
            transaction.erase( Internal::getExternalTable(), Internal::makeExternalKey( provider, digestHex ) );
        }
        ServiceRecord            linkRaw;
        const ServiceStoreResult linkRead = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( linkRead == ServiceStoreResult::Ok )
        {
            transaction.erase( Internal::getAccountSessionTable(), accountKey );
            transaction.erase( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( Internal::decodeId( linkRaw._bytes ) ) );
        }
        ServiceAuditEntry audit = Internal::makeAuditEntry( accountId, "account.delete", nowMs );
        audit._actor            = "system";
        ServiceAuditLog::stageEntry( transaction, audit, accountId, static_cast<uint64>( profile._deletionDueMs ) );
        const ServiceStoreResult commitResult = _connection.commit( transaction );
        if ( commitResult == ServiceStoreResult::Conflict )
            return LoginResult::NotLinked; // 그새 취소 · 로그인 — 다음 쓸기에서 다시 본다
        return commitResult == ServiceStoreResult::Ok ? LoginResult::Ok : LoginResult::StoreUnavailable;
    }

    void LoginStoreLogic::refreshSessions( const vector<LoginSessionRef>& listOnline, int64 nowMs )
    {
        using Internal = LoginStoreLogicInternal;
        for ( const LoginSessionRef& online : listOnline )
        {
            ServiceRecord            sessionRaw;
            const ServiceStoreResult readResult = _connection.readRecord( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( online._sessionId ), sessionRaw );
            if ( readResult == ServiceStoreResult::Unavailable )
                return; // 저장소가 아프면 아무도 끊지 않는다
            Internal::SessionRecord session;
            const bool              bReadable = readResult == ServiceStoreResult::Ok && Internal::decodeSession( sessionRaw._bytes, session );
            if ( bReadable && Internal::evaluateSession( session, nowMs ) == LoginResult::Ok )
                continue;
            const LoginRevokeReason reason = bReadable ? session._revokeReason : LoginRevokeReason::Administrative;
            _outcome._listRevoked.push_back( LoginSessionRef{ online._accountId, online._sessionId, reason } );
        }
    }

    ServiceStoreResult LoginStoreLogic::readIdentity( IServiceStoreConnection& connection, uint64 accountId, AccountIdentity& outIdentity )
    {
        LoginStoreLogicInternal::ProfileRecord profile;
        const ServiceStoreResult               readResult = LoginStoreLogicInternal::readProfile( connection, accountId, profile );
        if ( readResult != ServiceStoreResult::Ok )
            return readResult;
        outIdentity = LoginStoreLogicInternal::makeIdentity( accountId, profile );
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
        if ( Internal::decodeAccount( accountRaw._bytes, account ) == false )
        {
            SW_LOG_ERROR( "account record '%#' is corrupt", nameKey.c_str() );
            return ServiceStoreResult::Unavailable;
        }
        return readIdentity( connection, account._accountId, outIdentity );
    }

    LoginResult LoginStoreLogic::recordFailure( string_view nameKey, const vector<uint8>& accountBytes, uint64 accountVersion, int64 nowMs, int64& outRetryAfterMs )
    {
        using Internal = LoginStoreLogicInternal;
        Internal::AccountRecord account;
        if ( Internal::decodeAccount( accountBytes, account ) == false )
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
        transaction.put( Internal::getAccountTable(), nameKey, Internal::encodeAccount( account ), accountVersion );
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

    LoginResult LoginStoreLogic::evaluateSanction( uint64 accountId, int64 nowMs, LoginGrant& outGrant )
    {
        ServiceSanctionState state;
        if ( ServiceSanction::readState( _connection, accountId, state ) != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        const int64 blockedUntilMs = ServiceSanction::getLoginBlockedUntilMs( state, nowMs );
        if ( blockedUntilMs == 0 )
            return LoginResult::Ok;
        outGrant._sanctionUntilMs    = blockedUntilMs;
        outGrant._sanctionReasonCode = state._reasonCode;
        return LoginResult::AccountSuspended;
    }

    LoginResult LoginStoreLogic::stageSessionOpen( uint64 accountId, int64 nowMs, ServiceTransaction& inoutTransaction, LoginSessionToken& outToken, int64& outExpiresAtMs,
                                                   uint64& outReplacedSessionId )
    {
        using Internal                = LoginStoreLogicInternal;
        outReplacedSessionId          = 0;
        const string       accountKey = ServiceKeyUtil::makeHex64( accountId );
        ServiceRecord      linkRaw;
        ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
            return LoginResult::StoreUnavailable;
        const uint64 oldSessionId = readResult == ServiceStoreResult::Ok ? Internal::decodeId( linkRaw._bytes ) : 0;
        if ( oldSessionId != 0 )
        {
            ServiceRecord oldSessionRaw;
            const string  oldSessionKey = ServiceKeyUtil::makeHex64( oldSessionId );
            readResult                  = _connection.readRecord( Internal::getSessionTable(), oldSessionKey, oldSessionRaw );
            if ( readResult != ServiceStoreResult::Ok && readResult != ServiceStoreResult::NotFound )
                return LoginResult::StoreUnavailable;
            Internal::SessionRecord oldSession;
            const bool              bOldReadable = readResult == ServiceStoreResult::Ok && Internal::decodeSession( oldSessionRaw._bytes, oldSession );
            const bool              bOldLive     = bOldReadable && Internal::evaluateSession( oldSession, nowMs ) == LoginResult::Ok;
            const bool              bReject      = bOldLive && _settings._duplicatePolicy == LoginDuplicatePolicy::RejectNew && oldSession._resumeDeadlineMs == 0;
            if ( bReject )
                return LoginResult::AlreadyLoggedIn;
            if ( bOldLive )
            {
                oldSession._revokeReason = LoginRevokeReason::DuplicateLogin;
                oldSession._expiresAtMs  = std::min( oldSession._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
                inoutTransaction.put( Internal::getSessionTable(), oldSessionKey, Internal::encodeSession( oldSession ), oldSessionRaw._version );
                outReplacedSessionId = oldSessionId;
            }
        }

        const bool bIdMade = Internal::makeNonZeroId( _crypto, outToken._sessionId );
        if ( bIdMade == false || _crypto.fillRandom( outToken._arrSecret, LoginConstant::kTokenSecretSize ) == false )
            return LoginResult::StoreUnavailable;
        Internal::SessionRecord session;
        session._accountId   = accountId;
        session._issuedAtMs  = nowMs;
        session._expiresAtMs = nowMs + _settings._sessionLifetimeMs;
        if ( Internal::computeTokenDigest( _crypto, outToken._arrSecret, session._arrTokenDigest ) == false )
            return LoginResult::StoreUnavailable;
        inoutTransaction.put( Internal::getSessionTable(), ServiceKeyUtil::makeHex64( outToken._sessionId ), Internal::encodeSession( session ),
                              ServiceRecord::kAbsentVersion );
        inoutTransaction.put( Internal::getAccountSessionTable(), accountKey, Internal::encodeId( outToken._sessionId ), linkRaw._version );
        outExpiresAtMs = session._expiresAtMs;
        return LoginResult::Ok;
    }

    void LoginStoreLogic::recordSessionOpened( uint64 accountId, const LoginSessionToken& token, uint64 replacedSessionId )
    {
        if ( replacedSessionId != 0 )
            _outcome._listRevoked.push_back( LoginSessionRef{ accountId, replacedSessionId, LoginRevokeReason::DuplicateLogin } );
        _outcome._listOnline.push_back( LoginSessionRef{ accountId, token._sessionId, LoginRevokeReason::None } );
        _outcome._listEvent.push_back( LoginEvent{ accountId, token._sessionId, LoginRevokeReason::None, LoginEvent::Kind::LoggedIn } );
    }

    LoginResult LoginStoreLogic::stageSessionRevoke( uint64 accountId, LoginRevokeReason reason, int64 nowMs, ServiceTransaction& inoutTransaction, uint64& outSessionId )
    {
        using Internal                = LoginStoreLogicInternal;
        outSessionId                  = 0;
        const string       accountKey = ServiceKeyUtil::makeHex64( accountId );
        ServiceRecord      linkRaw;
        ServiceStoreResult readResult = _connection.readRecord( Internal::getAccountSessionTable(), accountKey, linkRaw );
        if ( readResult == ServiceStoreResult::NotFound )
            return LoginResult::Ok;
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        const uint64  sessionId  = Internal::decodeId( linkRaw._bytes );
        const string  sessionKey = ServiceKeyUtil::makeHex64( sessionId );
        ServiceRecord sessionRaw;
        readResult = _connection.readRecord( Internal::getSessionTable(), sessionKey, sessionRaw );
        if ( readResult == ServiceStoreResult::NotFound )
        {
            inoutTransaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
            return LoginResult::Ok;
        }
        if ( readResult != ServiceStoreResult::Ok )
            return LoginResult::StoreUnavailable;
        Internal::SessionRecord session;
        if ( Internal::decodeSession( sessionRaw._bytes, session ) == false )
            return LoginResult::StoreUnavailable;
        session._revokeReason = reason;
        session._expiresAtMs  = std::min( session._expiresAtMs, nowMs + _settings._tombstoneLifetimeMs );
        inoutTransaction.put( Internal::getSessionTable(), sessionKey, Internal::encodeSession( session ), sessionRaw._version );
        inoutTransaction.erase( Internal::getAccountSessionTable(), accountKey, linkRaw._version );
        outSessionId = sessionId;
        return LoginResult::Ok;
    }
} // namespace sw
