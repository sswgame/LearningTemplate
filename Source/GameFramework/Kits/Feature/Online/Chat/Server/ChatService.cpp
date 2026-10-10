#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ChatServiceInternal
        {
            static constexpr int64       kShutdownWaitMs = 5000;
            static constexpr int32       kTrimBatchCount = 64; ///< 정리 한 번에 지우는 지난 기록
            static constexpr const utf8* kWhisperPrefix  = "whisper.";

            static const hashed_string& getHistoryTable()
            {
                static const hashed_string s_table{ "chat_history" };
                return s_table;
            }

            static string makeHistoryKey( const ChatMessage& message )
            {
                string key = message._channelID;
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, static_cast<uint64>( message._sentMs ) );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, message._serverID );
                key.push_back( '.' );
                ServiceKeyUtil::appendHex64( key, message._sequence );
                return key;
            }

            static bool isClientJoinable( ChatChannelKind kind ) { return kind == ChatChannelKind::World || kind == ChatChannelKind::Custom; }

            static uint16 toMethod( bool bWhisper ) { return bWhisper ? ChatMethod::kWhisper : ChatMethod::kSend; }
        };

        /** @brief 제재 상태를 읽는 일입니다. */
        class ChatSanctionWork final : public IServiceStoreWork
        {
        public:
            ChatSanctionWork( ChatService* pService, AccountID accountID, int64 nowMs )
                : _pService{ pService }
                , _accountID{ accountID }
                , _nowMs{ nowMs }
                , _muteUntilMs{ 0 }
                , _bReadOk{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                ServiceSanctionState     state;
                const ServiceStoreResult result = ServiceSanction::readState( connection, _accountID, state );
                if ( result != ServiceStoreResult::Ok )
                    return;
                if ( ServiceSanction::isActive( state, ServiceSanctionKind::ChatMute, _nowMs ) )
                    _muteUntilMs = state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )];
                _bReadOk = SW_TRUE;
            }

            void complete() override { _pService->applySanction( _accountID, _muteUntilMs, _bReadOk == SW_TRUE, _nowMs ); }

        private:
            ChatService* _pService;
            AccountID    _accountID;
            int64        _nowMs;
            int64        _muteUntilMs;
            uint8        _bReadOk;
        };

        /** @brief 쌓인 기록을 한 트랜잭션으로 쓰고, 정리할 채널이 있으면 보존 기간이 지난 기록을 지웁니다. */
        class ChatHistoryWriteWork final : public IServiceStoreWork
        {
        public:
            ChatHistoryWriteWork( ChatService* pService, vector<ChatMessage>&& listMessage, vector<string>&& listTrimChannel, int64 trimBeforeMs )
                : _listMessage{ std::move( listMessage ) }
                , _listTrimChannel{ std::move( listTrimChannel ) }
                , _pService{ pService }
                , _trimBeforeMs{ trimBeforeMs }
                , _bCommitted{ SW_FALSE }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                ServiceTransaction transaction;
                for ( const ChatMessage& message : _listMessage )
                {
                    transaction.put( ChatServiceInternal::getHistoryTable(), ChatServiceInternal::makeHistoryKey( message ), ChatProtocol::encodeRecord( message ) );
                }
                _bCommitted = connection.commit( transaction ) == ServiceStoreResult::Ok ? SW_TRUE : SW_FALSE;

                for ( const string& channelID : _listTrimChannel )
                {
                    trimChannel( connection, channelID );
                }
            }

            void complete() override { _pService->applyHistoryWrite( _bCommitted == SW_TRUE, static_cast<int32>( _listMessage.size() ) ); }

        private:
            void trimChannel( IServiceStoreConnection& connection, const string& channelID ) const
            {
                vector<ServiceRecord> listOld;
                const string          prefix = channelID + "/";
                if ( connection.listRecords( ChatServiceInternal::getHistoryTable(), prefix, "", ChatServiceInternal::kTrimBatchCount, false, listOld ) != ServiceStoreResult::Ok )
                    return;
                ServiceTransaction trim;
                for ( const ServiceRecord& record : listOld )
                {
                    uint64 sentMs = 0;
                    if ( ServiceKeyUtil::parseHex64( string_view( record._key ).substr( prefix.size(), ServiceKeyUtil::kHexWidth ), sentMs ) == false ||
                         static_cast<int64>( sentMs ) >= _trimBeforeMs )
                        break; // 키 순서 = 시간 순 — 첫 새 기록에서 멈춘다
                    trim.erase( ChatServiceInternal::getHistoryTable(), record._key, record._version );
                }
                if ( trim.isEmpty() == false )
                    (void)connection.commit( trim ); // 정리는 다음 기회에 다시 해도 된다(Conflict · Unavailable 은 버린다)
            }

            vector<ChatMessage> _listMessage;
            vector<string>      _listTrimChannel;
            ChatService*        _pService;
            int64               _trimBeforeMs;
            uint8               _bCommitted;
        };

        /** @brief 기록을 최근 것부터 읽는 일입니다. 커서는 앞 쪽의 마지막 키(그보다 이전부터)입니다. */
        class ChatHistoryReadWork final : public IServiceStoreWork
        {
        public:
            ChatHistoryReadWork( ChatService* pService, string&& channelID, string&& cursor, int32 maxCount, uint64 requestTag )
                : _listMessage{}
                , _channelID{ std::move( channelID ) }
                , _cursor{ std::move( cursor ) }
                , _nextCursor{}
                , _pService{ pService }
                , _requestTag{ requestTag }
                , _maxCount{ maxCount }
                , _result{ ChatResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                vector<ServiceRecord> listRecord;
                if ( connection.listRecords( ChatServiceInternal::getHistoryTable(), _channelID + "/", _cursor, _maxCount, true, listRecord ) != ServiceStoreResult::Ok )
                    return;
                for ( const ServiceRecord& record : listRecord )
                {
                    ChatMessage message;
                    if ( ChatProtocol::decodeRecord( record._bytes, message ) )
                        _listMessage.push_back( std::move( message ) );
                }
                if ( static_cast<int32>( listRecord.size() ) == _maxCount )
                    _nextCursor = listRecord.back()._key;
                _result = ChatResult::Ok;
            }

            void complete() override { _pService->applyHistoryRead( _requestTag, _result, std::move( _listMessage ), std::move( _nextCursor ) ); }

        private:
            vector<ChatMessage> _listMessage;
            string              _channelID;
            string              _cursor;
            string              _nextCursor;
            ChatService*        _pService;
            uint64              _requestTag;
            int32               _maxCount;
            ChatResult          _result;
        };
    } // namespace
} // namespace sw

namespace sw
{
    ChatService::ChatService()
        : _mapAccountToMember{}
        , _mapChannelToMember{}
        , _mapPresenceRequestToWhisper{}
        , _listHistoryQueue{}
        , _completionBuffer{}
        , _deliveryBuffer{}
        , _busTopicBuffer{}
        , _wordFilter{}
        , _spamGuard{}
        , _settings{}
        , _dependencies{}
        , _serverTopic{}
        , _sequence{ 0 }
        , _pendingStoreCount{ 0 }
        , _bHistoryWriting{ SW_FALSE }
    {
    }

    ChatService::~ChatService() { shutdown(); }

    bool ChatService::initialize( const ChatServiceDependencies& dependencies, const ChatSettings& settings )
    {
        if ( dependencies._pStore == nullptr || dependencies._pDirectory == nullptr )
        {
            SW_LOG_ERROR( "ChatService needs a service store and an account directory" );
            return false;
        }
        _dependencies = dependencies;
        _settings     = settings;
        _serverTopic  = ChatProtocol::makeServerTopic( dependencies._serverID );
        _spamGuard.initialize( settings._spam );
        if ( settings._bannedWordsPath.empty() == false && _wordFilter.loadFile( settings._bannedWordsPath, settings._filterMode ) == false )
            SW_LOG_WARNING( "ChatService: banned word file '%#' not found - chat is not filtered", settings._bannedWordsPath );
        if ( dependencies._pBus != nullptr )
        {
            _busTopicBuffer.push( ChatBusTopicChange{ _serverTopic, SW_TRUE } );                                // 다른 서버가 보낸 귓속말
            _busTopicBuffer.push( ChatBusTopicChange{ string( ServiceSanctionBus::kChangedTopic ), SW_TRUE } ); // GM 이 제재를 바꿨다(자기 서버 것도)
        }
        return true;
    }

    void ChatService::shutdown()
    {
        IServiceStore* pStore   = _dependencies._pStore;
        const Deadline deadline = Deadline::afterMilliseconds( ChatServiceInternal::kShutdownWaitMs );
        while ( pStore != nullptr && _pendingStoreCount > 0 && deadline.isExpired() == false )
        {
            if ( pStore->pollCompletions() == 0 )
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 1000000 );
        }
        if ( _pendingStoreCount > 0 )
            SW_LOG_ERROR( "ChatService shut down with %# store works still pending", _pendingStoreCount );
        if ( _dependencies._pPresence != nullptr ) // 맡긴 찾기를 거둔다 — 접속 상태 창구가 이 객체보다 오래 살아도 부르지 않게(저장 일을 거둔 뒤 — 그 완료가 찾기를 낼 수 있다)
        {
            for ( const auto& [requestID, pending] : _mapPresenceRequestToWhisper )
            {
                _dependencies._pPresence->cancel( requestID );
            }
        }
        _mapAccountToMember.clear();
        _mapChannelToMember.clear();
        _mapPresenceRequestToWhisper.clear();
        _listHistoryQueue.clear();
        _dependencies = ChatServiceDependencies{};
    }

    int32 ChatService::getLocalMemberCount( string_view channelID ) const
    {
        const auto channelIt = _mapChannelToMember.find( string( channelID ) );
        return channelIt != _mapChannelToMember.end() ? static_cast<int32>( channelIt->second.size() ) : 0;
    }

    ChatService::Member* ChatService::ensureMember( AccountID accountID, int64 nowMs )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt != _mapAccountToMember.end() )
            return &memberIt->second;
        AccountIdentity identity;
        if ( _dependencies._pDirectory == nullptr || _dependencies._pDirectory->findIdentity( accountID, identity ) == false )
            return nullptr; // 이 서버에 붙지 않은 계정 — 호스트가 인증했다면 생기지 않는다
        Member& member      = _mapAccountToMember[accountID];
        member._displayName = identity._displayName;
        startSanctionRead( accountID, member, nowMs );
        return &member;
    }

    bool ChatService::isSanctionFresh( const Member& member, int64 nowMs ) const
    {
        return member._sanctionReadMs >= 0 && nowMs - member._sanctionReadMs < _settings._sanctionCacheMs;
    }

    void ChatService::startSanctionRead( AccountID accountID, Member& member, int64 nowMs )
    {
        if ( member._bSanctionReading == SW_TRUE )
            return;
        member._bSanctionReading = SW_TRUE;
        ++_pendingStoreCount;
        _dependencies._pStore->submit( make_unique<ChatSanctionWork>( this, accountID, nowMs ) );
    }

    void ChatService::applySanction( AccountID accountID, int64 muteUntilMs, bool bReadOk, int64 nowMs )
    {
        --_pendingStoreCount;
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt == _mapAccountToMember.end() )
            return; // 그새 떠났다
        Member& member           = memberIt->second;
        member._bSanctionReading = SW_FALSE;
        if ( bReadOk )
        {
            member._muteUntilMs    = muteUntilMs;
            member._sanctionReadMs = nowMs;
        }
        vector<PendingSend> listPending;
        listPending.swap( member._listPendingSend );
        for ( const PendingSend& send : listPending )
        {
            if ( member._sanctionReadMs < 0 )
            {
                completeSimple( ChatServiceInternal::toMethod( send._recipientID != kInvalidAccountID ), send._requestTag, ChatResult::Unavailable );
                continue;
            }
            processSend( accountID, member, send );
        }
    }

    void ChatService::refreshSanction( AccountID accountID )
    {
        const auto memberIt = _mapAccountToMember.find( accountID );
        if ( memberIt != _mapAccountToMember.end() )
            memberIt->second._sanctionReadMs = -1;
    }

    void ChatService::completeSimple( uint16 method, uint64 requestTag, ChatResult result, int64 retryAfterMs )
    {
        ChatCompletion completion;
        completion._method              = method;
        completion._requestTag          = requestTag;
        completion._reply._result       = result;
        completion._reply._retryAfterMs = retryAfterMs;
        _completionBuffer.push( std::move( completion ) );
    }

    void ChatService::completeMessage( uint16 method, uint64 requestTag, ChatMessage&& message )
    {
        ChatCompletion completion;
        completion._method         = method;
        completion._requestTag     = requestTag;
        completion._reply._message = std::move( message );
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
        member._listChannel.erase( std::remove( member._listChannel.begin(), member._listChannel.end(), channelID ), member._listChannel.end() );
        const auto channelIt = _mapChannelToMember.find( channelID );
        if ( channelIt == _mapChannelToMember.end() )
            return;
        vector<AccountID>& listMember = channelIt->second;
        listMember.erase( std::remove( listMember.begin(), listMember.end(), accountID ), listMember.end() );
        if ( listMember.empty() == false )
            return;
        _mapChannelToMember.erase( channelIt );
        if ( _dependencies._pBus != nullptr )
            _busTopicBuffer.push( ChatBusTopicChange{ ChatProtocol::makeChannelTopic( channelID ), SW_FALSE } );
    }

    ChatResult ChatService::addMember( AccountID accountID, string_view channelID, int64 nowMs )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false || kind == ChatChannelKind::Whisper )
            return ChatResult::Invalid;
        Member* pMember = ensureMember( accountID, nowMs );
        if ( pMember == nullptr )
            return ChatResult::TargetOffline;
        const string id( channelID );
        if ( std::find( pMember->_listChannel.begin(), pMember->_listChannel.end(), id ) != pMember->_listChannel.end() )
            return ChatResult::Ok;
        if ( static_cast<int32>( pMember->_listChannel.size() ) >= ChatLimit::kMaxChannelPerMember )
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
            completeSimple( ChatServiceInternal::toMethod( send._recipientID != kInvalidAccountID ), send._requestTag, ChatResult::Unavailable );
        }
        _mapAccountToMember.erase( memberIt );
        _spamGuard.forget( accountID );
    }

    void ChatService::joinChannel( AccountID accountID, string_view channelID, int64 nowMs, uint64 requestTag )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false )
        {
            completeSimple( ChatMethod::kJoin, requestTag, ChatResult::Invalid );
            return;
        }
        if ( ChatServiceInternal::isClientJoinable( kind ) == false )
        {
            completeSimple( ChatMethod::kJoin, requestTag, ChatResult::NotJoinable );
            return;
        }
        completeSimple( ChatMethod::kJoin, requestTag, addMember( accountID, channelID, nowMs ) );
    }

    void ChatService::leaveChannel( AccountID accountID, string_view channelID, uint64 requestTag )
    {
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false || ChatServiceInternal::isClientJoinable( kind ) == false )
        {
            completeSimple( ChatMethod::kLeave, requestTag, ChatResult::NotJoinable );
            return;
        }
        removeMember( accountID, channelID );
        completeSimple( ChatMethod::kLeave, requestTag, ChatResult::Ok );
    }

    void ChatService::sendMessage( AccountID accountID, string_view channelID, string_view text, int64 nowMs, uint64 requestTag )
    {
        submitSend( accountID, PendingSend{ string( channelID ), string( text ), requestTag, nowMs, kInvalidAccountID } );
    }

    void ChatService::sendWhisper( AccountID accountID, AccountID recipientID, string_view text, int64 nowMs, uint64 requestTag )
    {
        if ( recipientID == kInvalidAccountID || recipientID == accountID )
        {
            completeSimple( ChatMethod::kWhisper, requestTag, ChatResult::Invalid );
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
            const int64 nowMs = send._nowMs;
            pMember->_listPendingSend.push_back( std::move( send ) );
            startSanctionRead( accountID, *pMember, nowMs );
            return;
        }
        if ( isSanctionFresh( *pMember, send._nowMs ) == false )
            startSanctionRead( accountID, *pMember, send._nowMs ); // 묵은 값으로 지금 처리하고 뒤에서 새로 읽는다
        processSend( accountID, *pMember, send );
    }

    ChatResult ChatService::prepareText( AccountID accountID, Member& member, string_view text, int64 nowMs, string& outText, int64& outRetryAfterMs )
    {
        outRetryAfterMs = 0;
        if ( text.empty() || text.size() > static_cast<size_t>( ChatLimit::kMaxTextSize ) )
            return ChatResult::Invalid;
        if ( member._muteUntilMs != 0 && nowMs < member._muteUntilMs )
        {
            outRetryAfterMs = member._muteUntilMs - nowMs;
            return ChatResult::Muted;
        }
        const ChatSpamVerdict spam = _spamGuard.check( accountID, text, nowMs, outRetryAfterMs );
        if ( spam == ChatSpamVerdict::RateLimited )
            return ChatResult::RateLimited;
        if ( spam == ChatSpamVerdict::Repeated )
            return ChatResult::Repeated;
        switch ( _wordFilter.apply( text, outText ) )
        {
            case ChatFilterVerdict::Rejected:
                return ChatResult::Rejected;
            case ChatFilterVerdict::InvalidText:
                return ChatResult::Invalid;
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
            const bool bMember = std::find( member._listChannel.begin(), member._listChannel.end(), send._channelID ) != member._listChannel.end();
            if ( ChatChannelID::parseKind( send._channelID, kind ) == false || bMember == false )
            {
                completeSimple( method, send._requestTag, ChatResult::NotMember );
                return;
            }
        }
        string           filtered;
        int64            retryAfterMs = 0;
        const ChatResult prepared     = prepareText( accountID, member, send._text, send._nowMs, filtered, retryAfterMs );
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
        message._sentMs      = send._nowMs;
        message._serverID    = _dependencies._serverID;
        message._sequence    = ++_sequence;

        if ( bWhisper == false )
        {
            deliverToChannel( message ); // 보낸 이도 알림으로 받는다(되울림과 별개로 — 다른 창이 같은 채널을 보고 있을 수 있다)
            publishMessage( ChatProtocol::makeChannelTopic( message._channelID ), message );
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
        if ( _dependencies._pPresence == nullptr || _dependencies._pBus == nullptr )
        {
            completeSimple( method, send._requestTag, ChatResult::TargetOffline );
            return;
        }
        // 다른 서버 — 접속 상태로 서버를 찾는다(결과는 onPresenceFound, 늘 나중에)
        const uint64 requestID = _dependencies._pPresence->submitFindByAccount( send._recipientID, AccountPresenceDelegate::create<&ChatService::onPresenceFound>( this ) );
        _mapPresenceRequestToWhisper.emplace( requestID, PendingWhisper{ std::move( message ), send._requestTag } );
    }

    void ChatService::onPresenceFound( const AccountPresenceResult& result )
    {
        const auto whisperIt = _mapPresenceRequestToWhisper.find( result._requestID );
        if ( whisperIt == _mapPresenceRequestToWhisper.end() )
            return; // 내린 뒤에 온 결과
        PendingWhisper pending = std::move( whisperIt->second );
        _mapPresenceRequestToWhisper.erase( whisperIt );
        if ( result.isOnline() == false || result._serverID == _dependencies._serverID )
        {
            completeSimple( ChatMethod::kWhisper, pending._requestTag, ChatResult::TargetOffline ); // 이 서버라면 그새 떠났다
            return;
        }
        publishMessage( ChatProtocol::makeServerTopic( result._serverID ), pending._message );
        queueHistory( pending._message );
        completeMessage( ChatMethod::kWhisper, pending._requestTag, std::move( pending._message ) ); // 받는 서버가 차단으로 버려도 보낸 이에게는 보낸 것으로 보인다
    }

    void ChatService::publishMessage( const string& topic, const ChatMessage& message )
    {
        if ( _dependencies._pBus == nullptr )
            return;
        BitWriter body;
        ChatProtocol::writeMessage( body, message );
        _dependencies._pBus->publish( topic, body.getBytes().data(), body.getByteCount() );
    }

    void ChatService::deliverToChannel( const ChatMessage& message )
    {
        const auto channelIt = _mapChannelToMember.find( message._channelID );
        if ( channelIt == _mapChannelToMember.end() )
            return;
        for ( const AccountID recipientID : channelIt->second )
        {
            if ( _dependencies._pPolicy != nullptr && recipientID != message._senderID && _dependencies._pPolicy->isBlocked( recipientID, message._senderID ) )
                continue; // 막은 사람의 채널 말은 받는 쪽에서 안 보인다
            _deliveryBuffer.push( ChatDelivery{ message, recipientID } );
        }
    }

    void ChatService::deliverWhisperLocally( const ChatMessage& message )
    {
        if ( _dependencies._pPolicy != nullptr && _dependencies._pPolicy->isBlocked( message._recipientID, message._senderID ) )
            return;
        _deliveryBuffer.push( ChatDelivery{ message, message._recipientID } );
    }

    void ChatService::handleBusMessage( string_view topic, const vector<uint8>& bytes )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( topic == ServiceSanctionBus::kChangedTopic )
        {
            const AccountID accountID = reader.readVarUint();
            if ( reader.hasOverflowed() == false )
                refreshSanction( accountID );
            return;
        }
        ChatMessage message;
        if ( ChatProtocol::readMessage( reader, message ) == false || message._serverID == _dependencies._serverID )
            return;
        if ( message._kind == ChatChannelKind::Whisper )
        {
            if ( topic == _serverTopic && _dependencies._pDirectory != nullptr && _dependencies._pDirectory->isAccountOnline( message._recipientID ) )
                deliverWhisperLocally( message );
            return;
        }
        if ( topic == ChatProtocol::makeChannelTopic( message._channelID ) )
            deliverToChannel( message );
    }

    void ChatService::queueHistory( const ChatMessage& message )
    {
        if ( static_cast<int32>( _listHistoryQueue.size() ) >= _settings._maxQueuedHistory )
        {
            SW_LOG_WARNING( "ChatService: chat history queue is full (%#) - dropping the oldest record", _listHistoryQueue.size() );
            _listHistoryQueue.erase( _listHistoryQueue.begin() );
        }
        _listHistoryQueue.push_back( message );
    }

    void ChatService::tick( int64 nowMs )
    {
        if ( _dependencies._pStore == nullptr || _bHistoryWriting == SW_TRUE || _listHistoryQueue.empty() )
            return;
        const size_t        takeCount = std::min( _listHistoryQueue.size(), static_cast<size_t>( _settings._historyFlushMax ) );
        vector<ChatMessage> listMessage( _listHistoryQueue.begin(), _listHistoryQueue.begin() + static_cast<ptrdiff_t>( takeCount ) );
        _listHistoryQueue.erase( _listHistoryQueue.begin(), _listHistoryQueue.begin() + static_cast<ptrdiff_t>( takeCount ) );
        vector<string> listTrimChannel;
        for ( const ChatMessage& message : listMessage )
        {
            const bool bTrim = _settings._historyTrimEvery > 0 && message._sequence % static_cast<uint32>( _settings._historyTrimEvery ) == 0;
            if ( bTrim && std::find( listTrimChannel.begin(), listTrimChannel.end(), message._channelID ) == listTrimChannel.end() )
                listTrimChannel.push_back( message._channelID );
        }
        _bHistoryWriting = SW_TRUE;
        ++_pendingStoreCount;
        _dependencies._pStore->submit( sw::make_unique<ChatHistoryWriteWork>( this, std::move( listMessage ), std::move( listTrimChannel ), nowMs - _settings._historyRetentionMs ) );
    }

    void ChatService::applyHistoryWrite( bool bCommitted, int32 messageCount )
    {
        --_pendingStoreCount;
        _bHistoryWriting = SW_FALSE;
        if ( bCommitted == false )
            SW_LOG_WARNING( "ChatService: %# chat history records were not stored (store unavailable) - they are dropped", messageCount );
    }

    bool ChatService::isWhisperParticipant( AccountID accountID, string_view channelID ) const
    {
        // 기록 키 `whisper.<작은 계정 16 진>.<큰 계정 16 진>` — 두 id 중 하나가 나여야 한다
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
        ChatChannelKind kind = ChatChannelKind::World;
        if ( ChatChannelID::parseKind( channelID, kind ) == false || maxCount < 1 || maxCount > ChatLimit::kMaxHistoryPage ||
             cursor.size() > static_cast<size_t>( ChatProtocol::kMaxCursorSize ) )
        {
            completeSimple( ChatMethod::kHistory, requestTag, ChatResult::Invalid );
            return;
        }
        bool bAllowed = false;
        if ( kind == ChatChannelKind::Whisper )
        {
            bAllowed = isWhisperParticipant( accountID, channelID );
        }
        else
        {
            const auto memberIt = _mapAccountToMember.find( accountID );
            bAllowed            = memberIt != _mapAccountToMember.end() &&
                       std::find( memberIt->second._listChannel.begin(), memberIt->second._listChannel.end(), string( channelID ) ) != memberIt->second._listChannel.end();
        }
        if ( bAllowed == false )
        {
            completeSimple( ChatMethod::kHistory, requestTag, ChatResult::NotMember );
            return;
        }
        ++_pendingStoreCount;
        _dependencies._pStore->submit( sw::make_unique<ChatHistoryReadWork>( this, string( channelID ), string( cursor ), maxCount, requestTag ) );
    }

    void ChatService::applyHistoryRead( uint64 requestTag, ChatResult result, vector<ChatMessage>&& listMessage, string&& nextCursor )
    {
        --_pendingStoreCount;
        ChatCompletion completion;
        completion._method             = ChatMethod::kHistory;
        completion._requestTag         = requestTag;
        completion._reply._result      = result;
        completion._reply._listHistory = std::move( listMessage );
        completion._reply._nextCursor  = std::move( nextCursor );
        _completionBuffer.push( std::move( completion ) );
    }
} // namespace sw
