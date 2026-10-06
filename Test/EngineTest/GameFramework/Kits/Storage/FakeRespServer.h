/**
 * @file FakeRespServer.h
 * @brief 시험 안의 가짜 RESP 서버 — 루프백 스트림 위에서 RESP 드라이버가 쓰는 명령 부분집합만 Valkey 처럼 답합니다(만료 · WATCH · MULTI/EXEC · 정렬 집합 · 발행/구독 · AUTH).
 * @details - 스레드가 없다 — 루프백 전송은 한 스레드에서만 돈다. RESP 앞에는 `createClientTransport` 가 준 전송을 준다: 앞이 자기 전송을 돌 때
 *            (`pollIo`) 서버도 한 번 돈다. 그래서 시험 스레드 하나로 결정적이다.
 *          - 시계는 손으로 민다(`advanceTimeMs`). 실패 주입: 답 붙잡기(시한) · 모든 연결 끊기(`dropAllConnections`).
 *          - TLS 컨텍스트를 주면 연결마다 서버 세션을 지난다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Storage/Server/CacheStore/Driver/Resp/RespCodec.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace test
{
    class FakeRespServer;

    /** @brief 안의 루프백 전송을 돌 때 가짜 서버도 한 번 돕니다. 나머지는 그대로 넘긴다. */
    class FakeRespPumpingTransport final : public sw::IStreamTransport
    {
    public:
        FakeRespPumpingTransport( sw::unique_ptr<sw::IStreamTransport> inner, FakeRespServer* pServer )
            : _inner{ std::move( inner ) }
            , _pServer{ pServer }
        {
        }

        bool                       initialize( sw::IStreamHandler* pHandler, const sw::StreamTransportSettings& settings ) override { return _inner->initialize( pHandler, settings ); }
        void                       shutdown() override { _inner->shutdown(); }
        bool                       listen( const sw::NetAddress& bindAddress ) override { return _inner->listen( bindAddress ); }
        uint16                     getListenPort() const override { return _inner->getListenPort(); }
        sw::StreamConnectionHandle connect( const sw::NetAddress& remote ) override { return _inner->connect( remote ); }
        sw::StreamSendResult       send( sw::StreamConnectionHandle handle, const uint8* pData, int32 size ) override { return _inner->send( handle, pData, size ); }
        void                       close( sw::StreamConnectionHandle handle, sw::StreamCloseMode mode ) override { _inner->close( handle, mode ); }
        void                       setReceivePaused( sw::StreamConnectionHandle handle, bool bPaused ) override { _inner->setReceivePaused( handle, bPaused ); }
        int32                      pollIo( int32 timeoutMilli ) override;
        sw::NetAddress             getRemoteAddress( sw::StreamConnectionHandle handle ) const override { return _inner->getRemoteAddress( handle ); }
        sw::StreamTransportStats   getStats() const override { return _inner->getStats(); }

    private:
        sw::unique_ptr<sw::IStreamTransport> _inner;
        FakeRespServer*                      _pServer;
    };
} // namespace test

namespace test
{
    class FakeRespServer final : public sw::IStreamHandler
    {
    public:
        FakeRespServer( sw::LoopbackStreamNetwork& network, sw::ITlsContext* pTlsContext = nullptr )
            : _mapConnection{}
            , _mapEntry{}
            , _mapKeyVersion{}
            , _password{}
            , _pNetwork{ &network }
            , _transport{ network.createTransport() }
            , _pTlsContext{ pTlsContext }
            , _nowMs{ 1000000 }
            , _nextVersion{ 1 }
            , _acceptedCount{ 0 }
            , _bHoldReplies{ false }
        {
            sw::StreamTransportSettings settings;
            settings._ioThreadCount      = 0;
            settings._idleTimeoutSeconds = 0.0;
            (void)_transport->initialize( this, settings );
            (void)_transport->listen( sw::NetAddress::makeLoopback( 0 ) );
        }

        ~FakeRespServer() override { _transport->shutdown(); }

        FakeRespServer( const FakeRespServer& )            = delete;
        FakeRespServer& operator=( const FakeRespServer& ) = delete;

        sw::NetAddress getAddress() const { return sw::NetAddress::makeLoopback( _transport->getListenPort() ); }

        /** @brief RESP 앞에 줄 전송 — 앞이 돌 때 이 서버도 돈다. */
        sw::unique_ptr<sw::IStreamTransport> createClientTransport() { return sw::make_unique<FakeRespPumpingTransport>( _pNetwork->createTransport(), this ); }

        /** @brief 서버 전송을 한 번 돕니다(받은 명령에 답한다). */
        void pump() { (void)_transport->pollIo( 0 ); }

        void setPassword( const sw::string& password ) { _password = password; }
        void advanceTimeMs( int64 deltaMs ) { _nowMs += deltaMs; }

        /** @brief 켜 두면 받은 명령에 답하지 않는다(시한 시험). */
        void setHoldReplies( bool bHold ) { _bHoldReplies = bHold; }

        /** @brief 붙은 연결을 모두 RST 로 끊습니다. */
        void dropAllConnections()
        {
            pump(); // 오는 중인 연결 · 명령을 먼저 받는다
            for ( const auto& [packed, connection] : _mapConnection )
            {
                (void)packed;
                _transport->close( connection._handle, sw::StreamCloseMode::Abort );
            }
            _mapConnection.clear();
            pump();
        }

        int32 getAcceptedCount() const { return _acceptedCount; }

        int32 getSubscriberCount( const sw::string& channel ) const
        {
            int32 count = 0;
            for ( const auto& [packed, connection] : _mapConnection )
            {
                (void)packed;
                count += std::find( connection._listChannel.begin(), connection._listChannel.end(), channel ) != connection._listChannel.end() ? 1 : 0;
            }
            return count;
        }

        // IStreamHandler — 서버 스레드
        void onStreamOpened( sw::StreamConnectionHandle handle, const sw::NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            Connection& connection = _mapConnection[handle.packed()];
            connection._handle     = handle;
            if ( _pTlsContext != nullptr )
                connection._tlsSession = _pTlsContext->createSession();
            ++_acceptedCount;
        }

        void onStreamReceived( sw::StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            const auto connectionIt = _mapConnection.find( handle.packed() );
            if ( connectionIt == _mapConnection.end() )
                return;
            Connection& connection = connectionIt->second;
            if ( connection._tlsSession == nullptr )
            {
                connection._parser.append( pData, static_cast<size_t>( size ) );
            }
            else
            {
                sw::vector<uint8> plainBytes;
                if ( connection._tlsSession->feedCiphertext( pData, size ) == false || connection._tlsSession->readPlaintext( plainBytes ) == false )
                {
                    _transport->close( handle, sw::StreamCloseMode::Abort );
                    return;
                }
                flushCiphertext( connection );
                connection._parser.append( plainBytes.data(), plainBytes.size() );
            }
            sw::RespValue command;
            for ( ;; )
            {
                const sw::RespParseResult result = connection._parser.next( command );
                if ( result == sw::RespParseResult::NeedMore )
                    break;
                if ( result == sw::RespParseResult::Error )
                {
                    _transport->close( handle, sw::StreamCloseMode::Abort );
                    return;
                }
                if ( _bHoldReplies )
                    continue;
                sw::vector<uint8> replyBytes;
                executeCommand( connection, command, replyBytes );
                writeReply( connection, replyBytes );
            }
        }

        void onStreamClosed( sw::StreamConnectionHandle handle, sw::StreamCloseReason reason ) override
        {
            (void)reason;
            _mapConnection.erase( handle.packed() );
        }

    private:
        struct Entry
        {
            sw::vector<uint8>          _bytes{};
            sw::map<sw::string, int64> _mapMemberToScore{};
            int64                      _expiresAtMs{ 0 };
            bool                       _bScoreSet{ false };
        };

        struct Connection
        {
            sw::RespParser                       _parser{};
            sw::vector<sw::RespValue>            _listQueuedCommand{};
            sw::vector<sw::string>               _listChannel{};
            sw::unordered_map<sw::string, int64> _mapWatchedVersion{};
            sw::unique_ptr<sw::ITlsSession>      _tlsSession{};
            sw::StreamConnectionHandle           _handle{};
            bool                                 _bAuthenticated{ false };
            bool                                 _bInMulti{ false };
        };

        void flushCiphertext( Connection& connection )
        {
            sw::vector<uint8> cipherBytes;
            connection._tlsSession->takeCiphertext( cipherBytes );
            if ( cipherBytes.empty() == false )
                (void)_transport->send( connection._handle, cipherBytes.data(), static_cast<int32>( cipherBytes.size() ) );
        }

        void writeReply( Connection& connection, const sw::vector<uint8>& replyBytes )
        {
            if ( replyBytes.empty() )
                return;
            if ( connection._tlsSession == nullptr )
            {
                (void)_transport->send( connection._handle, replyBytes.data(), static_cast<int32>( replyBytes.size() ) );
                return;
            }
            // 가짜 서버 — 못 보낸 답은 클라이언트 쪽 시한 초과로 드러난다
            (void)connection._tlsSession->writePlaintext( replyBytes.data(), static_cast<int32>( replyBytes.size() ) );
            flushCiphertext( connection );
        }

        // ---- 답 바이트 ----
        static void appendText( sw::vector<uint8>& outBytes, sw::string_view text ) { outBytes.insert( outBytes.end(), text.begin(), text.end() ); }
        static void appendNumber( sw::vector<uint8>& outBytes, int64 value )
        {
            utf8                       arrBuffer[sw::constant::kMaxBuffer32];
            const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sw::constant::kMaxBuffer32, value );
            outBytes.insert( outBytes.end(), arrBuffer, result.ptr );
        }
        static void replySimple( sw::vector<uint8>& outBytes, sw::string_view text )
        {
            outBytes.push_back( '+' );
            appendText( outBytes, text );
            appendText( outBytes, "\r\n" );
        }
        static void replyError( sw::vector<uint8>& outBytes, sw::string_view text )
        {
            outBytes.push_back( '-' );
            appendText( outBytes, text );
            appendText( outBytes, "\r\n" );
        }
        static void replyInteger( sw::vector<uint8>& outBytes, int64 value )
        {
            outBytes.push_back( ':' );
            appendNumber( outBytes, value );
            appendText( outBytes, "\r\n" );
        }
        static void replyBulk( sw::vector<uint8>& outBytes, const uint8* pData, size_t size )
        {
            outBytes.push_back( '$' );
            appendNumber( outBytes, static_cast<int64>( size ) );
            appendText( outBytes, "\r\n" );
            outBytes.insert( outBytes.end(), pData, pData + size );
            appendText( outBytes, "\r\n" );
        }
        static void replyBulkText( sw::vector<uint8>& outBytes, sw::string_view text ) { replyBulk( outBytes, reinterpret_cast<const uint8*>( text.data() ), text.size() ); }
        static void replyBulkNumber( sw::vector<uint8>& outBytes, int64 value )
        {
            sw::vector<uint8> numberBytes;
            appendNumber( numberBytes, value );
            replyBulk( outBytes, numberBytes.data(), numberBytes.size() );
        }
        static void replyNull( sw::vector<uint8>& outBytes ) { appendText( outBytes, "$-1\r\n" ); }
        static void replyArrayHeader( sw::vector<uint8>& outBytes, int64 count )
        {
            outBytes.push_back( '*' );
            appendNumber( outBytes, count );
            appendText( outBytes, "\r\n" );
        }

        // ---- 명령 ----
        static sw::string toUpper( sw::string_view text )
        {
            sw::string upper( text );
            for ( utf8& character : upper )
                character = static_cast<utf8>( std::toupper( static_cast<uint8>( character ) ) );
            return upper;
        }

        Entry* findEntry( const sw::string& key )
        {
            const auto entryIt = _mapEntry.find( key );
            if ( entryIt == _mapEntry.end() )
                return nullptr;
            if ( entryIt->second._expiresAtMs != 0 && _nowMs >= entryIt->second._expiresAtMs )
            {
                _mapEntry.erase( entryIt );
                touchKey( key );
                return nullptr;
            }
            return &entryIt->second;
        }

        void touchKey( const sw::string& key ) { _mapKeyVersion[key] = _nextVersion++; }

        int64 readKeyVersion( const sw::string& key ) const
        {
            const auto versionIt = _mapKeyVersion.find( key );
            return versionIt == _mapKeyVersion.end() ? 0 : versionIt->second;
        }

        static bool parseInteger( const sw::RespValue& value, int64& outValue ) { return parseIntegerText( value.getText(), outValue ); }

        static bool parseIntegerText( sw::string_view text, int64& outValue )
        {
            if ( text.empty() )
                return false;
            const std::from_chars_result result = std::from_chars( text.data(), text.data() + text.size(), outValue );
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        sw::vector<std::pair<sw::string, int64>> makeRanking( const Entry& entry ) const
        {
            sw::vector<std::pair<sw::string, int64>> listRanked( entry._mapMemberToScore.begin(), entry._mapMemberToScore.end() );
            std::sort( listRanked.begin(), listRanked.end(), &FakeRespServer::isRankedBefore );
            return listRanked;
        }

        /** @brief 점수 내림차순 · 같은 점수는 멤버 바이트 내림차순(Valkey `ZREVRANGE` 와 같다). */
        static bool isRankedBefore( const std::pair<sw::string, int64>& left, const std::pair<sw::string, int64>& right )
        {
            if ( left.second != right.second )
                return left.second > right.second;
            return left.first > right.first;
        }

        void executeCommand( Connection& connection, const sw::RespValue& command, sw::vector<uint8>& outBytes )
        {
            if ( command._type != sw::RespType::Array || command._listElement.empty() )
            {
                replyError( outBytes, "ERR protocol error" );
                return;
            }
            const sw::vector<sw::RespValue>& listArgument = command._listElement;
            const sw::string                 name         = toUpper( listArgument[0].getText() );
            if ( name == "AUTH" )
            {
                const sw::string_view password = listArgument.back().getText();
                connection._bAuthenticated     = _password.empty() || password == _password;
                if ( connection._bAuthenticated )
                    replySimple( outBytes, "OK" );
                else
                    replyError( outBytes, "WRONGPASS invalid username-password pair" );
                return;
            }
            if ( _password.empty() == false && connection._bAuthenticated == false )
            {
                replyError( outBytes, "NOAUTH Authentication required." );
                return;
            }
            if ( name == "SUBSCRIBE" || name == "UNSUBSCRIBE" )
            {
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    const sw::string channel( listArgument[index].getText() );
                    const auto       channelIt = std::find( connection._listChannel.begin(), connection._listChannel.end(), channel );
                    if ( name == "SUBSCRIBE" && channelIt == connection._listChannel.end() )
                        connection._listChannel.push_back( channel );
                    else if ( name == "UNSUBSCRIBE" && channelIt != connection._listChannel.end() )
                        connection._listChannel.erase( channelIt );
                    replyArrayHeader( outBytes, 3 );
                    replyBulkText( outBytes, name == "SUBSCRIBE" ? "subscribe" : "unsubscribe" );
                    replyBulkText( outBytes, channel );
                    replyInteger( outBytes, static_cast<int64>( connection._listChannel.size() ) );
                }
                return;
            }
            if ( name == "MULTI" )
            {
                connection._bInMulti = true;
                connection._listQueuedCommand.clear();
                replySimple( outBytes, "OK" );
                return;
            }
            if ( name == "DISCARD" )
            {
                connection._bInMulti = false;
                connection._listQueuedCommand.clear();
                connection._mapWatchedVersion.clear();
                replySimple( outBytes, "OK" );
                return;
            }
            if ( name == "EXEC" )
            {
                connection._bInMulti = false;
                bool bWatchBroken    = false;
                for ( const auto& [key, version] : connection._mapWatchedVersion )
                {
                    (void)findEntry( key ); // 만료도 바뀜이다
                    bWatchBroken = bWatchBroken || readKeyVersion( key ) != version;
                }
                connection._mapWatchedVersion.clear();
                const sw::vector<sw::RespValue> listQueued = std::move( connection._listQueuedCommand );
                connection._listQueuedCommand.clear();
                if ( bWatchBroken )
                {
                    appendText( outBytes, "*-1\r\n" );
                    return;
                }
                replyArrayHeader( outBytes, static_cast<int64>( listQueued.size() ) );
                for ( const sw::RespValue& queued : listQueued )
                    executeDataCommand( queued, outBytes );
                return;
            }
            if ( connection._bInMulti )
            {
                connection._listQueuedCommand.push_back( command );
                replySimple( outBytes, "QUEUED" );
                return;
            }
            if ( name == "WATCH" )
            {
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    const sw::string key( listArgument[index].getText() );
                    (void)findEntry( key );
                    connection._mapWatchedVersion[key] = readKeyVersion( key );
                }
                replySimple( outBytes, "OK" );
                return;
            }
            if ( name == "UNWATCH" )
            {
                connection._mapWatchedVersion.clear();
                replySimple( outBytes, "OK" );
                return;
            }
            if ( name == "PUBLISH" && listArgument.size() == 3 )
            {
                const sw::string channel( listArgument[1].getText() );
                int64            receiverCount = 0;
                for ( auto& [packed, subscriber] : _mapConnection )
                {
                    (void)packed;
                    if ( std::find( subscriber._listChannel.begin(), subscriber._listChannel.end(), channel ) == subscriber._listChannel.end() )
                        continue;
                    sw::vector<uint8> messageBytes;
                    replyArrayHeader( messageBytes, 3 );
                    replyBulkText( messageBytes, "message" );
                    replyBulkText( messageBytes, channel );
                    replyBulk( messageBytes, listArgument[2]._bytes.data(), listArgument[2]._bytes.size() );
                    writeReply( subscriber, messageBytes );
                    ++receiverCount;
                }
                replyInteger( outBytes, receiverCount );
                return;
            }
            executeDataCommand( command, outBytes );
        }

        void executeDataCommand( const sw::RespValue& command, sw::vector<uint8>& outBytes )
        {
            const sw::vector<sw::RespValue>& listArgument = command._listElement;
            const sw::string                 name         = toUpper( listArgument[0].getText() );
            const sw::string                 key          = listArgument.size() > 1 ? sw::string( listArgument[1].getText() ) : sw::string{};
            Entry*                           pEntry       = key.empty() ? nullptr : findEntry( key );
            if ( name == "PING" )
            {
                replySimple( outBytes, "PONG" );
                return;
            }
            if ( name == "GET" )
            {
                if ( pEntry == nullptr )
                    replyNull( outBytes );
                else if ( pEntry->_bScoreSet )
                    replyError( outBytes, "WRONGTYPE Operation against a key holding the wrong kind of value" );
                else
                    replyBulk( outBytes, pEntry->_bytes.data(), pEntry->_bytes.size() );
                return;
            }
            if ( name == "SET" && listArgument.size() >= 3 )
            {
                int64 ttlMs      = 0;
                bool  bIfAbsent  = false;
                bool  bIfPresent = false;
                for ( size_t index = 3; index < listArgument.size(); ++index )
                {
                    const sw::string option = toUpper( listArgument[index].getText() );
                    if ( option == "PX" && index + 1 < listArgument.size() )
                        (void)parseInteger( listArgument[++index], ttlMs ); // 가짜 서버 — 숫자가 아니면 만료 없음(0)으로 둔다
                    bIfAbsent  = bIfAbsent || option == "NX";
                    bIfPresent = bIfPresent || option == "XX";
                }
                if ( ( bIfAbsent && pEntry != nullptr ) || ( bIfPresent && pEntry == nullptr ) )
                {
                    replyNull( outBytes );
                    return;
                }
                Entry& entry       = _mapEntry[key];
                entry              = Entry{};
                entry._bytes       = listArgument[2]._bytes;
                entry._expiresAtMs = ttlMs > 0 ? _nowMs + ttlMs : 0;
                touchKey( key );
                replySimple( outBytes, "OK" );
                return;
            }
            if ( name == "DEL" )
            {
                const bool bExisted = pEntry != nullptr;
                _mapEntry.erase( key );
                if ( bExisted )
                    touchKey( key );
                replyInteger( outBytes, bExisted ? 1 : 0 );
                return;
            }
            if ( name == "PEXPIRE" && listArgument.size() == 3 )
            {
                int64 ttlMs = 0;
                if ( pEntry == nullptr || parseInteger( listArgument[2], ttlMs ) == false )
                {
                    replyInteger( outBytes, 0 );
                    return;
                }
                pEntry->_expiresAtMs = _nowMs + ttlMs;
                touchKey( key );
                replyInteger( outBytes, 1 );
                return;
            }
            if ( name == "INCRBY" && listArgument.size() == 3 )
            {
                int64 delta   = 0;
                int64 current = 0;
                if ( pEntry != nullptr && ( pEntry->_bScoreSet || parseIntegerText( sw::string_view{ reinterpret_cast<const utf8*>( pEntry->_bytes.data() ), pEntry->_bytes.size() }, current ) == false ) )
                {
                    replyError( outBytes, "ERR value is not an integer or out of range" );
                    return;
                }
                if ( parseInteger( listArgument[2], delta ) == false )
                {
                    replyError( outBytes, "ERR value is not an integer or out of range" );
                    return;
                }
                Entry& entry = _mapEntry[key];
                entry._bytes.clear();
                appendNumber( entry._bytes, current + delta );
                touchKey( key );
                replyInteger( outBytes, current + delta );
                return;
            }
            if ( pEntry != nullptr && pEntry->_bScoreSet == false && name.size() > 0 && name[0] == 'Z' )
            {
                replyError( outBytes, "WRONGTYPE Operation against a key holding the wrong kind of value" );
                return;
            }
            if ( ( name == "ZADD" || name == "ZINCRBY" ) && listArgument.size() == 4 )
            {
                int64 score = 0;
                (void)parseInteger( listArgument[2], score ); // 가짜 서버 — 숫자가 아니면 점수 0 으로 둔다
                Entry& entry      = _mapEntry[key];
                entry._bScoreSet  = true;
                int64&     stored = entry._mapMemberToScore[sw::string( listArgument[3].getText() )];
                const bool bNew   = name == "ZADD" && stored == 0;
                stored            = name == "ZADD" ? score : stored + score;
                touchKey( key );
                if ( name == "ZADD" )
                    replyInteger( outBytes, bNew ? 1 : 0 );
                else
                    replyBulkNumber( outBytes, stored );
                return;
            }
            if ( name == "ZREM" && listArgument.size() == 3 )
            {
                const bool bRemoved = pEntry != nullptr && pEntry->_mapMemberToScore.erase( sw::string( listArgument[2].getText() ) ) > 0;
                if ( bRemoved )
                    touchKey( key );
                replyInteger( outBytes, bRemoved ? 1 : 0 );
                return;
            }
            if ( ( name == "ZSCORE" || name == "ZREVRANK" ) && listArgument.size() == 3 )
            {
                if ( pEntry == nullptr )
                {
                    replyNull( outBytes );
                    return;
                }
                const sw::string                               member     = sw::string( listArgument[2].getText() );
                const sw::vector<std::pair<sw::string, int64>> listRanked = makeRanking( *pEntry );
                for ( size_t rankIndex = 0; rankIndex < listRanked.size(); ++rankIndex )
                {
                    if ( listRanked[rankIndex].first != member )
                        continue;
                    if ( name == "ZSCORE" )
                        replyBulkNumber( outBytes, listRanked[rankIndex].second );
                    else
                        replyInteger( outBytes, static_cast<int64>( rankIndex ) );
                    return;
                }
                replyNull( outBytes );
                return;
            }
            if ( name == "ZREVRANGE" && listArgument.size() == 5 )
            {
                int64 startIndex = 0;
                int64 stopIndex  = 0;
                (void)parseInteger( listArgument[2], startIndex ); // 가짜 서버 — 숫자가 아니면 0 으로 둔다
                (void)parseInteger( listArgument[3], stopIndex );  // 가짜 서버 — 숫자가 아니면 0 으로 둔다
                const sw::vector<std::pair<sw::string, int64>> listRanked =
                    pEntry != nullptr ? makeRanking( *pEntry ) : sw::vector<std::pair<sw::string, int64>>{};
                const int64 endIndex = std::min( stopIndex + 1, static_cast<int64>( listRanked.size() ) );
                const int64 count    = std::max( int64( 0 ), endIndex - startIndex );
                replyArrayHeader( outBytes, count * 2 );
                for ( int64 index = startIndex; index < endIndex; ++index )
                {
                    replyBulkText( outBytes, listRanked[static_cast<size_t>( index )].first );
                    replyBulkNumber( outBytes, listRanked[static_cast<size_t>( index )].second );
                }
                return;
            }
            replyError( outBytes, "ERR unknown command" );
        }

        sw::unordered_map<uint64, Connection> _mapConnection;
        sw::unordered_map<sw::string, Entry>  _mapEntry;
        sw::unordered_map<sw::string, int64>  _mapKeyVersion;
        sw::string                            _password;
        sw::LoopbackStreamNetwork*            _pNetwork;
        sw::unique_ptr<sw::IStreamTransport>  _transport;
        sw::ITlsContext*                      _pTlsContext;
        int64                                 _nowMs;
        int64                                 _nextVersion;
        int32                                 _acceptedCount;
        bool                                  _bHoldReplies;
    };

    inline int32 FakeRespPumpingTransport::pollIo( int32 timeoutMilli )
    {
        _pServer->pump();
        const int32 eventCount = _inner->pollIo( timeoutMilli );
        _pServer->pump();
        return eventCount;
    }
} // namespace test
