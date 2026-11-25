#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Report/Server/ServiceReportStore.h"
#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStoreUtil.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ChatServiceInternal
        {
            static constexpr const utf8* kWhisperPrefix = "whisper.";
            static constexpr int64       kIndexDayMs    = constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond; ///< TTL 색인 줄 하나가 맡는 날짜 폭

            static const hashed_string& getHistoryTable()
            {
                static const hashed_string s_table{ "chat_history" };
                return s_table;
            }

            static const hashed_string& getHistoryIndexTable()
            {
                static const hashed_string s_table{ "chat_history_expiry" };
                return s_table;
            }

            /** @brief 보낸 사람 색인 `<보낸 계정 16진>/<보낸 시각 16진>/<기록 키>`(값 없음)입니다. 탈퇴 정리가 계정의 기록을 찾는다. 접두어 아래가 시각 순서다. */
            static const hashed_string& getSenderTable()
            {
                static const hashed_string s_table{ "chat_history_sender" };
                return s_table;
            }

            /** @brief 보낸 사람 색인의 TTL 색인 `<날짜 끝 16진>/<보낸 계정 16진>/`(보낸 사람 · 날짜마다 한 줄)입니다. */
            static const hashed_string& getSenderIndexTable()
            {
                static const hashed_string s_table{ "chat_history_sender_expiry" };
                return s_table;
            }

            static int64 getDayEndMs( int64 sentMs ) { return ( MathUtil::max<int64>( 0, sentMs ) / kIndexDayMs + 1 ) * kIndexDayMs; }

            static string makeSenderPrefix( AccountID senderID )
            {
                StringBuilder<constant::buffer::kMax32> prefix;
                prefix.appendFormat( "%#/", ServiceKeyUtil::makeHex64( senderID ) );
                return string( prefix.view() );
            }

            static string makeSenderKey( AccountID senderID, int64 sentMs, string_view historyKey )
            {
                StringBuilder<constant::buffer::kMax256> key;
                key.appendFormat( "%#%#/%#", makeSenderPrefix( senderID ), ServiceKeyUtil::makeHex64( static_cast<uint64>( MathUtil::max<int64>( 0, sentMs ) ) ), historyKey );
                return string( key.view() );
            }

            static string makeSenderIndexKey( AccountID senderID, int64 sentMs ) { return ServiceTTLRule::makeTTLIndexKey( getDayEndMs( sentMs ), makeSenderPrefix( senderID ) ); }

            /** @brief 보낸 사람 색인 키에서 보낸 시각을 읽습니다(TTL 쓸기 · 탈퇴 정리). */
            [[nodiscard]] static bool readSenderTimeMs( const ServiceRecord& record, int64& outTimeMs )
            {
                const size_t width  = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
                uint64       sentMs = 0;
                if ( record._key.size() < width * 2 + 2 || ServiceKeyUtil::parseHex64( string_view( record._key ).substr( width + 1, width ), sentMs ) == false )
                    return false;
                outTimeMs = static_cast<int64>( sentMs );
                return true;
            }

            /** @brief 채널 · 날짜의 TTL 색인 키입니다. 시각은 날짜의 끝이라 그 줄이 기간을 넘기면 그 날짜의 기록이 모두 기간을 넘겼다. */
            static string makeGroupIndexKey( string_view channelID, int64 sentMs )
            {
                string target{ channelID };
                target.push_back( '/' );
                return ServiceTTLRule::makeTTLIndexKey( getDayEndMs( sentMs ), target );
            }

            /** @brief 기록 키 `<채널>/<보낸 시각 16진>.<서버>.<순번>` 에서 보낸 시각을 읽습니다. */
            [[nodiscard]] static bool readHistoryTimeMs( const ServiceRecord& record, int64& outTimeMs )
            {
                const size_t slash  = record._key.rfind( '/' );
                const size_t width  = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
                uint64       sentMs = 0;
                if ( slash == string::npos || record._key.size() < slash + 1 + width ||
                     ServiceKeyUtil::parseHex64( string_view( record._key ).substr( slash + 1, width ), sentMs ) == false )
                    return false;
                outTimeMs = static_cast<int64>( sentMs );
                return true;
            }

            static string makeHistoryKey( string_view channelID, int64 sentMs, uint64 serverID, uint64 sequence )
            {
                string key{ channelID };
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( sentMs ) );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, serverID );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, sequence );
                return key;
            }

            static string makeHistoryKey( const ChatMessage& message ) { return makeHistoryKey( message._channelID, message._sentMs, message._serverID, message._sequence ); }

            static bool isClientJoinable( ChatChannelKind kind ) { return kind == ChatChannelKind::World || kind == ChatChannelKind::Custom; }

            static uint16 toMethod( bool bWhisper ) { return bWhisper ? ChatMethod::Whisper::kID : ChatMethod::Send::kID; }
        };

        /** @brief 제재 상태 읽기가 완료로 넘기는 값입니다. */
        struct ChatSanctionResult
        {
            int64 _muteUntilMs{ 0 };
            uint8 _bReadOk{ SW_FALSE };
        };

        /** @brief 기록 쓰기가 완료로 넘기는 값입니다. */
        struct ChatHistoryWriteResult
        {
            int32 _rejectedCount{ 0 }; ///< 기록 레코드를 쓰지 못해 뺀 메시지 수이다
            uint8 _bCommitted{ SW_FALSE };
        };

        /** @brief 기록 읽기가 완료로 넘기는 값입니다. */
        struct ChatHistoryReadResult
        {
            vector<ChatMessage> _listMessage{};
            string              _nextCursor{};
            ChatServiceResult   _result{ ChatServiceResult::fail( OnlineError::kUnavailable ) };
        };

        /** @brief 저장소 스레드에서 도는 채팅 일입니다. 서비스 멤버를 만지지 않고 인자로 받은 값만 씁니다. */
        struct ChatStoreRun
        {
            static ChatSanctionResult readSanction( IServiceStoreConnection& connection, AccountID accountID, UnixMilliseconds nowMs )
            {
                ChatSanctionResult       read;
                ServiceSanctionState     state;
                const ServiceStoreResult result = ServiceSanction::readState( connection, accountID, state );
                if ( result != ServiceStoreResult::Ok )
                    return read;
                if ( ServiceSanction::isActive( state, ServiceSanctionKind::ChatMute, nowMs.count() ) )
                    read._muteUntilMs = state._arrUntilMs[ServiceSanctionKind::ChatMute];
                read._bReadOk = SW_TRUE;
                return read;
            }

            /**
             * @brief 쌓인 기록과 아직 이 서버가 넣지 않은 TTL 색인 줄을 한 트랜잭션으로 씁니다. 색인 줄은 조건 없이 덮어쓴다(서버 여럿이 같은 줄을 넣어도 된다).
             * @details 기록마다 보낸 사람 색인 줄과, 묶음 안의 보낸 사람 · 날짜마다 그 TTL 색인 줄을 함께 넣는다 — 탈퇴 정리가 계정의 기록을 찾는다(쓰기 수는 `update` 가 묶음을 자를 때 잰다).
             */
            static ChatHistoryWriteResult writeHistory( IServiceStoreConnection& connection, const vector<ChatMessage>& listMessage, const vector<string>& listIndexKey )
            {
                ChatHistoryWriteResult write;
                ServiceTransaction     transaction;
                vector<string>         listSenderIndexKey;
                for ( const ChatMessage& message : listMessage )
                {
                    vector<uint8> bytes;
                    if ( ChatProtocol::encodeRecord( message, bytes ) == false )
                    {
                        ++write._rejectedCount; // 쓰기가 오류 로그를 남겼다. 이 기록만 빼고 나머지는 쓴다
                        continue;
                    }
                    const string historyKey = ChatServiceInternal::makeHistoryKey( message );
                    transaction.put( ChatServiceInternal::getHistoryTable(), historyKey, std::move( bytes ) );
                    if ( message._senderID == kInvalidAccountID )
                        continue; // 시스템 메시지다
                    transaction.put( ChatServiceInternal::getSenderTable(), ChatServiceInternal::makeSenderKey( message._senderID, message._sentMs, historyKey ), vector<uint8>{} );
                    const string senderIndexKey = ChatServiceInternal::makeSenderIndexKey( message._senderID, message._sentMs );
                    VectorUtil::addUnique( listSenderIndexKey, senderIndexKey );
                }
                for ( const string& senderIndexKey : listSenderIndexKey )
                {
                    transaction.put( ChatServiceInternal::getSenderIndexTable(), senderIndexKey, vector<uint8>{}, ServiceRecord::kAnyVersion );
                }
                for ( const string& indexKey : listIndexKey )
                {
                    transaction.put( ChatServiceInternal::getHistoryIndexTable(), indexKey, vector<uint8>{}, ServiceRecord::kAnyVersion );
                }
                const bool bCommitted = transaction.isEmpty() || connection.commit( transaction ) == ServiceStoreResult::Ok;
                write._bCommitted     = bCommitted ? SW_TRUE : SW_FALSE;
                return write;
            }

            /** @brief 기록을 최근 것부터 읽습니다. 커서는 앞 응답의 마지막 키이며 그보다 이전 기록부터 읽습니다. 손상된 영속 기록은 건너뛰지 않고 이 읽기를 실패로 돌려준다. */
            static ChatHistoryReadResult readHistory( IServiceStoreConnection& connection, const string& channelID, const string& cursor, int32 maxCount )
            {
                ChatHistoryReadResult                    read;
                vector<ServiceRecord>                    listRecord;
                StringBuilder<constant::buffer::kMax256> channelPrefix;
                channelPrefix.appendFormat( "%#/", channelID );
                if ( ServiceRecordReader::readPage( connection, ChatServiceInternal::getHistoryTable(), channelPrefix.view(), cursor, maxCount, true, listRecord )._result != ServiceStoreResult::Ok )
                    return read;
                for ( const ServiceRecord& record : listRecord )
                {
                    ChatMessage message;
                    if ( ChatProtocol::decodeRecord( record._key, record._bytes, message ) == false )
                    {
                        read._listMessage.clear();
                        return read;
                    }
                    read._listMessage.push_back( std::move( message ) );
                }
                if ( static_cast<int32>( listRecord.size() ) == maxCount )
                    read._nextCursor = listRecord.back()._key;
                read._result = ChatResult::Ok;
                return read;
            }

            /**
             * @brief 신고한 메시지와 그 앞 기록을 읽어 신고 레코드(`ServiceReportStore`)로 남깁니다.
             * @details 기록 키는 고정 폭 16진이라 순번을 하나 올린 키가 신고한 메시지 바로 다음 키입니다. 그 키를 커서로 내림차순으로 읽으면 첫 레코드가 신고한 메시지입니다.
             *          첫 레코드가 그 메시지가 아니면 기록에 아직 없거나 지워진 것이므로 NotFound 입니다. 문맥 줄은 오래된 것부터 담아 마지막 줄이 신고 대상입니다.
             */
            static ChatServiceResult writeReport( IServiceStoreConnection& connection, const ChatReportRequest& request, AccountID reporterID, uint64 uniqueID, UnixMilliseconds nowMs,
                                                  int32 contextCount )
            {
                using Internal                                      = ChatServiceInternal;
                const string                             messageKey = Internal::makeHistoryKey( request._channelID, request._sentMs, request._serverID, request._sequence );
                const string                             cursorKey  = Internal::makeHistoryKey( request._channelID, request._sentMs, request._serverID, static_cast<uint64>( request._sequence ) + 1 );
                vector<ServiceRecord>                    listRecord;
                StringBuilder<constant::buffer::kMax256> channelPrefix;
                channelPrefix.appendFormat( "%#/", request._channelID );
                if ( ServiceRecordReader::readPage( connection, Internal::getHistoryTable(), channelPrefix.view(), cursorKey, contextCount, true, listRecord )._result != ServiceStoreResult::Ok )
                    return ChatServiceResult::fail( OnlineError::kUnavailable );
                if ( listRecord.empty() || listRecord.front()._key != messageKey )
                    return ChatResult::NotFound;
                ServiceReport report;
                for ( auto recordIt = listRecord.rbegin(); recordIt != listRecord.rend(); ++recordIt )
                {
                    ChatMessage message;
                    if ( ChatProtocol::decodeRecord( recordIt->_key, recordIt->_bytes, message ) == false )
                        return ChatServiceResult::fail( OnlineError::kUnavailable ); // 손상된 영속 기록은 건너뛰지 않고 이 신고를 실패로 돌려준다
                    ServiceReportLine& line = report._listLine.emplace_back();
                    line._senderName        = std::move( message._senderName );
                    line._text              = std::move( message._text );
                    line._senderID          = message._senderID;
                    line._sentMs            = message._sentMs;
                }
                report._targetID = report._listLine.back()._senderID;
                if ( report._targetID == reporterID )
                    return ChatServiceResult::fail( OnlineError::kInvalidRequest ); // 자기 메시지는 신고하지 않는다
                report._category   = "chat";
                report._context    = request._channelID;
                report._reason     = request._reason;
                report._reporterID = reporterID;
                report._createdMs  = nowMs.count();
                ServiceTransaction transaction;
                if ( ServiceReportStore::stageOpen( transaction, report, uniqueID ) == false )
                    return ChatServiceResult::fail( OnlineError::kInvalidRequest );
                if ( connection.commit( transaction ) == ServiceStoreResult::Ok )
                    return ChatResult::Ok;
                return ChatServiceResult::fail( OnlineError::kUnavailable );
            }
        };

        /** @brief 색인보다 먼저 쓴 기록의 색인 키입니다(백필). 쓰는 쪽과 같은 채널 · 날짜 줄이라 겹쳐 넣어도 한 줄이다. */
        class ChatHistoryBackfillSource final : public IServiceTTLBackfillSource
        {
        public:
            [[nodiscard]] bool makeIndexKey( IServiceStoreConnection& connection, const ServiceRecord& record, string& outIndexKey ) const override
            {
                (void)connection;
                int64 sentMs = 0;
                if ( ChatServiceInternal::readHistoryTimeMs( record, sentMs ) == false )
                    return false;
                outIndexKey = ChatServiceInternal::makeGroupIndexKey( string_view( record._key ).substr( 0, record._key.rfind( '/' ) ), sentMs );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ChatService::ChatService()
        : _mapAccountToMember{}
        , _mapChannelToMember{}
        , _listHistoryQueue{}
        , _completionBuffer{}
        , _deliveryBuffer{}
        , _busTopicBuffer{}
        , _wordFilter{}
        , _spamGuard{}
        , _reportGuard{}
        , _settings{}
        , _dependencies{}
        , _presenceFinds{}
        , _serverTopic{}
        , _sequence{ 0 }
        , _reportSequence{ 0 }
        , _store{}
        , _uniqueIndexedGroupKeys{}
        , _indexedDayEndMs{ 0 }
        , _bHistoryWriting{ SW_FALSE }
    {
    }

    ChatService::~ChatService()
    {
        shutdown();
    }

    bool ChatService::initialize( const ChatServiceDependencies& dependencies, const ChatServiceConfig& settings )
    {
        if ( dependencies._pStore == nullptr || dependencies._pDirectory == nullptr )
        {
            SW_LOG_ERROR_ID( "ChatService.ChatServiceNeedsServiceStore", "ChatService needs a service store and an account directory" );
            return false;
        }
        if ( settings._spam.isComplete() == false )
        {
            SW_LOG_ERROR_ID( "ChatService.ChatServiceNoSpamLimit", "ChatService needs the spam limits (_spam in the chat kit settings)" );
            return false;
        }
        const bool bReportContextOk = 0 < settings._reportContextCount && settings._reportContextCount <= ServiceReportConstant::kMaxLineCount;
        const bool bReportRateOk    = settings._reportBurst > 0 && settings._reportRefillIntervalMs > 0 && settings._sanctionCacheMs > 0;
        if ( settings._maxChannelPerMember <= 0 || bReportContextOk == false || bReportRateOk == false )
        {
            SW_LOG_ERROR_ID( "ChatService.ChatServiceNoLimits", "ChatService needs _maxChannelPerMember above zero (%#), _reportContextCount in 1..%# (%#), and _reportBurst (%#), _reportRefillIntervalMs (%#), _sanctionCacheMs (%#) above zero from the chat kit settings",
                             settings._maxChannelPerMember, static_cast<int32>( ServiceReportConstant::kMaxLineCount ), settings._reportContextCount, settings._reportBurst, settings._reportRefillIntervalMs,
                             settings._sanctionCacheMs );
            return false;
        }
        _dependencies = dependencies;
        _store.bindStore( dependencies._pStore );
        _presenceFinds.bind( dependencies._pBus != nullptr ? dependencies._pPresence : nullptr ); // 버스가 없으면 다른 서버로 보낼 길이 없다
        _settings    = settings;
        _serverTopic = ChatProtocol::makeServerTopic( dependencies._serverID );
        // 정규화(건너뛸 글자)는 금칙어 거르개와 도배 방지기가 함께 쓴다 — 데이터라 없으면 서비스가 서지 않는다.
        ChatTextNormalizer normalizer;
        if ( normalizer.loadFromFile( settings._ignoredCharactersPath ) == false )
            return false;
        _spamGuard.initialize( settings._spam, normalizer );
        _reportGuard.initialize( settings._reportBurst, settings._reportRefillIntervalMs, static_cast<size_t>( settings._spam._maxTrackedAccount ) );
        // 금칙어 파일을 고른 서버가 그것을 못 읽으면 서지 않는다 - 예전에는 경고만 남기고 거르지 않은 채 열렸다(fail-open). 거르지 않으려면 옵션을 'none' 으로 밝힌다.
        if ( settings._bannedWordsPath.empty() == false && _wordFilter.loadFromFile( settings._bannedWordsPath, settings._filterMode, normalizer ) == false )
        {
            SW_LOG_ERROR_ID( "ChatService.ChatServiceBannedWordFile", "ChatService: banned word file '%#' could not be read - the chat service does not start (leave _bannedWordsFile empty to run unfiltered)", settings._bannedWordsPath.str() );
            return false;
        }
        if ( dependencies._pBus != nullptr )
        {
            _busTopicBuffer.push( ChatBusTopicChange{ _serverTopic, SW_TRUE } );                                          // 다른 서버가 보낸 귓속말을 받는다
            _busTopicBuffer.push( ChatBusTopicChange{ string( ServiceSanctionBus::kChangedTopic.getName() ), SW_TRUE } ); // GM 이 제재를 바꿨을 때 받는다. 자기 서버가 낸 것도 포함한다
            _busTopicBuffer.push( ChatBusTopicChange{ string( ChatBus::kMemberRemovalTopic.getName() ), SW_TRUE } );      // 다른 서버가 그룹 채널에서 뺀 계정을 이 서버의 멤버에서도 뺀다
        }
        return true;
    }

    void ChatService::shutdown()
    {
        _store.flushUntilIdle();
        if ( _store.isIdle() == false )
            SW_LOG_ERROR_ID( "ChatService.ChatServiceShutDownStore", "ChatService shut down with %# store works still pending", _store.getOpenCount() );
        // 맡긴 찾기를 회수한다(기다리던 귓속말은 답 없이 버린다). 접속 상태 관리자가 이 객체보다 오래 살아도 이 객체를 부르지 않게 하려는 것이다. 저장 작업을 회수한 뒤에 한다. 그 완료가 찾기를 낼 수 있기 때문이다
        _presenceFinds.unbind();
        _mapAccountToMember.clear();
        _mapChannelToMember.clear();
        _listHistoryQueue.clear();
        _dependencies = ChatServiceDependencies{};
        _store.bindStore( nullptr );
    }

    int32 ChatService::getLocalMemberCount( string_view channelID ) const
    {
        const auto channelIt = _mapChannelToMember.find( string( channelID ) );
        return channelIt != _mapChannelToMember.end() ? static_cast<int32>( channelIt->second.size() ) : 0;
    }

    ChatService::Member* ChatService::ensureMember( AccountID accountID, UnixMilliseconds nowMs )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt != _mapAccountToMember.end() )
            return &memberIt->second;
        AccountIdentity identity;
        if ( _dependencies._pDirectory == nullptr || _dependencies._pDirectory->findIdentity( accountID, identity ) == false )
            return nullptr; // 이 서버에 접속하지 않은 계정이다. 호스트가 인증했다면 생기지 않는다
        Member& member      = _mapAccountToMember[accountID];
        member._displayName = identity._displayName;
        startSanctionRead( accountID, member, nowMs );
        return &member;
    }

    bool ChatService::isSanctionFresh( const Member& member, UnixMilliseconds nowMs ) const
    {
        return member._sanctionReadMs >= 0 && nowMs.count() - member._sanctionReadMs < _settings._sanctionCacheMs;
    }

    void ChatService::startSanctionRead( AccountID accountID, Member& member, UnixMilliseconds nowMs )
    {
        if ( member._bSanctionReading == SW_TRUE )
            return;
        member._bSanctionReading = SW_TRUE;
        _store.submit( [accountID, nowMs]( IServiceStoreConnection& connection )
        { return ChatStoreRun::readSanction( connection, accountID, nowMs ); },
                       [this, accountID, nowMs]( const ChatSanctionResult& read )
        { applySanction( accountID, read._muteUntilMs, read._bReadOk == SW_TRUE, nowMs ); } );
    }

    void ChatService::applySanction( AccountID accountID, int64 muteUntilMs, bool bReadOk, UnixMilliseconds nowMs )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt == _mapAccountToMember.end() )
            return; // 그사이 떠났다
        Member& member           = memberIt->second;
        member._bSanctionReading = SW_FALSE;
        if ( member._bSanctionChanged == SW_TRUE )
        {
            member._bSanctionChanged = SW_FALSE;
            startSanctionRead( accountID, member, nowMs ); // 기다리던 말은 새 값을 읽은 뒤에 처리한다
            return;
        }
        if ( bReadOk )
        {
            member._muteUntilMs    = muteUntilMs;
            member._sanctionReadMs = nowMs.count();
        }
        vector<PendingSend> listPending;
        listPending.swap( member._listPendingSend );
        for ( const PendingSend& send : listPending )
        {
            if ( member._sanctionReadMs < 0 )
            {
                completeSimple( ChatServiceInternal::toMethod( send._recipientID != kInvalidAccountID ), send._requestTag, ChatServiceResult::fail( OnlineError::kUnavailable ) );
                continue;
            }
            processSend( accountID, member, send );
        }
    }

    void ChatService::refreshSanction( AccountID accountID )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt == _mapAccountToMember.end() )
            return;
        memberIt->second._sanctionReadMs   = constant::invalid_index::kInt32;
        memberIt->second._bSanctionChanged = memberIt->second._bSanctionReading;
    }

    void ChatService::completeSimple( uint16 method, uint64 requestTag, ChatServiceResult result, int64 retryAfterMs )
    {
        ChatCompletion completion;
        completion._method       = method;
        completion._requestTag   = requestTag;
        completion._result       = result;
        completion._retryAfterMs = retryAfterMs;
        _completionBuffer.push( std::move( completion ) );
    }

    void ChatService::completeMessage( uint16 method, uint64 requestTag, ChatMessage&& message )
    {
        ChatCompletion completion;
        completion._method     = method;
        completion._requestTag = requestTag;
        completion._message    = std::move( message );
        _completionBuffer.push( std::move( completion ) );
    }

    void ChatService::addLocalMember( AccountID accountID, Member& member, const string& channelID )
    {
        member._listChannel.push_back( channelID );
        vector<AccountID>& listMember = _mapChannelToMember[channelID];
        listMember.push_back( accountID );
        if ( listMember.size() == 1 && _dependencies._pBus != nullptr )
            _busTopicBuffer.push( ChatBusTopicChange{ ChatProtocol::makeChannelTopic( channelID ), SW_TRUE } );
    }

    void ChatService::removeLocalMember( AccountID accountID, Member& member, const string& channelID )
    {
        VectorUtil::removeValue( member._listChannel, channelID );
        const auto channelIt = _mapChannelToMember.find( channelID );
        if ( channelIt == _mapChannelToMember.end() )
            return;
        vector<AccountID>& listMember = channelIt->second;
        VectorUtil::removeValue( listMember, accountID );
        if ( listMember.empty() == false )
            return;
        _mapChannelToMember.erase( channelIt );
        if ( _dependencies._pBus != nullptr )
            _busTopicBuffer.push( ChatBusTopicChange{ ChatProtocol::makeChannelTopic( channelID ), SW_FALSE } );
    }

    ChatServiceResult ChatService::addMember( AccountID accountID, string_view channelID, UnixMilliseconds nowMs )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false || kind == ChatChannelKind::Whisper )
            return ChatServiceResult::fail( OnlineError::kInvalidRequest );
        Member* pMember = ensureMember( accountID, nowMs );
        if ( pMember == nullptr )
            return ChatResult::TargetOffline;
        const string id( channelID );
        if ( VectorUtil::contains( pMember->_listChannel, id ) )
            return ChatResult::Ok;
        if ( static_cast<int32>( pMember->_listChannel.size() ) >= _settings._maxChannelPerMember )
            return ChatResult::TooManyChannels;
        addLocalMember( accountID, *pMember, id );
        return ChatResult::Ok;
    }

    void ChatService::removeMember( AccountID accountID, string_view channelID )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt != _mapAccountToMember.end() )
            removeLocalMember( accountID, memberIt->second, string( channelID ) );
    }

    void ChatService::removeGroupMember( AccountID accountID, string_view channelID )
    {
        removeMember( accountID, channelID );
        if ( _dependencies._pBus == nullptr )
            return;
        ChatMemberRemoval removal;
        removal._channelID = string( channelID );
        removal._accountID = accountID;
        (void)ChatBus::kMemberRemovalTopic.publish( *_dependencies._pBus, removal ); // 실패는 발행이 로그에 남겼다 — 그 서버의 멤버는 계정이 떠날 때 빠진다
    }

    void ChatService::removeAccount( AccountID accountID )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt == _mapAccountToMember.end() )
            return;
        const vector<string> listChannel = memberIt->second._listChannel;
        for ( const string& channelID : listChannel )
        {
            removeLocalMember( accountID, memberIt->second, channelID );
        }
        for ( const PendingSend& send : memberIt->second._listPendingSend )
        {
            completeSimple( ChatServiceInternal::toMethod( send._recipientID != kInvalidAccountID ), send._requestTag, ChatServiceResult::fail( OnlineError::kUnavailable ) );
        }
        _mapAccountToMember.erase( memberIt );
        _spamGuard.remove( accountID );
    }

    void ChatService::joinChannel( AccountID accountID, string_view channelID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false )
        {
            completeSimple( ChatMethod::Join::kID, requestTag, ChatServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        if ( ChatServiceInternal::isClientJoinable( kind ) == false )
        {
            completeSimple( ChatMethod::Join::kID, requestTag, ChatResult::NotJoinable );
            return;
        }
        completeSimple( ChatMethod::Join::kID, requestTag, addMember( accountID, channelID, nowMs ) );
    }

    void ChatService::leaveChannel( AccountID accountID, string_view channelID, uint64 requestTag )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false || ChatServiceInternal::isClientJoinable( kind ) == false )
        {
            completeSimple( ChatMethod::Leave::kID, requestTag, ChatResult::NotJoinable );
            return;
        }
        removeMember( accountID, channelID );
        completeSimple( ChatMethod::Leave::kID, requestTag, ChatResult::Ok );
    }

    void ChatService::sendMessage( AccountID accountID, string_view channelID, string_view text, UnixMilliseconds nowMs, uint64 requestTag )
    {
        submitSend( accountID, PendingSend{ string( channelID ), string( text ), requestTag, nowMs, kInvalidAccountID } );
    }

    void ChatService::sendWhisper( AccountID accountID, AccountID recipientID, string_view text, UnixMilliseconds nowMs, uint64 requestTag )
    {
        if ( recipientID == kInvalidAccountID || recipientID == accountID )
        {
            completeSimple( ChatMethod::Whisper::kID, requestTag, ChatServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        submitSend( accountID, PendingSend{ ChatChannelID::makeWhisper( accountID, recipientID ), string( text ), requestTag, nowMs, recipientID } );
    }

    void ChatService::submitSend( AccountID accountID, PendingSend&& send )
    {
        const uint16 method  = ChatServiceInternal::toMethod( send._recipientID != kInvalidAccountID );
        Member*      pMember = ensureMember( accountID, send._nowMs );
        if ( pMember == nullptr )
        {
            completeSimple( method, send._requestTag, ChatResult::NotMember );
            return;
        }
        if ( pMember->_sanctionReadMs < 0 )
        {
            if ( static_cast<int32>( pMember->_listPendingSend.size() ) >= _settings._maxPendingSendPerMember )
            {
                completeSimple( method, send._requestTag, ChatResult::RateLimited, _settings._pendingSendRetryAfterMs );
                return;
            }
            const UnixMilliseconds nowMs = send._nowMs;
            pMember->_listPendingSend.push_back( std::move( send ) );
            startSanctionRead( accountID, *pMember, nowMs );
            return;
        }
        if ( isSanctionFresh( *pMember, send._nowMs ) == false )
            startSanctionRead( accountID, *pMember, send._nowMs ); // 캐시된 이전 값으로 지금 처리하고, 뒤에서 새로 읽는다
        processSend( accountID, *pMember, send );
    }

    ChatServiceResult ChatService::prepareText( AccountID accountID, Member& member, string_view text, UnixMilliseconds nowMs, string& outText, int64& outRetryAfterMs )
    {
        outRetryAfterMs = 0;
        if ( text.empty() || text.size() > static_cast<size_t>( ChatLimit::kMaxTextSize ) )
            return ChatServiceResult::fail( OnlineError::kInvalidRequest );
        if ( member._muteUntilMs != 0 && nowMs.count() < member._muteUntilMs )
        {
            outRetryAfterMs = member._muteUntilMs - nowMs.count();
            return ChatResult::Muted;
        }
        const ChatSpamResult spam = _spamGuard.check( accountID, text, nowMs.count(), outRetryAfterMs );
        if ( spam == ChatSpamResult::RateLimited )
            return ChatResult::RateLimited;
        if ( spam == ChatSpamResult::Repeated )
            return ChatResult::Repeated;
        switch ( _wordFilter.apply( text, outText ) )
        {
            case ChatFilterResult::Rejected:
                return ChatResult::Rejected;
            case ChatFilterResult::InvalidText:
                return ChatServiceResult::fail( OnlineError::kInvalidRequest );
            default:
                return ChatResult::Ok;
        }
    }

    void ChatService::processSend( AccountID accountID, Member& member, const PendingSend& send )
    {
        const bool      bWhisper = send._recipientID != kInvalidAccountID;
        const uint16    method   = ChatServiceInternal::toMethod( bWhisper );
        ChatChannelKind kind     = ChatChannelKind::Whisper;
        if ( bWhisper == false )
        {
            const bool bMember = VectorUtil::contains( member._listChannel, send._channelID );
            if ( ChatChannelID::parseKind( send._channelID, kind ) == false || bMember == false )
            {
                completeSimple( method, send._requestTag, ChatResult::NotMember );
                return;
            }
        }
        string                  filtered;
        int64                   retryAfterMs = 0;
        const ChatServiceResult prepared     = prepareText( accountID, member, send._text, send._nowMs, filtered, retryAfterMs );
        if ( prepared != ChatResult::Ok )
        {
            completeSimple( method, send._requestTag, prepared, retryAfterMs );
            return;
        }

        ChatMessage message;
        message._channelID   = send._channelID;
        message._kind        = kind;
        message._senderID    = accountID;
        message._senderName  = member._displayName;
        message._recipientID = send._recipientID;
        message._text        = std::move( filtered );
        message._sentMs      = send._nowMs.count();
        message._serverID    = _dependencies._serverID;
        message._sequence    = ++_sequence;

        if ( bWhisper == false )
        {
            deliverToChannel( message ); // 보낸 사람도 알림으로 받는다. 되울림과는 별개이며 다른 창이 같은 채널을 보고 있을 수 있기 때문이다
            publishMessage( ChatBus::kChannelTopic, message._channelID, message );
            queueHistory( message );
            completeMessage( method, send._requestTag, std::move( message ) );
            return;
        }
        if ( _dependencies._pDirectory->isAccountOnline( send._recipientID ) )
        {
            deliverWhisperLocally( message );
            queueHistory( message );
            completeMessage( method, send._requestTag, std::move( message ) );
            return;
        }
        // 다른 서버에 있으면 접속 상태로 서버를 찾는다. 답은 항상 나중에 온다(접속 상태 · 버스가 없으면 맡지 않는다)
        const uint64 requestTag = send._requestTag;
        const bool   bFinding   = _presenceFinds.findByAccount( send._recipientID, accountID, [this, message = std::move( message ), requestTag]( const AccountPresenceResult& result ) mutable
            { handleWhisperPresenceResult( result, std::move( message ), requestTag ); } );
        if ( bFinding == false )
            completeSimple( method, requestTag, ChatResult::TargetOffline );
    }

    void ChatService::handleWhisperPresenceResult( const AccountPresenceResult& result, ChatMessage&& message, uint64 requestTag )
    {
        if ( result.isOnline() == false || result._serverID == _dependencies._serverID )
        {
            completeSimple( ChatMethod::Whisper::kID, requestTag, ChatResult::TargetOffline ); // 이 서버였다면 그사이 떠난 것이다
            return;
        }
        publishMessage( ChatBus::kServerTopic, ServiceKeyUtil::makeHex64( result._serverID ), message );
        queueHistory( message );
        completeMessage( ChatMethod::Whisper::kID, requestTag, std::move( message ) ); // 받는 서버가 차단으로 버려도 보낸 사람에게는 보낸 것으로 보인다
    }

    void ChatService::publishMessage( const ServerBusTopicFamily<ChatMessage>& family, string_view suffix, const ChatMessage& message )
    {
        if ( _dependencies._pBus == nullptr )
            return;
        (void)family.publish( *_dependencies._pBus, suffix, message ); // 실패는 발행이 오류 로그로 남겼다. 기록 레코드가 원본이다
    }

    void ChatService::deliverToChannel( const ChatMessage& message )
    {
        const auto channelIt = _mapChannelToMember.find( message._channelID );
        if ( channelIt == _mapChannelToMember.end() )
            return;
        for ( const AccountID recipientID : channelIt->second )
        {
            const bool bSkipBlockedRecipient = _dependencies._pPolicy != nullptr && recipientID != message._senderID && _dependencies._pPolicy->isBlocked( recipientID, message._senderID );
            if ( bSkipBlockedRecipient )
                continue; // 막은 사람의 채널 말은 받는 쪽에게 보이지 않는다
            _deliveryBuffer.push( ChatDelivery{ message, recipientID } );
        }
    }

    void ChatService::deliverWhisperLocally( const ChatMessage& message )
    {
        if ( _dependencies._pPolicy != nullptr && _dependencies._pPolicy->isBlocked( message._recipientID, message._senderID ) )
            return;
        _deliveryBuffer.push( ChatDelivery{ message, message._recipientID } );
    }

    void ChatService::handleBusMessage( const ServerBusMessage& busMessage )
    {
        if ( ServiceSanctionBus::kChangedTopic.matches( busMessage ) )
        {
            ServiceSanctionChanged changed;
            if ( ServiceSanctionBus::kChangedTopic.read( busMessage, changed ) == ServerBusReadResult::Read )
                refreshSanction( changed._accountID ); // 못 읽으면 묵힘 시한 안에 다시 읽는다
            return;
        }
        if ( ChatBus::kMemberRemovalTopic.matches( busMessage ) )
        {
            ChatMemberRemoval removal;
            if ( ChatBus::kMemberRemovalTopic.read( busMessage, removal ) == ServerBusReadResult::Read )
                removeMember( removal._accountID, removal._channelID ); // 자기 서버가 낸 것도 받는다 — 이미 뺐으면 아무것도 하지 않는다
            return;
        }
        ChatMessage               message;
        const bool                bChannelTopic = ChatBus::kChannelTopic.matches( busMessage );
        const ServerBusReadResult result        = bChannelTopic ? ChatBus::kChannelTopic.read( busMessage, message ) : ChatBus::kServerTopic.read( busMessage, message );
        if ( result != ServerBusReadResult::Read || message._serverID == _dependencies._serverID )
            return;
        if ( message._kind == ChatChannelKind::Whisper )
        {
            const bool bDeliverWhisper = busMessage.getTopic() == _serverTopic && _dependencies._pDirectory != nullptr && _dependencies._pDirectory->isAccountOnline( message._recipientID );
            if ( bDeliverWhisper )
                deliverWhisperLocally( message );
            return;
        }
        if ( busMessage.getTopic() == ChatProtocol::makeChannelTopic( message._channelID ) )
            deliverToChannel( message );
    }

    void ChatService::queueHistory( const ChatMessage& message )
    {
        if ( static_cast<int32>( _listHistoryQueue.size() ) >= _settings._maxQueuedHistory )
        {
            SW_LOG_WARNING_ID( "ChatService.ChatServiceChatHistoryQueue", "ChatService: chat history queue is full (%#) - dropping the oldest record", _listHistoryQueue.size() );
            _listHistoryQueue.pop_front();
        }
        _listHistoryQueue.push_back( message );
    }

    void ChatService::update( UnixMilliseconds nowMs )
    {
        const bool bNothingToFlush = _store.hasStore() == false || _bHistoryWriting == SW_TRUE || _listHistoryQueue.empty();
        if ( bNothingToFlush )
            return;
        const int64 dayEndMs = ChatServiceInternal::getDayEndMs( nowMs.count() );
        if ( dayEndMs != _indexedDayEndMs )
        {
            _uniqueIndexedGroupKeys.clear(); // 지난 날짜의 줄은 이미 넣었다. 늦게 온 지난 날짜 기록은 줄을 다시 넣어도 된다(덮어쓴다)
            _indexedDayEndMs = dayEndMs;
        }
        // 기록 하나는 쓰기 둘(기록 · 보낸 사람 색인), 이 서버가 아직 넣지 않은 채널 · 날짜는 색인 줄 하나, 묶음 안의 보낸 사람 · 날짜마다 그 TTL 색인 줄 하나가 더 든다.
        // 합이 트랜잭션 상한을 넘지 않게 자른다.
        vector<string> listIndexKey;
        vector<string> listSenderIndexKey;
        size_t         takeCount = 0;
        while ( takeCount < _listHistoryQueue.size() && takeCount < static_cast<size_t>( _settings._historyFlushMax ) )
        {
            const ChatMessage& message    = _listHistoryQueue[takeCount];
            const string       indexKey   = ChatServiceInternal::makeGroupIndexKey( message._channelID, message._sentMs );
            const bool         bNewGroup  = _uniqueIndexedGroupKeys.count( indexKey ) == 0 && VectorUtil::contains( listIndexKey, indexKey ) == false;
            const string       senderKey  = message._senderID == kInvalidAccountID ? string() : ChatServiceInternal::makeSenderIndexKey( message._senderID, message._sentMs );
            const bool         bNewSender = senderKey.empty() == false && VectorUtil::contains( listSenderIndexKey, senderKey ) == false;
            const size_t       writeCount = ( takeCount + 1 ) * 2 + listIndexKey.size() + listSenderIndexKey.size() + ( bNewGroup ? 1 : 0 ) + ( bNewSender ? 1 : 0 );
            if ( writeCount > static_cast<size_t>( ServiceTransaction::kMaxWriteCount ) )
                break;
            if ( bNewGroup )
                listIndexKey.push_back( indexKey );
            if ( bNewSender )
                listSenderIndexKey.push_back( senderKey );
            ++takeCount;
        }
        vector<ChatMessage> listMessage;
        listMessage.reserve( takeCount );
        for ( size_t index = 0; index < takeCount; ++index )
        {
            listMessage.push_back( std::move( _listHistoryQueue.front() ) );
            _listHistoryQueue.pop_front();
        }
        _bHistoryWriting         = SW_TRUE;
        const int32 messageCount = static_cast<int32>( listMessage.size() );
        _store.submit( [listMessage = std::move( listMessage ), listIndexKey]( IServiceStoreConnection& connection )
        { return ChatStoreRun::writeHistory( connection, listMessage, listIndexKey ); },
                       [this, messageCount, listIndexKey]( const ChatHistoryWriteResult& write, ServiceStoreCompletion completion )
        { applyHistoryWrite( write._bCommitted == SW_TRUE, messageCount - write._rejectedCount, write._rejectedCount, listIndexKey, completion ); } );
    }

    ServiceTTLRule ChatService::makeTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = ChatServiceInternal::getHistoryTable();
        rule._ttlIndexTable   = ChatServiceInternal::getHistoryIndexTable();
        rule._expireAfterMs   = expireAfterMs;
        rule._pfnReadTime     = &ChatServiceInternal::readHistoryTimeMs;
        rule._pBackfillSource = make_shared<ChatHistoryBackfillSource>();
        rule._backfillVersion = 0;
        return rule;
    }

    ServiceTTLRule ChatService::makeSenderTTLRule( int64 expireAfterMs )
    {
        // 백필은 없다 — 색인보다 먼저 쓴 기록은 보낸 사람 줄이 없고, 기록 TTL(같은 기간)이 지운다
        ServiceTTLRule rule;
        rule._table         = ChatServiceInternal::getSenderTable();
        rule._ttlIndexTable = ChatServiceInternal::getSenderIndexTable();
        rule._expireAfterMs = expireAfterMs;
        rule._pfnReadTime   = &ChatServiceInternal::readSenderTimeMs;
        return rule;
    }

    ServiceStoreResult ChatService::eraseAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs )
    {
        (void)nowMs;
        using Internal         = ChatServiceInternal;
        const string    prefix = Internal::makeSenderPrefix( accountID );
        const size_t    width  = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        constexpr int32 kPage  = 20; ///< 줄 하나에 지우기 셋(색인 · 기록 · TTL 색인 줄)까지라 트랜잭션 상한 안이다
        static_assert( kPage * 3 <= ServiceTransaction::kMaxWriteCount, "a page of sender rows must fit one transaction" );
        vector<string> listWhisperChannel;
        while ( true )
        {
            vector<ServiceRecord>    listSender;
            const ServiceStoreResult listed = ServiceRecordReader::readPage( connection, Internal::getSenderTable(), prefix, "", kPage, false, listSender )._result;
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            if ( listSender.empty() )
                break;
            ServiceTransaction transaction;
            vector<string>     listSenderIndexKey; // 같은 날짜의 줄이 쪽 안에 여럿이어도 TTL 색인 줄은 한 번만 지운다(같은 키를 두 번 쓰는 트랜잭션은 거절된다)
            for ( const ServiceRecord& record : listSender )
            {
                transaction.erase( Internal::getSenderTable(), record._key, record._version );
                int64 sentMs = 0;
                if ( record._key.size() <= prefix.size() + width + 1 || Internal::readSenderTimeMs( record, sentMs ) == false )
                    continue; // 깨진 색인 줄이다 — 지우기만 한다
                const string historyKey = record._key.substr( prefix.size() + width + 1 );
                transaction.erase( Internal::getHistoryTable(), historyKey );
                const string senderIndexKey = Internal::makeSenderIndexKey( accountID, sentMs );
                if ( VectorUtil::contains( listSenderIndexKey, senderIndexKey ) == false )
                {
                    transaction.erase( Internal::getSenderIndexTable(), senderIndexKey );
                    listSenderIndexKey.push_back( senderIndexKey );
                }
                const string channelID = historyKey.substr( 0, historyKey.rfind( '/' ) );
                if ( StringUtil::startsWith( channelID, Internal::kWhisperPrefix ) && VectorUtil::contains( listWhisperChannel, channelID ) == false )
                    listWhisperChannel.push_back( channelID );
            }
            if ( const ServiceStoreResult committed = connection.commit( transaction ); committed != ServiceStoreResult::Ok )
                return committed;
        }
        // 귓속말 채널은 id 에 두 계정이 있다 — 상대가 보낸 기록까지 채널째 지운다. 그 채널의 TTL 색인 줄은 대상이 비어 TTL 쓸기가 지운다
        for ( const string& channelID : listWhisperChannel )
        {
            StringBuilder<constant::buffer::kMax256> channelPrefix;
            channelPrefix.appendFormat( "%#/", channelID );
            if ( const ServiceStoreResult erased = ServiceStoreUtil::erasePrefix( connection, Internal::getHistoryTable(), channelPrefix.view() ); erased != ServiceStoreResult::Ok )
                return erased;
        }
        return ServiceStoreResult::Ok;
    }

    void ChatService::applyHistoryWrite( bool bCommitted, int32 messageCount, int32 rejectedCount, const vector<string>& listIndexKey, ServiceStoreCompletion completion )
    {
        _bHistoryWriting = SW_FALSE;
        if ( bCommitted )
        {
            // 커밋한 줄만 넣었다고 적는다 - 실패한 쓰기의 줄을 적으면 같은 날짜의 다음 기록이 색인 없이 남는다
            for ( const string& indexKey : listIndexKey )
            {
                _uniqueIndexedGroupKeys.insert( indexKey );
            }
        }
        if ( bCommitted == false && completion.isCancelled() == false ) // 종료 때문에 취소된 쓰기는 경고하지 않는다(기록은 어느 쪽이든 버려진다)
            SW_LOG_WARNING_ID( "ChatService.ChatServiceChatHistoryRecords", "ChatService: %# chat history records were not stored (store unavailable) - they are dropped", messageCount );
        if ( rejectedCount > 0 )
            SW_LOG_ERROR_ID( "ChatService.ChatServiceChatHistoryRecords2", "ChatService: %# chat history records could not be written - they are dropped", rejectedCount );
    }

    bool ChatService::isWhisperParticipant( AccountID accountID, string_view channelID ) const
    {
        // 기록 키는 `whisper.<작은 계정 16진>.<큰 계정 16진>` 형식이며, 두 id 중 하나가 나여야 한다
        const size_t prefixSize = string_view( ChatServiceInternal::kWhisperPrefix ).size();
        const size_t hexWidth   = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        if ( channelID.size() != prefixSize + hexWidth + 1 + hexWidth || channelID[prefixSize + hexWidth] != '.' )
            return false;
        uint64 low  = 0;
        uint64 high = 0;
        if ( ServiceKeyUtil::parseHex64( channelID.substr( prefixSize, hexWidth ), low ) == false ||
             ServiceKeyUtil::parseHex64( channelID.substr( prefixSize + hexWidth + 1, hexWidth ), high ) == false )
            return false;
        return low == accountID || high == accountID;
    }

    void ChatService::readHistory( AccountID accountID, string_view channelID, string_view cursor, int32 maxCount, uint64 requestTag )
    {
        ChatChannelKind kind                   = ChatChannelKind::World;
        const bool      bInvalidHistoryRequest = ChatChannelID::parseKind( channelID, kind ) == false || maxCount < 1 || maxCount > ChatLimit::kMaxHistoryPage ||
                                            cursor.size() > static_cast<size_t>( ChatProtocol::kMaxCursorSize );
        if ( bInvalidHistoryRequest )
        {
            completeSimple( ChatMethod::History::kID, requestTag, ChatServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        if ( canReadChannel( accountID, channelID, kind ) == false )
        {
            completeSimple( ChatMethod::History::kID, requestTag, ChatResult::NotMember );
            return;
        }
        _store.submit( [channel = string( channelID ), from = string( cursor ), maxCount]( IServiceStoreConnection& connection )
        { return ChatStoreRun::readHistory( connection, channel, from, maxCount ); },
                       [this, accountID, requestTag]( ChatHistoryReadResult& read )
        { applyHistoryRead( accountID, requestTag, read._result, std::move( read._listMessage ), std::move( read._nextCursor ) ); } );
    }

    void ChatService::applyHistoryRead( AccountID readerID, uint64 requestTag, ChatServiceResult result, vector<ChatMessage>&& listMessage, string&& nextCursor )
    {
        if ( _dependencies._pPolicy != nullptr )
        {
            VectorUtil::removeIf( listMessage, [this, readerID]( const ChatMessage& message )
            { return message._senderID != readerID && _dependencies._pPolicy->isBlocked( readerID, message._senderID ); } ); // 쪽은 짧아질 수 있다 — 다음 쪽 커서는 읽은 그대로다
        }
        ChatCompletion completion;
        completion._method      = ChatMethod::History::kID;
        completion._requestTag  = requestTag;
        completion._result      = result;
        completion._listHistory = std::move( listMessage );
        completion._nextCursor  = std::move( nextCursor );
        _completionBuffer.push( std::move( completion ) );
    }

    bool ChatService::canReadChannel( AccountID accountID, string_view channelID, ChatChannelKind kind ) const
    {
        if ( kind == ChatChannelKind::Whisper )
            return isWhisperParticipant( accountID, channelID );
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt == _mapAccountToMember.end() )
            return false;
        const vector<string>& listChannel = memberIt->second._listChannel;
        return VectorUtil::contains( listChannel, channelID );
    }

    void ChatService::reportMessage( AccountID accountID, const ChatReportRequest& request, UnixMilliseconds nowMs, uint64 requestTag )
    {
        ChatChannelKind kind           = ChatChannelKind::World;
        const bool      bInvalidReport = ChatChannelID::parseKind( request._channelID, kind ) == false || request._sentMs < 0 ||
                                    ServiceReportStore::isValidCode( request._reason, ChatLimit::kMaxReasonSize ) == false;
        if ( bInvalidReport )
        {
            completeSimple( ChatMethod::Report::kID, requestTag, ChatServiceResult::fail( OnlineError::kInvalidRequest ) );
            return;
        }
        if ( canReadChannel( accountID, request._channelID, kind ) == false )
        {
            completeSimple( ChatMethod::Report::kID, requestTag, ChatResult::NotMember );
            return;
        }
        int64 retryAfterMs = 0;
        if ( _reportGuard.tryConsume( accountID, nowMs.count(), retryAfterMs ) == false )
        {
            completeSimple( ChatMethod::Report::kID, requestTag, ChatResult::RateLimited, retryAfterMs );
            return;
        }
        ++_reportSequence;
        const uint64 uniqueID = ( _dependencies._serverID << 32 ) | _reportSequence;
        _store.submit( [request, accountID, uniqueID, nowMs, contextCount = _settings._reportContextCount]( IServiceStoreConnection& connection )
        { return ChatStoreRun::writeReport( connection, request, accountID, uniqueID, nowMs, contextCount ); },
                       [this, requestTag]( ChatServiceResult result )
        { applyReport( requestTag, result ); } );
    }

    void ChatService::applyReport( uint64 requestTag, ChatServiceResult result )
    {
        completeSimple( ChatMethod::Report::kID, requestTag, result );
    }
} // namespace sw
