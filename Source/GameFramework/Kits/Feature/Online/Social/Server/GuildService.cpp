#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Service/OnlineLimit.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

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

            /** @brief 길드 창고 보유자(`esc/guild/<길드 16진>`)입니다. */
            static LedgerHolder makeStorageHolder( uint64 guildID ) { return LedgerHolder::makeEscrow( kStorageDomain, ServiceKeyUtil::makeHex64( guildID ) ); }

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
                static constexpr utf8 kHexDigit[] = "0123456789abcdef";
                string                key;
                for ( const utf8 character : name )
                {
                    const uint8 byte = static_cast<uint8>( StringUtil::toLowerChar( character ) );
                    key.push_back( kHexDigit[byte >> 4] );
                    key.push_back( kHexDigit[byte & 0xFu] );
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
                outInfo._memberCount  = static_cast<int32>( reader.readVarInt() );
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

        /** @brief 길드 요청 하나를 처리합니다. 읽기, 규칙 검사, 버전 조건 쓰기 순서로 진행하며 충돌이 나면 다시 시도합니다. */
        class GuildWork final : public IServiceStoreWork
        {
        public:
            GuildWork( GuildService* pService, const GuildRequest& request, uint64 requestTag )
                : _request{ request }
                , _outcome{}
                , _pService{ pService }
                , _pLedgerPolicy{ pService->getLedgerPolicy() }
                , _requestTag{ requestTag }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                for ( int32 attempt = 0; attempt < OnlineLimit::kMaxConflictRetry; ++attempt )
                {
                    _outcome                        = GuildOutcome{};
                    const ServiceStoreResult result = runOnce( connection );
                    if ( result == ServiceStoreResult::Ok )
                    {
                        collectMembers( connection );
                        return;
                    }
                    if ( result != ServiceStoreResult::Conflict )
                    {
                        if ( _outcome._result == SocialResult::Ok )
                            _outcome._result = SocialResult::Unavailable;
                        return;
                    }
                }
                _outcome         = GuildOutcome{};
                _outcome._result = SocialResult::Conflict;
            }

            void complete() override { _pService->applyOutcome( _request, _requestTag, std::move( _outcome ) ); }

        private:
            /** @brief 규칙 실패를 처리합니다. 결과를 적고 Invalid 를 반환하며 다시 시도하지 않습니다. */
            ServiceStoreResult fail( SocialResult result )
            {
                _outcome._result = result;
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
                        _outcome._guildID    = guildID;
                        _outcome._attachedID = _request._accountID;
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
                    return fail( SocialResult::Invalid );
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
                info._createdMs   = _request._nowMs;
                info._memberCount = 1;
                ServiceTransaction transaction;
                transaction.put( Internal::getSequenceTable(), Internal::kSequenceKey, Internal::encodeID( nextID + 1 ), sequenceRecord._version );
                transaction.put( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( nextID ), Internal::encodeGuild( info ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getMemberTable(), Internal::makePairKey( nextID, _request._accountID ), Internal::encodeMember( GuildRole::Master, _request._nowMs ),
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), Internal::encodeID( nextID ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getNameTable(), nameKey, Internal::encodeID( nextID ), ServiceRecord::kAbsentVersion );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                {
                    _outcome._info     = info;
                    _outcome._guildID  = nextID;
                    _outcome._joinedID = _request._accountID;
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
                    const bool bLive     = Internal::decodeApplication( record._bytes, appliedMs, expiresMs ) && _request._nowMs < expiresMs;
                    if ( bLive )
                        ++liveCount;
                    else
                        transaction.erase( Internal::getApplicationTable(), record._key, record._version ); // 죽었거나 읽을 수 없는 신청이다
                }
                if ( liveCount >= GuildLimit::kMaxApplication )
                    return fail( SocialResult::PendingLimit );
                transaction.put( Internal::getApplicationTable(), selfKey, Internal::encodeApplication( _request._nowMs, _request._nowMs + GuildLimit::kApplicationLifetimeMs ) );
                transaction.requireVersion( Internal::getGuildTable(), guildKey, guildRecord._version ); // 그사이 비공개로 바뀌면 다시 읽는다
                return connection.commit( transaction );
            }

            /** @brief 서버 시스템이 길드 경험치를 늘립니다. 상한(`kMaxExperience`)을 넘지 않습니다. */
            ServiceStoreResult runAddExperience( IServiceStoreConnection& connection )
            {
                using Internal = GuildServiceInternal;
                if ( _request._amount <= 0 )
                    return fail( SocialResult::Invalid );
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
                    _outcome._guildID = _request._guildID;
                    _outcome._info    = info;
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
                if ( _request._nowMs >= expiresMs )
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
                                 Internal::encodeMember( GuildRole::Member, _request._nowMs ), ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), Internal::encodeID( _request._guildID ),
                                 ServiceRecord::kAbsentVersion );
                transaction.erase( Internal::getInviteTable(), inviteKey, inviteRecord._version );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                {
                    _outcome._guildID  = _request._guildID;
                    _outcome._joinedID = _request._accountID;
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
                _outcome._guildID = guildID;

                ServiceTransaction transaction;
                switch ( _request._operation )
                {
                    case GuildOperation::Get:
                    {
                        _outcome._info = info;
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
                            return fail( SocialResult::Invalid );
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
                    default:
                    {
                        return fail( SocialResult::Invalid );
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
                    return fail( SocialResult::Invalid );
                uint64                   targetGuildID = 0;
                ServiceRecord            targetRecord;
                const ServiceStoreResult targetRead = readAccountGuild( connection, _request._targetID, targetGuildID, targetRecord );
                if ( targetRead == ServiceStoreResult::Unavailable )
                    return ServiceStoreResult::Unavailable;
                if ( targetRead == ServiceStoreResult::Ok )
                    return fail( SocialResult::AlreadyInGuild );
                ServiceTransaction transaction;
                transaction.put( Internal::getInviteTable(), Internal::makePairKey( _request._targetID, guildID ),
                                 Internal::encodeInvite( _request._accountID, _request._nowMs + GuildLimit::kInviteLifetimeMs ) ); // 다시 보내면 덮어쓴다
                _outcome._invitedID = _request._targetID;
                return connection.commit( transaction );
            }

            /** @brief 길드를 떠납니다. 혼자 남은 길드장이 떠나면 길드를 해산하며, 창고가 비어 있어야 합니다. */
            ServiceStoreResult runLeave( IServiceStoreConnection& connection, uint64 guildID, GuildInfo& inoutInfo, const ServiceRecord& guildRecord, const GuildMember& selfMember,
                                         const ServiceRecord& selfMemberRecord, const ServiceRecord& selfGuildRecord )
            {
                using Internal = GuildServiceInternal;
                if ( selfMember._role == GuildRole::Master && inoutInfo._memberCount > 1 )
                    return fail( SocialResult::NotAllowed ); // 길드장을 넘기고 떠난다
                if ( selfMember._role == GuildRole::Master )
                {
                    vector<LedgerBalance> listBalance;
                    if ( Ledger::listBalances( connection, Internal::makeStorageHolder( guildID ), listBalance ) != ServiceStoreResult::Ok )
                        return ServiceStoreResult::Unavailable;
                    if ( listBalance.empty() == false )
                        return fail( SocialResult::StorageNotEmpty ); // 창고를 비운 뒤에 해산한다. 맡기기는 길드 레코드 버전을 올리므로 이 확인 뒤에 끼면 커밋이 충돌한다
                }
                ServiceTransaction transaction;
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
                _outcome._leftID = _request._accountID;
                return connection.commit( transaction );
            }

            /** @brief 길드 창고의 품목을 상한(`kMaxStorageItem`)까지 결과에 담습니다. */
            ServiceStoreResult runListStorage( IServiceStoreConnection& connection, uint64 guildID )
            {
                using Internal = GuildServiceInternal;
                vector<LedgerBalance> listBalance;
                if ( Ledger::listBalances( connection, Internal::makeStorageHolder( guildID ), listBalance ) != ServiceStoreResult::Ok )
                    return ServiceStoreResult::Unavailable;
                const size_t count = listBalance.size() < static_cast<size_t>( GuildLimit::kMaxStorageItem ) ? listBalance.size() : static_cast<size_t>( GuildLimit::kMaxStorageItem );
                _outcome._listStorage.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    _outcome._listStorage.push_back( GuildStorageItem{ listBalance[index]._assetID, listBalance[index]._amount } );
                }
                return ServiceStoreResult::Ok;
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
                        const bool       bLive     = Internal::decodeApplication( record._bytes, application._appliedMs, expiresMs ) && _request._nowMs < expiresMs &&
                                           ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), application._accountID );
                        if ( bLive )
                            _outcome._listApplication.push_back( application );
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
                if ( _request._nowMs >= expiresMs )
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
                transaction.put( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._targetID ), Internal::encodeMember( GuildRole::Member, _request._nowMs ),
                                 ServiceRecord::kAbsentVersion );
                transaction.put( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._targetID ), Internal::encodeID( guildID ), ServiceRecord::kAbsentVersion );
                const ServiceStoreResult commitResult = connection.commit( transaction );
                if ( commitResult == ServiceStoreResult::Ok )
                    _outcome._joinedID = _request._targetID;
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
                    --inoutInfo._memberCount;
                    transaction.erase( Internal::getMemberTable(), targetMemberKey, targetMemberRecord._version );
                    transaction.erase( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._targetID ), targetGuildRecord._version );
                    transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( inoutInfo ), guildRecord._version );
                    _outcome._leftID = _request._targetID;
                    return connection.commit( transaction );
                }
                if ( selfMember._role != GuildRole::Master )
                    return fail( SocialResult::NotAllowed );
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
                        return fail( SocialResult::Invalid ); // 규칙 밖의 자산이나 양, 다른 내용에 다시 쓴 멱등 키이다
                }
            }

            /** @brief 맡기기와 꺼내기입니다. 원장 이동을 멤버 레코드 버전 조건과 한 트랜잭션에 붙여 커밋합니다. */
            ServiceStoreResult runStorageTransfer( IServiceStoreConnection& connection, uint64 guildID, const GuildInfo& info, const ServiceRecord& guildRecord,
                                                   const GuildMember& selfMember, const ServiceRecord& selfMemberRecord )
            {
                using Internal      = GuildServiceInternal;
                const bool bDeposit = _request._operation == GuildOperation::Deposit;
                if ( bDeposit == false && selfMember._role < GuildRole::Officer )
                    return fail( SocialResult::NotAllowed );
                const bool bValid = LedgerUtil::isValidAssetID( _request._assetID ) && _request._amount > 0 && _request._amount <= LedgerConstant::kMaxAmount &&
                                    LedgerUtil::isValidJournalKey( _request._journalKey );
                if ( bValid == false )
                    return fail( SocialResult::Invalid );
                const LedgerHolder storage = Internal::makeStorageHolder( guildID );
                const LedgerHolder account = LedgerHolder::makeAccount( _request._accountID );

                LedgerTransferRequest transfer;
                transfer._listPosting.push_back( LedgerPosting{ bDeposit ? account : storage, bDeposit ? storage : account, _request._assetID, _request._amount } );
                transfer._journalKey = _request._journalKey;
                transfer._reason     = bDeposit ? Internal::kDepositReason : Internal::kWithdrawReason;
                transfer._pPolicy    = _pLedgerPolicy;
                transfer._timeMs     = _request._nowMs;
                transfer._actorID    = _request._accountID;
                transfer._actorKind  = LedgerActorKind::Player;

                ServiceTransaction transaction;
                transaction.requireVersion( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord._version );
                if ( bDeposit ) // 길드 레코드 버전을 올려 해산의 "창고가 비었다" 확인과 겹치지 않게 한다
                    transaction.put( Internal::getGuildTable(), ServiceKeyUtil::makeHex64( guildID ), Internal::encodeGuild( info ), guildRecord._version );
                LedgerTransferOutcome transferOutcome;
                const LedgerResult    staged = Ledger::stageTransfer( connection, transfer, transaction, transferOutcome );
                if ( staged != LedgerResult::Ok )
                    return failLedger( staged );
                if ( transferOutcome._bReplayed == SW_FALSE && connection.commit( transaction ) != ServiceStoreResult::Ok )
                {
                    const LedgerResult resolved = Ledger::resolveConflict( connection, transfer, transferOutcome );
                    if ( resolved != LedgerResult::Ok )
                        return resolved == LedgerResult::Conflict ? ServiceStoreResult::Conflict : ServiceStoreResult::Unavailable;
                }
                for ( const LedgerTransferOutcome::HolderBalance& holderBalance : transferOutcome._listHolderBalance )
                {
                    if ( holderBalance._holder == storage )
                    {
                        _outcome._listStorage.push_back( GuildStorageItem{ holderBalance._balance._assetID, holderBalance._balance._amount } );
                        return ServiceStoreResult::Ok;
                    }
                }
                LedgerBalance balance; // 다시 온 요청이면 이동 뒤 잔액이 없을 수 있어 지금 잔액을 읽는다
                if ( Ledger::readBalance( connection, storage, _request._assetID, balance ) == ServiceStoreResult::Ok )
                    _outcome._listStorage.push_back( GuildStorageItem{ _request._assetID, balance._amount } );
                return ServiceStoreResult::Ok;
            }

            /** @brief 성공한 뒤 멤버 목록을 읽습니다. 조회이면 정보로, 변경이면 알림 대상으로 씁니다. 창고 요청은 멤버 정보가 바뀌지 않으므로 읽지 않습니다. */
            void collectMembers( IServiceStoreConnection& connection )
            {
                const bool bStorage = _request._operation == GuildOperation::Deposit || _request._operation == GuildOperation::Withdraw ||
                                      _request._operation == GuildOperation::ListStorage;
                const bool bApplication = _request._operation == GuildOperation::Apply || _request._operation == GuildOperation::ListApplications ||
                                          _request._operation == GuildOperation::Reject;
                const bool bSkip = _outcome._guildID == 0 || _request._operation == GuildOperation::Attach || _request._operation == GuildOperation::Invite || bStorage ||
                                   bApplication;
                if ( bSkip )
                    return;
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _outcome._guildID ) + "/";
                if ( connection.listRecords( GuildServiceInternal::getMemberTable(), prefix, "", GuildLimit::kMaxMember, false, listRecord ) != ServiceStoreResult::Ok )
                    return; // 알림만 빠지며 클라이언트가 다시 조회한다
                for ( const ServiceRecord& record : listRecord )
                {
                    GuildMember member;
                    const bool  bParsed = ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size() ), member._accountID ) &&
                                         GuildServiceInternal::decodeMember( record._bytes, member );
                    if ( bParsed == false )
                        continue;
                    _outcome._listNotifyMember.push_back( member._accountID );
                    if ( _request._operation == GuildOperation::Get )
                        _outcome._info._listMember.push_back( member );
                }
            }

            GuildRequest         _request;
            GuildOutcome         _outcome;
            GuildService*        _pService;
            const ILedgerPolicy* _pLedgerPolicy; ///< 서비스 스레드에서 복사해 둔다. 저장소 스레드는 서비스를 읽지 않는다
            uint64               _requestTag;
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
        , _pStore{ nullptr }
        , _pLedgerPolicy{ nullptr }
        , _pendingCount{ 0 }
    {
    }

    void GuildService::initialize( IServiceStore* pStore )
    {
        SW_ASSERT( pStore != nullptr );
        _pStore = pStore;
    }

    void GuildService::shutdown()
    {
        _pStore        = nullptr;
        _pLedgerPolicy = nullptr;
        _onEvent       = GuildEventDelegate{};
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
            const uint32 codepoint = StringUtil::decodeUtf8( name, offset );
            const bool   bBad      = codepoint == GuildServiceInternal::kReplacementChar || codepoint < GuildServiceInternal::kFirstPrintable ||
                              codepoint == GuildServiceInternal::kDeleteChar;
            if ( bBad )
                return false;
        }
        return true;
    }

    void GuildService::submit( const GuildRequest& request, uint64 requestTag )
    {
        ++_pendingCount;
        _pStore->submit( sw::make_unique<GuildWork>( this, request, requestTag ) );
    }

    void GuildService::attachAccount( AccountID accountID, int64 nowMs )
    {
        GuildRequest request;
        request._operation = GuildOperation::Attach;
        request._accountID = accountID;
        request._nowMs     = nowMs;
        submit( request, 0 );
    }

    void GuildService::addExperience( uint64 guildID, int64 amount, int64 nowMs )
    {
        GuildRequest request;
        request._operation = GuildOperation::AddExperience;
        request._guildID   = guildID;
        request._amount    = amount;
        request._nowMs     = nowMs;
        submit( request, 0 );
    }

    bool GuildService::setLevelTable( const vector<int64>& listLevelExperience )
    {
        if ( listLevelExperience.size() > static_cast<size_t>( GuildLimit::kMaxLevelCount ) )
            return false;
        int64 previous = 0;
        for ( const int64 experience : listLevelExperience )
        {
            if ( experience <= previous )
                return false;
            previous = experience;
        }
        _listLevelExperience = listLevelExperience;
        return true;
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

    void GuildService::applyOutcome( const GuildRequest& request, uint64 requestTag, GuildOutcome&& outcome )
    {
        --_pendingCount;
        const bool bServerOperation = request._operation == GuildOperation::Attach || request._operation == GuildOperation::AddExperience;
        if ( bServerOperation == false )
        {
            GuildCompletion completion;
            completion._requestTag      = requestTag;
            completion._result          = outcome._result;
            completion._info            = outcome._info;
            completion._info._level     = computeLevel( outcome._info._experience );
            completion._listStorage     = std::move( outcome._listStorage );
            completion._listApplication = std::move( outcome._listApplication );
            _completionBuffer.push( std::move( completion ) );
        }
        if ( outcome._result != SocialResult::Ok )
            return;
        if ( outcome._attachedID != kInvalidAccountID )
            raiseEvent( outcome._attachedID, outcome._guildID, GuildEvent::Kind::MemberAttached, request._nowMs );
        if ( outcome._joinedID != kInvalidAccountID )
            raiseEvent( outcome._joinedID, outcome._guildID, GuildEvent::Kind::Joined, request._nowMs );
        if ( outcome._leftID != kInvalidAccountID )
        {
            raiseEvent( outcome._leftID, outcome._guildID, GuildEvent::Kind::Left, request._nowMs );
            if ( outcome._leftID != request._accountID ) // 내보내진 사람이다. 이제 멤버 목록에 없다
                _notificationBuffer.push( SocialNotification{ SocialPresence{}, outcome._leftID, request._accountID, outcome._guildID, SocialNotificationKind::GuildChanged } );
        }
        if ( outcome._invitedID != kInvalidAccountID )
            _notificationBuffer.push( SocialNotification{ SocialPresence{}, outcome._invitedID, request._accountID, outcome._guildID, SocialNotificationKind::GuildInvited } );
        if ( request._operation == GuildOperation::Get )
            return;
        for ( const AccountID memberID : outcome._listNotifyMember )
        {
            if ( memberID != request._accountID )
                _notificationBuffer.push( SocialNotification{ SocialPresence{}, memberID, request._accountID, outcome._guildID, SocialNotificationKind::GuildChanged } );
        }
    }

    void GuildService::raiseEvent( AccountID accountID, uint64 guildID, GuildEvent::Kind kind, int64 nowMs )
    {
        if ( _onEvent.isBound() )
            _onEvent( GuildEvent{ accountID, guildID, nowMs, kind } );
    }
} // namespace sw
