#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"

#include "Core/Encoding/HexUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct GuildServiceInternal
        {
            static constexpr uint8       kFormatVersion      = 1;
            static constexpr uint8       kGuildFormatVersion = 2;                             ///< 길드 레코드이다. 2 에서 경험치와 공개 여부를 더했다(옛 레코드는 다시 쓴다)
            static constexpr int64       kMaxExperience      = static_cast<int64>( 1 ) << 53; ///< 경험치 상한이다. 넘으면 상한에 멈춘다
            static constexpr const utf8* kSequenceKey        = "guild";
            static constexpr uint32      kReplacementChar    = 0xFFFDu;
            static constexpr uint32      kFirstPrintable     = 0x20u;
            static constexpr uint32      kDeleteChar         = 0x7Fu;
            static constexpr const utf8* kStorageDomain      = "guild";
            static constexpr const utf8* kDepositReason      = "guild.deposit";
            static constexpr const utf8* kWithdrawReason     = "guild.withdraw";
            static constexpr const utf8* kRefundReason       = "guild.refund"; ///< 떠나거나 내보내진 멤버에게 그의 몫을 돌려준다

            /** @brief 길드 창고 보유자(`esc/guild/<길드 16진>`)입니다. 이전 가능 자산의 공용 창고입니다. */
            static LedgerHolder makeStorageHolder( uint64 guildID ) { return LedgerHolder::makeEscrow( kStorageDomain, ServiceKeyUtil::makeHex64( guildID ) ); }

            /**
             * @brief 맡긴 사람별 창고 보유자(`esc/guild/<길드 16진>.<계정 16진>`)입니다. 이전 불가 자산이 듭니다.
             * @details 섞이면 누가 맡긴 몫인지 잃으므로 보유자를 나눈다. 공용 창고의 잔액 키 앞(`esc/guild/<길드 16진>/`)과 겹치지 않는다.
             */
            static LedgerHolder makeDepositorHolder( uint64 guildID, AccountID depositorID )
            {
                string token = ServiceKeyUtil::makeHex64( guildID );
                token.push_back( '.' );
                ServiceKeyUtil::appendHex64( token, depositorID );
                return LedgerHolder::makeEscrow( kStorageDomain, token );
            }

            /** @brief 꺼내기 허용 키(`<길드>/<맡긴 사람>/<받는 계정>/<자산>`)입니다. 맡긴 사람의 허용은 `<길드>/<맡긴 사람>/` 로 열거합니다. */
            static string makeGrantKey( uint64 guildID, AccountID depositorID, AccountID granteeID, string_view assetID )
            {
                string key = makePairKey( guildID, depositorID );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, granteeID );
                key.push_back( '/' );
                key.append( assetID );
                return key;
            }

            /** @brief 꺼내기 허용입니다. 남은 한도와 그 허용을 만든 요청의 분개 키(다시 온 부여가 쓴 만큼 줄어든 한도를 되돌리지 않게)입니다. */
            static vector<uint8> encodeGrant( int64 remaining, string_view grantKey )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarInt( remaining );
                ServiceKeyUtil::writeString( writer, grantKey );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeGrant( const vector<uint8>& bytes, int64& outRemaining, string& outGrantKey )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outRemaining = reader.readVarInt();
                return ServiceKeyUtil::readString( reader, LedgerConstant::kMaxJournalKeySize, outGrantKey ) && reader.hasOverflowed() == false && 0 <= outRemaining;
            }

            static const hashed_string& getGuildTable()
            {
                static const hashed_string s_table{ "social_guild" };
                return s_table;
            }

            static const hashed_string& getMemberTable()
            {
                static const hashed_string s_table{ "social_guild_member" };
                return s_table;
            }

            static const hashed_string& getAccountGuildTable()
            {
                static const hashed_string s_table{ "social_account_guild" };
                return s_table;
            }

            static const hashed_string& getNameTable()
            {
                static const hashed_string s_table{ "social_guild_name" };
                return s_table;
            }

            static const hashed_string& getInviteTable()
            {
                static const hashed_string s_table{ "social_guild_invite" };
                return s_table;
            }

            static const hashed_string& getApplicationTable()
            {
                static const hashed_string s_table{ "social_guild_application" };
                return s_table;
            }

            /** @brief 이전 불가 자산을 맡긴 사람(`<길드>/<계정>`)입니다. 원장은 보유자를 열거하지 못하므로 해산 확인과 창고 목록이 이것으로 몫을 찾습니다. */
            static const hashed_string& getDepositorTable()
            {
                static const hashed_string s_table{ "social_guild_depositor" };
                return s_table;
            }

            /** @brief 맡긴 사람이 준 꺼내기 허용(`<길드>/<맡긴 사람>/<받는 계정>/<자산>`)입니다. */
            static const hashed_string& getGrantTable()
            {
                static const hashed_string s_table{ "social_guild_storage_grant" };
                return s_table;
            }

            static const hashed_string& getSequenceTable()
            {
                static const hashed_string s_table{ "social_sequence" };
                return s_table;
            }

            static string makePairKey( uint64 first, uint64 second )
            {
                string key = ServiceKeyUtil::makeHex64( first );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, second );
                return key;
            }

            /** @brief 이름 키를 만듭니다. ASCII 는 소문자로 바꾼 뒤 모든 바이트를 16진 문자열로 바꾸므로 한글 이름도 키 규칙 안에 들어옵니다. 24바이트는 48글자가 됩니다. */
            static string makeNameKey( string_view name )
            {
                string key;
                for ( const utf8 character : name )
                {
                    const uint8 byte = static_cast<uint8>( StringUtil::toLowerChar( character ) );
                    HexUtil::appendBytes( key, &byte, 1 );
                }
                return key;
            }

            static vector<uint8> encodeGuild( const GuildInfo& info )
            {
                BitWriter writer;
                writer.writeBits( kGuildFormatVersion, 8 );
                ServiceKeyUtil::writeString( writer, info._name );
                ServiceKeyUtil::writeString( writer, info._notice );
                writer.writeVarUint( info._masterID );
                writer.writeVarInt( info._createdMs );
                writer.writeVarInt( info._memberCount );
                writer.writeVarInt( info._experience );
                writer.writeBits( info._bOpenToApply == SW_TRUE ? 1u : 0u, 1 );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeGuild( const vector<uint8>& bytes, uint64 guildID, GuildInfo& outInfo )
            {
                BitReader  reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                const bool bHeadOk = reader.readBits( 8 ) == kGuildFormatVersion && ServiceKeyUtil::readString( reader, GuildLimit::kMaxNameSize, outInfo._name ) &&
                                     ServiceKeyUtil::readString( reader, GuildLimit::kMaxNoticeSize, outInfo._notice );
                if ( bHeadOk == false )
                    return false;
                outInfo._guildID      = guildID;
                outInfo._masterID     = reader.readVarUint();
                outInfo._createdMs    = reader.readVarInt();
                outInfo._memberCount  = reader.readVarInt32();
                outInfo._experience   = reader.readVarInt();
                outInfo._bOpenToApply = reader.readBits( 1 ) == 1u ? SW_TRUE : SW_FALSE;
                return reader.hasOverflowed() == false && 0 <= outInfo._experience;
            }

            static vector<uint8> encodeMember( GuildRole role, int64 joinedMs )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarUint( static_cast<uint64>( role ) );
                writer.writeVarInt( joinedMs );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeMember( const vector<uint8>& bytes, GuildMember& outMember )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                const uint64 role   = reader.readVarUint();
                outMember._joinedMs = reader.readVarInt();
                if ( reader.hasOverflowed() || role >= static_cast<uint64>( GuildRole::Count ) )
                    return false;
                outMember._role = static_cast<GuildRole>( role );
                return true;
            }

            static vector<uint8> encodeID( uint64 value )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarUint( value );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeID( const vector<uint8>& bytes, uint64& outValue )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outValue = reader.readVarUint();
                return reader.hasOverflowed() == false;
            }

            static vector<uint8> encodeInvite( AccountID inviterID, int64 expiresMs )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarUint( inviterID );
                writer.writeVarInt( expiresMs );
                return writer.getBytes();
            }

            static vector<uint8> encodeApplication( int64 appliedMs, int64 expiresMs )
            {
                BitWriter writer;
                writer.writeBits( kFormatVersion, 8 );
                writer.writeVarInt( appliedMs );
                writer.writeVarInt( expiresMs );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeApplication( const vector<uint8>& bytes, int64& outAppliedMs, int64& outExpiresMs )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                outAppliedMs = reader.readVarInt();
                outExpiresMs = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }

            [[nodiscard]] static bool decodeInviteExpiry( const vector<uint8>& bytes, int64& outExpiresMs )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                (void)reader.readVarUint(); // 초대한 사람 필드는 건너뛴다. 읽기가 넘쳤는지는 아래 hasOverflowed 가 확인한다
                outExpiresMs = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }
        };

        /** @brief 길드 요청 하나를 저장소 스레드에서 처리합니다. 읽기, 규칙 검사, 버전 조건 쓰기 순서로 진행하며 충돌이 나면 다시 시도합니다. 한 번 쓰고 버린다. */
        class GuildStoreRun final
        {
        public:
            GuildStoreRun( const GuildRequest& request, const ILedgerPolicy* pLedgerPolicy )
                : _request{ request }
                , _serviceResult{}
                , _pLedgerPolicy{ pLedgerPolicy }
            {
            }

            /** @brief 요청을 처리하고 결과를 넘깁니다. */
            GuildResult run( IServiceStoreConnection& connection )
            {
                runAttempts( connection );
                return std::move( _serviceResult );
            }

        private:
            void runAttempts( IServiceStoreConnection& connection )
            {
                for ( int32 attempt = 0; attempt < OnlineLimit::kMaxConflictRetry; ++attempt )
                {
                    _serviceResult                  = GuildResult{};
                    const ServiceStoreResult result = runOnce( connection );
                    if ( result == ServiceStoreResult::Ok )
                    {
                        collectMembers( connection );
                        return;
                    }
                    if ( result != ServiceStoreResult::Conflict )
                    {
                        if ( _serviceResult._result == SocialResult::Ok )
                            _serviceResult._result = SocialServiceResult::fail( OnlineError::kUnavailable );
                        return;
                    }
                }
                _serviceResult         = GuildResult{};
                _serviceResult._result = SocialServiceResult::fail( OnlineError::kBusy );
            }

            /** @brief 규칙 실패를 처리합니다. 결과를 적고 Invalid 를 반환하며 다시 시도하지 않습니다. */
            ServiceStoreResult fail( SocialServiceResult result )
            {
                _serviceResult._result = result;
                return ServiceStoreResult::Invalid;
            }

            /** @brief 계정의 길드를 읽습니다. 길드가 있으면 Ok, 없으면 NotFound, 저장소를 쓸 수 없으면 Unavailable 입니다. */
            static ServiceStoreResult readAccountGuild( IServiceStoreConnection& connection, AccountID accountID, uint64& outGuildID, ServiceRecord& outRecord )
            {
                outGuildID                    = 0;
                const ServiceStoreResult read = connection.readRecord( GuildServiceInternal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( accountID ), outRecord );
                if ( read != ServiceStoreResult::Ok )
                    return read == ServiceStoreResult::NotFound ? ServiceStoreResult::NotFound : ServiceStoreResult::Unavailable;
                return GuildServiceInternal::decodeID( outRecord._bytes, outGuildID ) ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
            }

            ServiceStoreResult runOnce( IServiceStoreConnection& connection )
            {
                ServiceRecord            selfGuildRecord;
                uint64                   guildID  = 0;
                const ServiceStoreResult selfRead = readAccountGuild( connection, _request._accountID, guildID, selfGuildRecord );
                if ( selfRead == ServiceStoreResult::Unavailable )
                    return ServiceStoreResult::Unavailable;
                const bool bSelfInGuild = selfRead == ServiceStoreResult::Ok;
                switch ( _request._operation )
                {
                    case GuildOperation::Create:
                    {
                        return bSelfInGuild ? fail( SocialResult::AlreadyInGuild ) : runCreate( connection );
                    }
                    case GuildOperation::Accept:
                    {
                        return bSelfInGuild ? fail( SocialResult::AlreadyInGuild ) : runAccept( connection );
                    }
                    case GuildOperation::Apply:
                    {
                        return bSelfInGuild ? fail( SocialResult::AlreadyInGuild ) : runApply( connection );
                    }
                    case GuildOperation::AddExperience:
                    {
                        return runAddExperience( connection );
                    }
                    case GuildOperation::Attach:
                    {
                        if ( bSelfInGuild == false )
                            return fail( SocialResult::NotInGuild );
                        _serviceResult._guildID    = guildID;
                        _serviceResult._attachedID = _request._accountID;
                        return ServiceStoreResult::Ok;
                    }
                    default:
                    {
                        return bSelfInGuild ? runInGuild( connection, guildID, selfGuildRecord ) : fail( SocialResult::NotInGuild );
                    }
                }
            }

            ServiceStoreResult runCreate( IServiceStoreConnection& connection )
            {
                using Internal = GuildServiceInternal;
                if ( GuildService::isValidName( _request._text ) == false )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                ServiceRecord            nameRecord;
                ServiceRecord            sequenceRecord;
                const string             nameKey      = Internal::makeNameKey( _request._text );
                const ServiceStoreResult nameRead     = connection.readRecord( Internal::getNameTable(), nameKey, nameRecord );
                const ServiceStoreResult sequenceRead = connection.readRecord( Internal::getSequenceTable(), Internal::kSequenceKey, sequenceRecord );
                if ( nameRead == ServiceStoreResult::Ok )
                    return fail( SocialResult::GuildNameTaken );
                if ( nameRead != ServiceStoreResult::NotFound || ( sequenceRead != ServiceStoreResult::Ok && sequenceRead != ServiceStoreResult::NotFound ) )
                    return ServiceStoreResult::Unavailable;
                uint64 nextID = 1;
                if ( sequenceRead == ServiceStoreResult::Ok && Internal::decodeID( sequenceRecord._bytes, nextID ) == false )
                    return ServiceStoreResult::Unavailable;
                GuildInfo info;
                info._guildID     = nextID;
                info._name        = _request._text;
                info._masterID    = _request._accountID;
                info._createdMs   = _request._nowMs.count();
                info._memberCount = 1;
                ServiceTransaction transaction;
                transaction.put( Internal::getSequenceTable(), Internal::kSequenceKey, Internal::encodeID( nextID + 1 ), sequenceRecord._version );
                transaction.put( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( nextID ), Internal::encodeGuild( info ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getMemberTable(), Internal::makePairKey( nextID, _request._accountID ), Internal::encodeMember( GuildRole::Master, _request._nowMs.count() ),
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), Internal::encodeID( nextID ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getNameTable(), nameKey, Internal::encodeID( nextID ), ServiceRecord::kAbsentVersion );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                {
                    _serviceResult._info     = info;
                    _serviceResult._guildID  = nextID;
                    _serviceResult._joinedID = _request._accountID;
                }
                return commitResult; // 이름이 그사이 선점됐으면 다시 읽어 GuildNameTaken 으로 끝낸다
            }

            /** @brief 공개 길드에 가입 신청을 냅니다. 죽은 신청을 지우고 살아 있는 신청이 상한이면 PendingLimit 입니다. */
            ServiceStoreResult runApply( IServiceStoreConnection& connection )
            {
                using Internal = GuildServiceInternal;
                ServiceRecord guildRecord;
                GuildInfo     info;
                const string  guildKey = ServiceKeyUtil::makeHex64( _request._guildID );
                const bool    bGuildOk = connection.readRecord( Internal::getGuildTable(), guildKey, guildRecord ) == ServiceStoreResult::Ok &&
                                      Internal::decodeGuild( guildRecord._bytes, _request._guildID, info );
                if ( bGuildOk == false )
                    return fail( SocialResult::NotFound );
                if ( info._bOpenToApply == SW_FALSE )
                    return fail( SocialResult::GuildClosed );
                if ( info._memberCount >= GuildLimit::kMaxMember )
                    return fail( SocialResult::GuildFull );
                vector<ServiceRecord> listRecord;
                const string          prefix = guildKey + "/";
                if ( connection.listRecords( Internal::getApplicationTable(), prefix, "", GuildLimit::kMaxApplication, false, listRecord ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                const string       selfKey   = Internal::makePairKey( _request._guildID, _request._accountID );
                int32              liveCount = 0;
                ServiceTransaction transaction;
                for ( const ServiceRecord& record : listRecord )
                {
                    if ( record._key == selfKey )
                        continue; // 다시 낸 신청은 아래 put 이 덮어쓴다
                    int64      appliedMs = 0;
                    int64      expiresMs = 0;
                    const bool bLive     = Internal::decodeApplication( record._bytes, appliedMs, expiresMs ) && _request._nowMs.count() < expiresMs;
                    if ( bLive )
                        ++liveCount;
                    else
                        transaction.erase( Internal::getApplicationTable(), record._key, record._version ); // 죽었거나 읽을 수 없는 신청이다
                }
                if ( liveCount >= GuildLimit::kMaxApplication )
                    return fail( SocialResult::PendingLimit );
                transaction.put( Internal::getApplicationTable(), selfKey, Internal::encodeApplication( _request._nowMs.count(), _request._nowMs.count() + GuildLimit::kApplicationLifetimeMs ) );
                transaction.requireVersion( Internal::getGuildTable(), guildKey, guildRecord._version ); // 그사이 비공개로 바뀌면 다시 읽는다
                return connection.commit( transaction );
            }

            /** @brief 서버 시스템이 길드 경험치를 늘립니다. 상한(`kMaxExperience`)을 넘지 않습니다. */
            ServiceStoreResult runAddExperience( IServiceStoreConnection& connection )
            {
                using Internal = GuildServiceInternal;
                if ( _request._amount <= 0 )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                ServiceRecord guildRecord;
                GuildInfo     info;
                const string  guildKey = ServiceKeyUtil::makeHex64( _request._guildID );
                const bool    bGuildOk = connection.readRecord( Internal::getGuildTable(), guildKey, guildRecord ) == ServiceStoreResult::Ok &&
                                      Internal::decodeGuild( guildRecord._bytes, _request._guildID, info );
                if ( bGuildOk == false )
                    return fail( SocialResult::NotFound );
                const bool bReachesCap = _request._amount >= Internal::kMaxExperience - info._experience;
                info._experience       = bReachesCap ? Internal::kMaxExperience : info._experience + _request._amount;
                ServiceTransaction transaction;
                transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( info ), guildRecord._version );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                {
                    _serviceResult._guildID = _request._guildID;
                    _serviceResult._info    = info;
                }
                return commitResult;
            }

            ServiceStoreResult runAccept( IServiceStoreConnection& connection )
            {
                using Internal = GuildServiceInternal;
                ServiceRecord inviteRecord;
                ServiceRecord guildRecord;
                int64         expiresMs = 0;
                GuildInfo     info;
                const string  inviteKey = Internal::makePairKey( _request._accountID, _request._guildID );
                const string  guildKey  = ServiceKeyUtil::makeHex64( _request._guildID );
                const bool    bInvited  = connection.readRecord( Internal::getInviteTable(), inviteKey, inviteRecord ) == ServiceStoreResult::Ok &&
                                      Internal::decodeInviteExpiry( inviteRecord._bytes, expiresMs );
                if ( bInvited == false )
                    return fail( SocialResult::NotFound );
                if ( _request._nowMs.count() >= expiresMs )
                    return fail( SocialResult::InviteExpired );
                const bool bGuildOk = connection.readRecord( Internal::getGuildTable(), guildKey, guildRecord ) == ServiceStoreResult::Ok &&
                                      Internal::decodeGuild( guildRecord._bytes, _request._guildID, info );
                if ( bGuildOk == false )
                    return fail( SocialResult::NotFound ); // 해산된 길드이다
                if ( info._memberCount >= GuildLimit::kMaxMember )
                    return fail( SocialResult::GuildFull );
                ++info._memberCount;
                ServiceTransaction transaction;
                transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( info ), guildRecord._version );
                transaction.put( Internal::getMemberTable(), Internal::makePairKey( _request._guildID, _request._accountID ),
                                 Internal::encodeMember( GuildRole::Member, _request._nowMs.count() ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), Internal::encodeID( _request._guildID ),
                                 ServiceRecord::kAbsentVersion );
                transaction.erase( Internal::getInviteTable(), inviteKey, inviteRecord._version );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                {
                    _serviceResult._guildID  = _request._guildID;
                    _serviceResult._joinedID = _request._accountID;
                }
                return commitResult;
            }

            /** @brief "내 길드" 에 대한 요청입니다. 조회, 초대, 떠나기, 내보내기, 역할, 공지가 해당합니다. */
            ServiceStoreResult runInGuild( IServiceStoreConnection& connection, uint64 guildID, const ServiceRecord& selfGuildRecord )
            {
                using Internal = GuildServiceInternal;
                ServiceRecord guildRecord;
                ServiceRecord selfMemberRecord;
                GuildInfo     info;
                GuildMember   selfMember;
                const string  guildKey = ServiceKeyUtil::makeHex64( guildID );
                const bool    bReadOk  = connection.readRecord( Internal::getGuildTable(), guildKey, guildRecord ) == ServiceStoreResult::Ok &&
                                     Internal::decodeGuild( guildRecord._bytes, guildID, info ) &&
                                     connection.readRecord( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord ) ==
                                         ServiceStoreResult::Ok &&
                                     Internal::decodeMember( selfMemberRecord._bytes, selfMember );
                if ( bReadOk == false )
                    return ServiceStoreResult::Unavailable;
                _serviceResult._guildID = guildID;

                ServiceTransaction transaction;
                switch ( _request._operation )
                {
                    case GuildOperation::Get:
                    {
                        _serviceResult._info = info;
                        return ServiceStoreResult::Ok; // 멤버 목록은 collectMembers 가 채운다
                    }
                    case GuildOperation::Invite:
                    {
                        return runInvite( connection, guildID, selfMember );
                    }
                    case GuildOperation::Leave:
                    {
                        return runLeave( connection, guildID, info, guildRecord, selfMember, selfMemberRecord, selfGuildRecord );
                    }
                    case GuildOperation::Kick:
                    case GuildOperation::SetRole:
                    {
                        return runMemberChange( connection, guildID, info, guildRecord, selfMember, selfMemberRecord );
                    }
                    case GuildOperation::SetNotice:
                    {
                        if ( selfMember._role < GuildRole::Officer )
                            return fail( SocialResult::NotAllowed );
                        if ( _request._text.size() > static_cast<size_t>( GuildLimit::kMaxNoticeSize ) )
                            return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                        info._notice = _request._text;
                        transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( info ), guildRecord._version );
                        return connection.commit( transaction );
                    }
                    case GuildOperation::Deposit:
                    case GuildOperation::Withdraw:
                    {
                        return runStorageTransfer( connection, guildID, info, guildRecord, selfMember, selfMemberRecord );
                    }
                    case GuildOperation::SetJoinPolicy:
                    {
                        if ( selfMember._role < GuildRole::Officer )
                            return fail( SocialResult::NotAllowed );
                        info._bOpenToApply = _request._bOpen == SW_TRUE ? SW_TRUE : SW_FALSE;
                        transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( info ), guildRecord._version );
                        return connection.commit( transaction );
                    }
                    case GuildOperation::ListApplications:
                    case GuildOperation::Approve:
                    case GuildOperation::Reject:
                    {
                        if ( selfMember._role < GuildRole::Officer )
                            return fail( SocialResult::NotAllowed );
                        return runApplication( connection, guildID, info, guildRecord );
                    }
                    case GuildOperation::ListStorage:
                    {
                        return runListStorage( connection, guildID );
                    }
                    case GuildOperation::GrantWithdrawal:
                    case GuildOperation::RevokeWithdrawal:
                    {
                        return runStorageGrant( connection, guildID, selfMemberRecord );
                    }
                    default:
                    {
                        return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                    }
                }
            }

            /** @brief 임원이 다른 계정을 길드에 초대합니다. 다시 보내면 초대를 덮어씁니다. */
            ServiceStoreResult runInvite( IServiceStoreConnection& connection, uint64 guildID, const GuildMember& selfMember )
            {
                using Internal = GuildServiceInternal;
                if ( selfMember._role < GuildRole::Officer )
                    return fail( SocialResult::NotAllowed );
                if ( _request._targetID == kInvalidAccountID || _request._targetID == _request._accountID )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                uint64                   targetGuildID = 0;
                ServiceRecord            targetRecord;
                const ServiceStoreResult targetRead = readAccountGuild( connection, _request._targetID, targetGuildID, targetRecord );
                if ( targetRead == ServiceStoreResult::Unavailable )
                    return ServiceStoreResult::Unavailable;
                if ( targetRead == ServiceStoreResult::Ok )
                    return fail( SocialResult::AlreadyInGuild );
                ServiceTransaction transaction;
                transaction.put( Internal::getInviteTable(), Internal::makePairKey( _request._targetID, guildID ),
                                 Internal::encodeInvite( _request._accountID, _request._nowMs.count() + GuildLimit::kInviteLifetimeMs ) ); // 다시 보내면 덮어쓴다
                _serviceResult._invitedID = _request._targetID;
                return connection.commit( transaction );
            }

            /** @brief 길드를 떠납니다. 혼자 남은 길드장이 떠나면 길드를 해산하며, 공용 창고와 맡긴 사람별 몫이 모두 비어 있어야 합니다. 멤버가 떠나면 그의 몫을 돌려줍니다. */
            ServiceStoreResult runLeave( IServiceStoreConnection& connection, uint64 guildID, GuildInfo& inoutInfo, const ServiceRecord& guildRecord, const GuildMember& selfMember,
                                         const ServiceRecord& selfMemberRecord, const ServiceRecord& selfGuildRecord )
            {
                using Internal = GuildServiceInternal;
                if ( selfMember._role == GuildRole::Master && inoutInfo._memberCount > 1 )
                    return fail( SocialResult::NotAllowed ); // 길드장을 넘기고 떠난다
                ServiceTransaction       transaction;
                const ServiceStoreResult staged = selfMember._role == GuildRole::Master ? stageDissolution( connection, guildID, transaction )
                                                                                        : stageShareRefund( connection, guildID, _request._accountID, transaction );
                if ( staged != ServiceStoreResult::Ok )
                    return staged;
                transaction.erase( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord._version );
                transaction.erase( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), selfGuildRecord._version );
                if ( selfMember._role == GuildRole::Master ) // 혼자 남은 길드장이면 길드를 해산한다
                {
                    transaction.erase( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( guildID ), guildRecord._version );
                    transaction.erase( Internal::getNameTable(), Internal::makeNameKey( inoutInfo._name ) );
                }
                else
                {
                    --inoutInfo._memberCount;
                    transaction.put( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( guildID ), Internal::encodeGuild( inoutInfo ), guildRecord._version );
                }
                _serviceResult._leftID = _request._accountID;
                return connection.commit( transaction ); // 몫을 돌려주는 이동이 그사이 바뀐 잔액과 부딪히면 Conflict 로 다시 읽는다
            }

            /**
             * @brief 해산 전 확인입니다. 공용 창고와 맡긴 사람별 몫이 모두 비어 있어야 하며, 빈 몫의 맡긴 사람 기록과 남은 꺼내기 허용을 @p inoutTransaction 에서 지웁니다.
             * @details 창고를 비운 뒤에 해산한다. 맡기기는 길드 레코드 버전을 올리므로 이 확인 뒤에 끼면 커밋이 충돌한다.
             *          맡긴 사람은 떠날 때 기록이 지워지므로 해산 때 남은 것은 혼자 남은 길드장의 것뿐이지만 기록 전체를 본다.
             */
            ServiceStoreResult stageDissolution( IServiceStoreConnection& connection, uint64 guildID, ServiceTransaction& inoutTransaction )
            {
                using Internal = GuildServiceInternal;
                vector<LedgerBalance> listBalance;
                if ( Ledger::listBalances( connection, Internal::makeStorageHolder( guildID ), listBalance ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                if ( listBalance.empty() == false )
                    return fail( SocialResult::StorageNotEmpty );
                vector<ServiceRecord> listDepositorRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( guildID ) + "/";
                if ( connection.listRecords( Internal::getDepositorTable(), prefix, "", GuildLimit::kMaxMember, false, listDepositorRecord ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                for ( const ServiceRecord& record : listDepositorRecord )
                {
                    AccountID depositorID = kInvalidAccountID;
                    if ( ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), depositorID ) == false )
                        return ServiceStoreResult::Unavailable;
                    if ( Ledger::listBalances( connection, Internal::makeDepositorHolder( guildID, depositorID ), listBalance ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    if ( listBalance.empty() == false )
                        return fail( SocialResult::StorageNotEmpty ); // 맡긴 몫이 남았다 — 몫의 주인이 꺼낸 뒤에 해산한다
                    inoutTransaction.erase( Internal::getDepositorTable(), record._key, record._version );
                }
                vector<ServiceRecord> listGrantRecord;
                if ( connection.listRecords( Internal::getGrantTable(), prefix, "", GuildLimit::kMaxStorageGrant, false, listGrantRecord ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                for ( const ServiceRecord& record : listGrantRecord ) // 떠난 맡긴 사람의 허용은 떠날 때 지웠으니 길드장의 허용(상한 안)만 남는다
                {
                    inoutTransaction.erase( Internal::getGrantTable(), record._key, record._version );
                }
                return ServiceStoreResult::Ok;
            }

            /**
             * @brief 떠나거나 내보내지는 @p depositorID 의 몫을 그 계정으로 돌려주는 원장 이동과, 그의 맡긴 사람 기록 · 그가 준 허용을 지우는 쓰기를 @p inoutTransaction 에 붙입니다.
             * @details 몫이 남은 채 멤버가 아니게 되면 아무도 꺼낼 수 없으므로 같은 트랜잭션에서 돌려준다. 분개 키는 요청(떠나기 · 내보내기)의 멱등 키이며 몫이 있을 때만 필요하다.
             *          계정 상한을 넘으면 떠나기 · 내보내기가 CapExceeded 로 멈춘다 — 재화를 버리지 않는다.
             */
            ServiceStoreResult stageShareRefund( IServiceStoreConnection& connection, uint64 guildID, AccountID depositorID, ServiceTransaction& inoutTransaction )
            {
                using Internal = GuildServiceInternal;
                ServiceRecord            depositorRecord;
                const string             depositorKey = Internal::makePairKey( guildID, depositorID );
                const ServiceStoreResult read         = connection.readRecord( Internal::getDepositorTable(), depositorKey, depositorRecord );
                if ( read == ServiceStoreResult::NotFound )
                    return ServiceStoreResult::Ok; // 이전 불가 자산을 맡긴 적이 없다
                if ( read != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                const LedgerHolder    share = Internal::makeDepositorHolder( guildID, depositorID );
                vector<LedgerBalance> listBalance;
                if ( Ledger::listBalances( connection, share, listBalance ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                if ( listBalance.empty() == false )
                {
                    if ( LedgerUtil::isValidJournalKey( _request._journalKey ) == false )
                        return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                    LedgerTransferRequest transfer;
                    for ( const LedgerBalance& balance : listBalance ) // 자산 종류는 맡기기가 `kMaxDepositorAsset`(다리 상한) 안으로 막는다
                    {
                        transfer._listPosting.push_back( LedgerPosting{ share, LedgerHolder::makeAccount( depositorID ), balance._assetID, balance._amount } );
                    }
                    transfer._journalKey = _request._journalKey;
                    transfer._reason     = Internal::kRefundReason;
                    transfer._pPolicy    = _pLedgerPolicy;
                    transfer._timeMs     = _request._nowMs.count();
                    transfer._actorID    = _request._accountID;
                    transfer._actorKind  = LedgerActorKind::Player;
                    LedgerTransferResult transferResult;
                    const LedgerResult   staged = Ledger::stageTransfer( connection, transfer, inoutTransaction, transferResult );
                    if ( staged != LedgerResult::Ok )
                        return failLedger( staged );
                }
                inoutTransaction.erase( Internal::getDepositorTable(), depositorKey, depositorRecord._version );
                vector<ServiceRecord> listGrantRecord;
                if ( connection.listRecords( Internal::getGrantTable(), depositorKey + "/", "", GuildLimit::kMaxStorageGrant, false, listGrantRecord ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                for ( const ServiceRecord& record : listGrantRecord )
                {
                    inoutTransaction.erase( Internal::getGrantTable(), record._key, record._version );
                }
                return ServiceStoreResult::Ok;
            }

            /** @brief 길드 창고의 품목(공용 창고, 그다음 맡긴 사람별 몫)을 상한(`kMaxStorageItem`)까지 결과에 담습니다. */
            ServiceStoreResult runListStorage( IServiceStoreConnection& connection, uint64 guildID )
            {
                using Internal = GuildServiceInternal;
                vector<LedgerBalance> listBalance;
                if ( Ledger::listBalances( connection, Internal::makeStorageHolder( guildID ), listBalance ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                appendStorageItems( listBalance, kInvalidAccountID );
                vector<ServiceRecord> listDepositorRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( guildID ) + "/";
                if ( connection.listRecords( Internal::getDepositorTable(), prefix, "", GuildLimit::kMaxMember, false, listDepositorRecord ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                for ( const ServiceRecord& record : listDepositorRecord )
                {
                    AccountID depositorID = kInvalidAccountID;
                    if ( ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), depositorID ) == false )
                        return ServiceStoreResult::Unavailable;
                    listBalance.clear();
                    if ( Ledger::listBalances( connection, Internal::makeDepositorHolder( guildID, depositorID ), listBalance ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    appendStorageItems( listBalance, depositorID );
                }
                return ServiceStoreResult::Ok;
            }

            /** @brief 잔액들을 창고 품목으로 결과에 붙입니다. 상한(`kMaxStorageItem`)을 넘는 것은 버립니다. */
            void appendStorageItems( const vector<LedgerBalance>& listBalance, AccountID depositorID )
            {
                // shortcut: 상한을 넘는 품목은 응답에서 빠진다, 창고 품목이 상한보다 많아지는 게임이면 쪽 읽기(커서)로 바꾼다
                for ( const LedgerBalance& balance : listBalance )
                {
                    if ( _serviceResult._listStorage.size() >= static_cast<size_t>( GuildLimit::kMaxStorageItem ) )
                        return;
                    _serviceResult._listStorage.push_back( GuildStorageItem{ balance._assetID, balance._amount, depositorID } );
                }
            }

            /** @brief 임원의 신청 처리입니다. 목록, 받아들이기(수락과 같은 쓰기), 지우기가 해당합니다. */
            ServiceStoreResult runApplication( IServiceStoreConnection& connection, uint64 guildID, GuildInfo& inoutInfo, const ServiceRecord& guildRecord )
            {
                using Internal = GuildServiceInternal;
                if ( _request._operation == GuildOperation::ListApplications )
                {
                    vector<ServiceRecord> listRecord;
                    const string          prefix = ServiceKeyUtil::makeHex64( guildID ) + "/";
                    if ( connection.listRecords( Internal::getApplicationTable(), prefix, "", GuildLimit::kMaxApplication, false, listRecord ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    for ( const ServiceRecord& record : listRecord )
                    {
                        GuildApplication application;
                        int64            expiresMs = 0;
                        const bool       bLive     = Internal::decodeApplication( record._bytes, application._appliedMs, expiresMs ) && _request._nowMs.count() < expiresMs &&
                                           ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), application._accountID );
                        if ( bLive )
                            _serviceResult._listApplication.push_back( application );
                    }
                    return ServiceStoreResult::Ok;
                }
                ServiceRecord applicationRecord;
                int64         appliedMs        = 0;
                int64         expiresMs        = 0;
                const string  applicationKey   = Internal::makePairKey( guildID, _request._targetID );
                const bool    bApplicationRead = connection.readRecord( Internal::getApplicationTable(), applicationKey, applicationRecord ) == ServiceStoreResult::Ok &&
                                              Internal::decodeApplication( applicationRecord._bytes, appliedMs, expiresMs );
                if ( bApplicationRead == false )
                    return fail( SocialResult::NotFound );
                ServiceTransaction transaction;
                transaction.erase( Internal::getApplicationTable(), applicationKey, applicationRecord._version );
                if ( _request._operation == GuildOperation::Reject )
                    return connection.commit( transaction );
                if ( _request._nowMs.count() >= expiresMs )
                    return fail( SocialResult::InviteExpired );
                uint64                   targetGuildID = 0;
                ServiceRecord            targetRecord;
                const ServiceStoreResult targetRead = readAccountGuild( connection, _request._targetID, targetGuildID, targetRecord );
                if ( targetRead == ServiceStoreResult::Unavailable )
                    return ServiceStoreResult::Unavailable;
                if ( targetRead == ServiceStoreResult::Ok )
                    return fail( SocialResult::AlreadyInGuild ); // 그사이 다른 길드에 들어갔다
                if ( inoutInfo._memberCount >= GuildLimit::kMaxMember )
                    return fail( SocialResult::GuildFull );
                ++inoutInfo._memberCount;
                const string guildKey = ServiceKeyUtil::makeHex64( guildID );
                transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( inoutInfo ), guildRecord._version );
                transaction.put( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._targetID ), Internal::encodeMember( GuildRole::Member, _request._nowMs.count() ),
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._targetID ), Internal::encodeID( guildID ), ServiceRecord::kAbsentVersion );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                    _serviceResult._joinedID = _request._targetID;
                return commitResult;
            }

            /** @brief 내보내기와 역할 바꾸기(길드장 넘기기 포함)입니다. */
            ServiceStoreResult runMemberChange( IServiceStoreConnection& connection, uint64 guildID, GuildInfo& inoutInfo, const ServiceRecord& guildRecord,
                                                const GuildMember& selfMember, const ServiceRecord& selfMemberRecord )
            {
                using Internal = GuildServiceInternal;
                ServiceRecord targetMemberRecord;
                GuildMember   targetMember;
                const string  targetMemberKey = Internal::makePairKey( guildID, _request._targetID );
                const string  guildKey        = ServiceKeyUtil::makeHex64( guildID );
                const bool    bTargetOk       = _request._targetID != _request._accountID &&
                                       connection.readRecord( Internal::getMemberTable(), targetMemberKey, targetMemberRecord ) == ServiceStoreResult::Ok &&
                                       Internal::decodeMember( targetMemberRecord._bytes, targetMember );
                if ( bTargetOk == false )
                    return fail( SocialResult::NotFound );
                ServiceTransaction transaction;
                if ( _request._operation == GuildOperation::Kick )
                {
                    if ( selfMember._role <= targetMember._role )
                        return fail( SocialResult::NotAllowed );
                    ServiceRecord            targetGuildRecord;
                    uint64                   targetGuildID = 0;
                    const ServiceStoreResult targetRead    = readAccountGuild( connection, _request._targetID, targetGuildID, targetGuildRecord );
                    if ( targetRead == ServiceStoreResult::Unavailable )
                        return ServiceStoreResult::Unavailable;
                    const ServiceStoreResult refunded = stageShareRefund( connection, guildID, _request._targetID, transaction );
                    if ( refunded != ServiceStoreResult::Ok )
                        return refunded;
                    --inoutInfo._memberCount;
                    transaction.erase( Internal::getMemberTable(), targetMemberKey, targetMemberRecord._version );
                    transaction.erase( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._targetID ), targetGuildRecord._version );
                    transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( inoutInfo ), guildRecord._version );
                    _serviceResult._leftID = _request._targetID;
                    return connection.commit( transaction );
                }
                if ( selfMember._role != GuildRole::Master )
                    return fail( SocialResult::NotAllowed );
                if ( _request._role >= GuildRole::Count ) // 와이어에서 온 값이다. 그대로 쓰면 그 멤버 레코드를 다시 읽지 못한다
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                transaction.put( Internal::getMemberTable(), targetMemberKey, Internal::encodeMember( _request._role, targetMember._joinedMs ), targetMemberRecord._version );
                if ( _request._role == GuildRole::Master ) // 길드장을 넘기면 나는 임원이 된다
                {
                    transaction.put( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ),
                                     Internal::encodeMember( GuildRole::Officer, selfMember._joinedMs ), selfMemberRecord._version );
                    inoutInfo._masterID = _request._targetID;
                    transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( inoutInfo ), guildRecord._version );
                }
                return connection.commit( transaction );
            }

            /** @brief 원장 결과를 규칙 실패나 저장소 결과로 바꿉니다. 충돌은 다시 읽어 다시 하도록 Conflict 를 반환합니다. */
            ServiceStoreResult failLedger( LedgerResult result )
            {
                switch ( result )
                {
                    case LedgerResult::InsufficientFunds:
                        return fail( SocialResult::InsufficientFunds );
                    case LedgerResult::CapExceeded:
                        return fail( SocialResult::CapExceeded );
                    case LedgerResult::Conflict:
                        return ServiceStoreResult::Conflict;
                    case LedgerResult::Unavailable:
                        return ServiceStoreResult::Unavailable;
                    default:
                        return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) ); // 규칙 밖의 자산이나 양, 다른 내용에 다시 쓴 멱등 키이다
                }
            }

            /**
             * @brief 맡기기와 꺼내기입니다. 원장 이동을 멤버 레코드 버전 조건과 한 트랜잭션에 붙여 커밋합니다.
             * @details 이전 불가 자산(유료 재화)은 맡긴 사람별 보유자에 든다 — 공용 창고에 섞이면 다른 임원이 꺼내 계정 사이로 옮겨진다. 맡긴 당사자가 허용한 권한 안에서만
             *          다른 계정으로 간다(사용자 결정 2026-11-12, 감사 O-10). 정책이 없으면(화폐 카탈로그 없이 조립한 서버) 유료 재화도 없어 모두 공용 창고이다.
             */
            ServiceStoreResult runStorageTransfer( IServiceStoreConnection& connection, uint64 guildID, const GuildInfo& info, const ServiceRecord& guildRecord,
                                                   const GuildMember& selfMember, const ServiceRecord& selfMemberRecord )
            {
                using Internal      = GuildServiceInternal;
                const bool bDeposit = _request._operation == GuildOperation::Deposit;
                const bool bValid   = LedgerUtil::isValidAssetID( _request._assetID ) && _request._amount > 0 && _request._amount <= LedgerConstant::kMaxAmount &&
                                    LedgerUtil::isValidJournalKey( _request._journalKey );
                if ( bValid == false )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                const bool bShare = _pLedgerPolicy != nullptr && _pLedgerPolicy->isTransferable( _request._assetID ) == false;
                // 맡기기의 몫 주인은 서버가 정하고(요청한 사람), 꺼내기는 어느 몫인지 요청이 적는다 — 자산 성격과 어긋나면 규칙 밖이다
                const bool bDepositorOk = bDeposit ? _request._depositorID == kInvalidAccountID : ( _request._depositorID != kInvalidAccountID ) == bShare;
                if ( bDepositorOk == false )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                const AccountID    depositorID = bDeposit ? _request._accountID : _request._depositorID;
                const LedgerHolder storage     = bShare ? Internal::makeDepositorHolder( guildID, depositorID ) : Internal::makeStorageHolder( guildID );
                const LedgerHolder account     = LedgerHolder::makeAccount( _request._accountID );

                ServiceTransaction transaction;
                if ( bDeposit == false )
                {
                    const ServiceStoreResult allowed = stageWithdrawalRight( connection, guildID, selfMember, bShare, transaction );
                    if ( allowed != ServiceStoreResult::Ok )
                        return allowed;
                }
                else if ( bShare )
                {
                    vector<LedgerBalance> listBalance;
                    if ( Ledger::listBalances( connection, storage, listBalance ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    bool bHeld = false;
                    for ( const LedgerBalance& balance : listBalance )
                    {
                        bHeld = bHeld || balance._assetID == _request._assetID;
                    }
                    if ( bHeld == false && listBalance.size() >= static_cast<size_t>( GuildLimit::kMaxDepositorAsset ) )
                        return fail( SocialResult::StorageLimit ); // 떠날 때 몫 전체를 이동 하나(다리 상한)로 돌려준다
                    transaction.put( Internal::getDepositorTable(), Internal::makePairKey( guildID, depositorID ), Internal::encodeID( depositorID ) );
                }

                LedgerTransferRequest transfer;
                transfer._listPosting.push_back( LedgerPosting{ bDeposit ? account : storage, bDeposit ? storage : account, _request._assetID, _request._amount } );
                transfer._journalKey = _request._journalKey;
                transfer._reason     = bDeposit ? Internal::kDepositReason : Internal::kWithdrawReason;
                transfer._pPolicy    = _pLedgerPolicy;
                transfer._timeMs     = _request._nowMs.count();
                transfer._actorID    = _request._accountID;
                transfer._actorKind  = LedgerActorKind::Player;

                transaction.requireVersion( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord._version );
                if ( bDeposit ) // 길드 레코드 버전을 올려 해산의 "창고가 비었다" 확인과 겹치지 않게 한다. 맡기기끼리도 줄을 세워 몫의 자산 종류 상한을 지킨다
                    transaction.put( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( guildID ), Internal::encodeGuild( info ), guildRecord._version );
                LedgerTransferResult transferResult;
                const LedgerResult   staged = Ledger::stageTransfer( connection, transfer, transaction, transferResult );
                if ( staged != LedgerResult::Ok )
                    return failLedger( staged );
                if ( transferResult._bReplayed == SW_FALSE && connection.commit( transaction ) != ServiceStoreResult::Ok )
                {
                    const LedgerResult resolved = Ledger::resolveConflict( connection, transfer, transferResult );
                    if ( resolved != LedgerResult::Ok )
                        return resolved == LedgerResult::Conflict ? ServiceStoreResult::Conflict : ServiceStoreResult::Unavailable;
                }
                const AccountID itemDepositorID = bShare ? depositorID : kInvalidAccountID;
                for ( const LedgerTransferResult::HolderBalance& holderBalance : transferResult._listHolderBalance )
                {
                    if ( holderBalance._holder == storage )
                    {
                        _serviceResult._listStorage.push_back( GuildStorageItem{ holderBalance._balance._assetID, holderBalance._balance._amount, itemDepositorID } );
                        return ServiceStoreResult::Ok;
                    }
                }
                LedgerBalance balance; // 다시 온 요청이면 이동 뒤 잔액이 없을 수 있어 지금 잔액을 읽는다
                if ( Ledger::readBalance( connection, storage, _request._assetID, balance ) == ServiceStoreResult::Ok )
                    _serviceResult._listStorage.push_back( GuildStorageItem{ _request._assetID, balance._amount, itemDepositorID } );
                return ServiceStoreResult::Ok;
            }

            /**
             * @brief 꺼내기 권한을 봅니다. 공용 창고는 임원 이상, 맡긴 사람별 몫은 본인이거나 그가 준 허용의 남은 한도 안일 때만이며, 허용을 쓰면 한도를 줄이는 쓰기를
             *        @p inoutTransaction 에 붙입니다(원장 이동과 같은 트랜잭션 — 그사이 회수 · 다른 꺼내기가 끼면 버전 충돌로 다시 읽는다).
             * @details 권한이 없어도 같은 분개 키의 이동이 이미 적용돼 있으면 통과시켜 원장이 처음 결과를 돌려주게 한다 — 한도를 다 쓴 뒤 다시 온 요청도 같은 답을 받는다.
             *          분개 키는 요청한 계정 범위(`makeAccountScope`)라 다른 계정이 만들 수 없다.
             */
            ServiceStoreResult stageWithdrawalRight( IServiceStoreConnection& connection, uint64 guildID, const GuildMember& selfMember, bool bShare,
                                                     ServiceTransaction& inoutTransaction )
            {
                using Internal = GuildServiceInternal;
                bool bAllowed  = bShare ? _request._depositorID == _request._accountID : selfMember._role >= GuildRole::Officer;
                if ( bShare && bAllowed == false )
                {
                    ServiceRecord            grantRecord;
                    int64                    remaining = 0;
                    string                   grantKey;
                    const string             recordKey = Internal::makeGrantKey( guildID, _request._depositorID, _request._accountID, _request._assetID );
                    const ServiceStoreResult read      = connection.readRecord( Internal::getGrantTable(), recordKey, grantRecord );
                    if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                        return ServiceStoreResult::Unavailable;
                    bAllowed = read == ServiceStoreResult::Ok && Internal::decodeGrant( grantRecord._bytes, remaining, grantKey ) && _request._amount <= remaining;
                    if ( bAllowed )
                        inoutTransaction.put( Internal::getGrantTable(), recordKey, Internal::encodeGrant( remaining - _request._amount, grantKey ), grantRecord._version );
                }
                if ( bAllowed )
                    return ServiceStoreResult::Ok;
                LedgerJournalEntry       entry;
                const ServiceStoreResult found = Ledger::findJournal( connection, _request._journalKey, entry );
                if ( found == ServiceStoreResult::NotFound )
                    return fail( SocialResult::NotAllowed );
                return found == ServiceStoreResult::Ok ? ServiceStoreResult::Ok : ServiceStoreResult::Unavailable;
            }

            /**
             * @brief 맡긴 사람이 자기 몫의 꺼내기 허용을 주거나(받는 계정 · 자산 · 한도, 한도는 덮어쓴다) 거둡니다. 맡긴 사람 본인만 하므로 키의 맡긴 사람은 요청한 계정입니다.
             * @details 허용은 이전 불가 자산에만 뜻이 있다(공용 창고는 임원 권한이 정한다). 받는 계정은 지금 멤버여야 한다.
             *          WoW 길드 은행의 출금 한도처럼 한도는 쌓지 않고 덮어쓰며, 꺼낼 때 쓴 만큼 줄어든다.
             *          다시 온 같은 부여(기록의 분개 키가 같다)는 쓴 만큼 줄어든 한도를 되돌리지 않는다. 회수는 기록을 지우므로 이미 없으면 그대로 Ok 이다.
             */
            ServiceStoreResult runStorageGrant( IServiceStoreConnection& connection, uint64 guildID, const ServiceRecord& selfMemberRecord )
            {
                using Internal    = GuildServiceInternal;
                const bool bGrant = _request._operation == GuildOperation::GrantWithdrawal;
                const bool bValid = LedgerUtil::isValidAssetID( _request._assetID ) && LedgerUtil::isValidJournalKey( _request._journalKey ) &&
                                    _request._targetID != kInvalidAccountID && _request._targetID != _request._accountID &&
                                    ( bGrant == false || ( _request._amount > 0 && _request._amount <= LedgerConstant::kMaxAmount ) );
                if ( bValid == false )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) );
                if ( _pLedgerPolicy == nullptr || _pLedgerPolicy->isTransferable( _request._assetID ) )
                    return fail( SocialServiceResult::fail( OnlineError::kInvalidRequest ) ); // 공용 창고 자산에는 맡긴 사람별 허용이 없다
                ServiceRecord            grantRecord;
                const string             recordKey = Internal::makeGrantKey( guildID, _request._accountID, _request._targetID, _request._assetID );
                const ServiceStoreResult grantRead = connection.readRecord( Internal::getGrantTable(), recordKey, grantRecord );
                if ( grantRead != ServiceStoreResult::Ok && grantRead != ServiceStoreResult::NotFound )
                    return ServiceStoreResult::Unavailable;
                const bool         bExists = grantRead == ServiceStoreResult::Ok;
                ServiceTransaction transaction;
                transaction.requireVersion( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord._version );
                if ( bGrant == false )
                {
                    if ( bExists == false )
                        return ServiceStoreResult::Ok;
                    transaction.erase( Internal::getGrantTable(), recordKey, grantRecord._version );
                    return connection.commit( transaction );
                }
                int64  remaining = 0;
                string grantKey;
                if ( bExists && Internal::decodeGrant( grantRecord._bytes, remaining, grantKey ) && grantKey == _request._journalKey )
                    return ServiceStoreResult::Ok;
                ServiceRecord targetMemberRecord;
                GuildMember   targetMember;
                const string  targetMemberKey = Internal::makePairKey( guildID, _request._targetID );
                const bool    bTargetOk       = connection.readRecord( Internal::getMemberTable(), targetMemberKey, targetMemberRecord ) == ServiceStoreResult::Ok &&
                                       Internal::decodeMember( targetMemberRecord._bytes, targetMember );
                if ( bTargetOk == false )
                    return fail( SocialResult::NotFound );
                if ( bExists == false )
                {
                    vector<ServiceRecord> listGrantRecord;
                    const string          prefix = Internal::makePairKey( guildID, _request._accountID ) + "/";
                    if ( connection.listRecords( Internal::getGrantTable(), prefix, "", GuildLimit::kMaxStorageGrant, false, listGrantRecord ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    if ( listGrantRecord.size() >= static_cast<size_t>( GuildLimit::kMaxStorageGrant ) )
                        return fail( SocialResult::StorageLimit ); // 쓰지 않는 허용을 거둔 뒤에 준다
                }
                transaction.requireVersion( Internal::getMemberTable(), targetMemberKey, targetMemberRecord._version ); // 그사이 내보내지면 다시 읽는다
                transaction.put( Internal::getGrantTable(), recordKey, Internal::encodeGrant( _request._amount, _request._journalKey ),
                                 bExists ? grantRecord._version : ServiceRecord::kAbsentVersion );
                return connection.commit( transaction );
            }

            /** @brief 성공한 뒤 멤버 목록을 읽습니다. 조회이면 정보로, 변경이면 알림 대상으로 씁니다. 창고 요청(이동 · 목록 · 허용)은 멤버 정보가 바뀌지 않으므로 읽지 않습니다. */
            void collectMembers( IServiceStoreConnection& connection )
            {
                const bool bStorage = _request._operation == GuildOperation::Deposit || _request._operation == GuildOperation::Withdraw ||
                                      _request._operation == GuildOperation::ListStorage || _request._operation == GuildOperation::GrantWithdrawal ||
                                      _request._operation == GuildOperation::RevokeWithdrawal;
                const bool bApplication = _request._operation == GuildOperation::Apply || _request._operation == GuildOperation::ListApplications ||
                                          _request._operation == GuildOperation::Reject;
                const bool bSkip = _serviceResult._guildID == 0 || _request._operation == GuildOperation::Attach || _request._operation == GuildOperation::Invite || bStorage ||
                                   bApplication;
                if ( bSkip )
                    return;
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _serviceResult._guildID ) + "/";
                if ( connection.listRecords( GuildServiceInternal::getMemberTable(), prefix, "", GuildLimit::kMaxMember, false, listRecord ) != ServiceStoreResult::Ok )
                    return; // 알림만 빠지며 클라이언트가 다시 조회한다
                for ( const ServiceRecord& record : listRecord )
                {
                    GuildMember member;
                    const bool  bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), member._accountID ) &&
                                         GuildServiceInternal::decodeMember( record._bytes, member );
                    if ( bParsed == false )
                        continue;
                    _serviceResult._listNotifyMember.push_back( member._accountID );
                    if ( _request._operation == GuildOperation::Get )
                        _serviceResult._info._listMember.push_back( member );
                }
            }

        private:
            GuildRequest         _request;
            GuildResult          _serviceResult;
            const ILedgerPolicy* _pLedgerPolicy; ///< 서비스 스레드에서 복사해 둔다. 저장소 스레드는 서비스를 읽지 않는다
        };
    } // namespace
} // namespace sw

namespace sw
{
    GuildService::GuildService()
        : _completionBuffer{}
        , _notificationBuffer{}
        , _onEvent{}
        , _listLevelExperience{}
        , _pLedgerPolicy{ nullptr }
        , _store{}
    {
    }

    void GuildService::initialize( IServiceStore* pStore )
    {
        SW_ASSERT( pStore != nullptr );
        _store.bindStore( pStore );
    }

    void GuildService::shutdown()
    {
        _store.bindStore( nullptr ); // 남은 일은 조립이 저장소 드레인으로 거둔다(예전과 같이 여기서 기다리지 않는다)
        _pLedgerPolicy = nullptr;    // 이벤트 칸은 건 쪽이 구독을 놓아 비운다
    }

    bool GuildService::isValidName( string_view name )
    {
        const bool bSizeOk           = name.empty() == false && name.size() <= static_cast<size_t>( GuildLimit::kMaxNameSize );
        const bool bInvalidGuildName = bSizeOk == false || name.front() == ' ' || name.back() == ' ';
        if ( bInvalidGuildName )
            return false;
        size_t offset = 0;
        while ( offset < name.size() )
        {
            const uint32 codepoint = StringUtil::decodeUTF8( name, offset );
            const bool   bBad      = codepoint == GuildServiceInternal::kReplacementChar || codepoint < GuildServiceInternal::kFirstPrintable ||
                              codepoint == GuildServiceInternal::kDeleteChar;
            if ( bBad )
                return false;
        }
        return true;
    }

    void GuildService::submit( const GuildRequest& request, uint64 requestTag )
    {
        _store.submit( [request, pLedgerPolicy = _pLedgerPolicy]( IServiceStoreConnection& connection )
        {
            GuildStoreRun runner{ request, pLedgerPolicy };
            return runner.run( connection );
        }, [this, request, requestTag]( GuildResult& serviceResult )
        { applyServiceResult( request, requestTag, std::move( serviceResult ) ); } );
    }

    void GuildService::attachAccount( AccountID accountID, UnixMilliseconds nowMs )
    {
        GuildRequest request;
        request._operation = GuildOperation::Attach;
        request._accountID = accountID;
        request._nowMs     = nowMs;
        submit( request, 0 );
    }

    void GuildService::addExperience( uint64 guildID, int64 amount, UnixMilliseconds nowMs )
    {
        GuildRequest request;
        request._operation = GuildOperation::AddExperience;
        request._guildID   = guildID;
        request._amount    = amount;
        request._nowMs     = nowMs;
        submit( request, 0 );
    }

    expected<void> GuildService::setLevelTable( const vector<int64>& listLevelExperience )
    {
        if ( listLevelExperience.size() > static_cast<size_t>( GuildLimit::kMaxLevelCount ) )
        {
            StringBuilder<constant::buffer::kMax128> detailBuilder;
            detailBuilder.appendFormat( "%# levels, at most %#", listLevelExperience.size(), static_cast<int32>( GuildLimit::kMaxLevelCount ) );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the level table has too many levels", detailBuilder.view() } );
        }
        int64 previous = 0;
        for ( const int64 experience : listLevelExperience )
        {
            if ( experience <= previous )
            {
                StringBuilder<constant::buffer::kMax128> detailBuilder;
                detailBuilder.appendFormat( "%# after %#", experience, previous );
                return unexpected( Error{ ErrorKind::InvalidArgument, "level experience must be positive and greater than the previous level", detailBuilder.view() } );
            }
            previous = experience;
        }
        _listLevelExperience = listLevelExperience;
        return {};
    }

    int32 GuildService::computeLevel( int64 experience ) const
    {
        int32 level = 1;
        for ( const int64 threshold : _listLevelExperience )
        {
            if ( experience < threshold )
                break;
            ++level;
        }
        return level;
    }

    void GuildService::applyServiceResult( const GuildRequest& request, uint64 requestTag, GuildResult&& serviceResult )
    {
        const bool bServerOperation = request._operation == GuildOperation::Attach || request._operation == GuildOperation::AddExperience;
        if ( bServerOperation == false )
        {
            GuildCompletion completion;
            completion._requestTag      = requestTag;
            completion._result          = serviceResult._result;
            completion._info            = serviceResult._info;
            completion._info._level     = computeLevel( serviceResult._info._experience );
            completion._listStorage     = std::move( serviceResult._listStorage );
            completion._listApplication = std::move( serviceResult._listApplication );
            _completionBuffer.push( std::move( completion ) );
        }
        if ( serviceResult._result != SocialResult::Ok )
            return;
        if ( serviceResult._attachedID != kInvalidAccountID )
            raiseEvent( serviceResult._attachedID, serviceResult._guildID, GuildEvent::Kind::MemberAttached, request._nowMs );
        if ( serviceResult._joinedID != kInvalidAccountID )
            raiseEvent( serviceResult._joinedID, serviceResult._guildID, GuildEvent::Kind::Joined, request._nowMs );
        if ( serviceResult._leftID != kInvalidAccountID )
        {
            raiseEvent( serviceResult._leftID, serviceResult._guildID, GuildEvent::Kind::Left, request._nowMs );
            if ( serviceResult._leftID != request._accountID ) // 내보내진 사람이다. 이제 멤버 목록에 없다
                _notificationBuffer.push( SocialNotification{ SocialPresence{}, serviceResult._leftID, request._accountID, serviceResult._guildID, SocialNotificationKind::GuildChanged } );
        }
        if ( serviceResult._invitedID != kInvalidAccountID )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, serviceResult._invitedID, request._accountID, serviceResult._guildID, SocialNotificationKind::GuildInvited } );
        if ( request._operation == GuildOperation::Get )
            return;
        for ( const AccountID memberID : serviceResult._listNotifyMember )
        {
            if ( memberID != request._accountID )
                _notificationBuffer.push( SocialNotification{ SocialPresence{}, memberID, request._accountID, serviceResult._guildID, SocialNotificationKind::GuildChanged } );
        }
    }

    void GuildService::raiseEvent( AccountID accountID, uint64 guildID, GuildEvent::Kind kind, UnixMilliseconds nowMs )
    {
        if ( _onEvent.isBound() )
            _onEvent.get()( GuildEvent{ accountID, guildID, nowMs, kind } );
    }
} // namespace sw
