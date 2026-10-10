#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct GuildServiceInternal
        {
            static constexpr uint8       kFormatVersion    = 1;
            static constexpr int32       kMaxConflictRetry = 4;
            static constexpr const utf8* kSequenceKey      = "guild";
            static constexpr uint32      kReplacementChar  = 0xFFFDu;
            static constexpr uint32      kFirstPrintable   = 0x20u;
            static constexpr uint32      kDeleteChar       = 0x7Fu;

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

            /** @brief 이름 키 — ASCII 는 소문자로 바꾼 뒤 모든 바이트를 16 진(한글 이름도 키 규칙 안, 24 바이트 → 48 글자). */
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
                writer.writeBits( kFormatVersion, 8 );
                ServiceKeyUtil::writeString( writer, info._name );
                ServiceKeyUtil::writeString( writer, info._notice );
                writer.writeVarUint( info._masterID );
                writer.writeVarInt( info._createdMs );
                writer.writeVarInt( info._memberCount );
                return writer.getBytes();
            }

            [[nodiscard]] static bool decodeGuild( const vector<uint8>& bytes, uint64 guildID, GuildInfo& outInfo )
            {
                BitReader  reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                const bool bHeadOk = reader.readBits( 8 ) == kFormatVersion && ServiceKeyUtil::readString( reader, GuildLimit::kMaxNameSize, outInfo._name ) &&
                                     ServiceKeyUtil::readString( reader, GuildLimit::kMaxNoticeSize, outInfo._notice );
                if ( bHeadOk == false )
                    return false;
                outInfo._guildID     = guildID;
                outInfo._masterID    = reader.readVarUint();
                outInfo._createdMs   = reader.readVarInt();
                outInfo._memberCount = static_cast<int32>( reader.readVarInt() );
                return reader.hasOverflowed() == false;
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

            [[nodiscard]] static bool decodeInviteExpiry( const vector<uint8>& bytes, int64& outExpiresMs )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                if ( reader.readBits( 8 ) != kFormatVersion )
                    return false;
                (void)reader.readVarUint(); // 초대한 사람 칸은 건너뛴다 — 넘침은 아래 hasOverflowed 가 본다
                outExpiresMs = reader.readVarInt();
                return reader.hasOverflowed() == false;
            }
        };

        /** @brief 길드 요청 하나 — 읽기 → 규칙 → 판 조건 쓰기, 충돌이면 다시. */
        class GuildWork final : public IServiceStoreWork
        {
        public:
            GuildWork( GuildService* pService, const GuildRequest& request, uint64 requestTag )
                : _request{ request }
                , _outcome{}
                , _pService{ pService }
                , _requestTag{ requestTag }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                for ( int32 attempt = 0; attempt < GuildServiceInternal::kMaxConflictRetry; ++attempt )
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
            /** @brief 규칙 실패 — 결과를 적고 Invalid(다시 하지 않는다)를 돌려준다. */
            ServiceStoreResult fail( SocialResult result )
            {
                _outcome._result = result;
                return ServiceStoreResult::Invalid;
            }

            /** @brief 계정의 길드를 읽습니다 — Ok(있음) · NotFound(없음) · Unavailable. */
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
                return commitResult; // 이름이 그새 잡혔으면 다시 읽어 GuildNameTaken
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
                    return fail( SocialResult::NotFound ); // 해산된 길드
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

            /** @brief "내 길드" 위의 요청(조회 · 초대 · 떠나기 · 내보내기 · 역할 · 공지)입니다. */
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
                        return ServiceStoreResult::Ok; // 회원 목록은 collectMembers 가 채운다
                    }
                    case GuildOperation::Invite:
                    {
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
                        transaction.put( Internal::getInviteTable(), Internal::makePairKey( _request._targetID, guildID ),
                                         Internal::encodeInvite( _request._accountID, _request._nowMs + GuildLimit::kInviteLifetimeMs ) ); // 다시 보내면 덮는다
                        _outcome._invitedID = _request._targetID;
                        return connection.commit( transaction );
                    }
                    case GuildOperation::Leave:
                    {
                        if ( selfMember._role == GuildRole::Master && info._memberCount > 1 )
                            return fail( SocialResult::NotAllowed ); // 넘기고 떠난다
                        transaction.erase( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ), selfMemberRecord._version );
                        transaction.erase( Internal::getAccountGuildTable(), ServiceKeyUtil::makeHex64( _request._accountID ), selfGuildRecord._version );
                        if ( selfMember._role == GuildRole::Master ) // 혼자 남은 길드장 — 해산
                        {
                            transaction.erase( Internal::getGuildTable(), guildKey, guildRecord._version );
                            transaction.erase( Internal::getNameTable(), Internal::makeNameKey( info._name ) );
                        }
                        else
                        {
                            --info._memberCount;
                            transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( info ), guildRecord._version );
                        }
                        _outcome._leftID = _request._accountID;
                        return connection.commit( transaction );
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
                    default:
                    {
                        return fail( SocialResult::Invalid );
                    }
                }
            }

            /** @brief 내보내기 · 역할 바꾸기(길드장 넘기기 포함)입니다. */
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
                if ( _request._role == GuildRole::Master ) // 길드장 넘기기 — 나는 임원으로
                {
                    transaction.put( Internal::getMemberTable(), Internal::makePairKey( guildID, _request._accountID ),
                                     Internal::encodeMember( GuildRole::Officer, selfMember._joinedMs ), selfMemberRecord._version );
                    inoutInfo._masterID = _request._targetID;
                    transaction.put( Internal::getGuildTable(), guildKey, Internal::encodeGuild( inoutInfo ), guildRecord._version );
                }
                return connection.commit( transaction );
            }

            /** @brief 성공 뒤 회원 목록을 읽습니다 — 조회는 정보로, 바꾸기는 알림 대상으로. */
            void collectMembers( IServiceStoreConnection& connection )
            {
                const bool bSkip = _outcome._guildID == 0 || _request._operation == GuildOperation::Attach || _request._operation == GuildOperation::Invite;
                if ( bSkip )
                    return;
                vector<ServiceRecord> listRecord;
                const string          prefix = ServiceKeyUtil::makeHex64( _outcome._guildID ) + "/";
                if ( connection.listRecords( GuildServiceInternal::getMemberTable(), prefix, "", GuildLimit::kMaxMember, false, listRecord ) != ServiceStoreResult::Ok )
                    return; // 알림만 빠진다(클라이언트가 다시 조회)
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

            GuildRequest  _request;
            GuildOutcome  _outcome;
            GuildService* _pService;
            uint64        _requestTag;
        };
    } // namespace
} // namespace sw

namespace sw
{
    GuildService::GuildService()
        : _completionBuffer{}
        , _notificationBuffer{}
        , _onEvent{}
        , _pStore{ nullptr }
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
        _pStore  = nullptr;
        _onEvent = GuildEventDelegate{};
    }

    bool GuildService::isValidName( string_view name )
    {
        const bool bSizeOk = name.empty() == false && name.size() <= static_cast<size_t>( GuildLimit::kMaxNameSize );
        if ( bSizeOk == false || name.front() == ' ' || name.back() == ' ' )
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

    void GuildService::applyOutcome( const GuildRequest& request, uint64 requestTag, GuildOutcome&& outcome )
    {
        --_pendingCount;
        if ( request._operation != GuildOperation::Attach )
        {
            GuildCompletion completion;
            completion._requestTag = requestTag;
            completion._result     = outcome._result;
            completion._info       = outcome._info;
            _completionBuffer.push( std::move( completion ) );
        }
        if ( outcome._result != SocialResult::Ok )
            return;
        if ( outcome._attachedID != kInvalidAccountID )
            raiseEvent( outcome._attachedID, outcome._guildID, GuildEvent::Kind::MemberAttached );
        if ( outcome._joinedID != kInvalidAccountID )
            raiseEvent( outcome._joinedID, outcome._guildID, GuildEvent::Kind::Joined );
        if ( outcome._leftID != kInvalidAccountID )
        {
            raiseEvent( outcome._leftID, outcome._guildID, GuildEvent::Kind::Left );
            if ( outcome._leftID != request._accountID ) // 내보내진 사람 — 이제 회원 목록에 없다
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

    void GuildService::raiseEvent( AccountID accountID, uint64 guildID, GuildEvent::Kind kind )
    {
        if ( _onEvent.isBound() )
            _onEvent( GuildEvent{ accountID, guildID, kind } );
    }
} // namespace sw
