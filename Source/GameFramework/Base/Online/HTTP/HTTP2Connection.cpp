#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Connection.h"

#include "Core/Container/StringUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HTTP2ConnectionInternal
        {
            static constexpr uint16 kDefaultHTTPSPort         = 443;
            static constexpr uint32 kWindowMask               = 0x7FFFFFFFu; ///< WINDOW_UPDATE 증가량의 예약 비트를 뺀 마스크
            static constexpr size_t kPaddingLengthSize        = 1;
            static constexpr size_t kPrioritySize             = 5;
            static constexpr size_t kRstStreamBodySize        = 4;
            static constexpr int32  kStatusInformationalBegin = 100; ///< 1xx 는 최종 응답이 아니다
            static constexpr int32  kStatusInformationalEnd   = 200;

            /** @brief HTTP/2 가 연결 단위 머리라 요청에 실을 수 없는 이름입니다(소문자). */
            static bool isConnectionSpecificHeader( string_view lowerName )
            {
                return lowerName == "host" || lowerName == "connection" || lowerName == "content-length" || lowerName == "transfer-encoding" || lowerName == "keep-alive" ||
                       lowerName == "upgrade" || lowerName == "proxy-connection";
            }

            /** @brief `:method` · `:scheme` · `:authority` · `:path` 다음에 소문자 머리, 본문이 있으면 `content-length` 를 놓습니다. */
            static vector<HpackHeader> makeRequestHeaders( const HTTPClientRequest& request, const HTTPAddress& url )
            {
                vector<HpackHeader> listHeader;
                string              authority = url._host;
                if ( url._port != kDefaultHTTPSPort )
                    authority += ":" + to_string( static_cast<int32>( url._port ) );
                listHeader.push_back( HpackHeader{ ":method", toString( request._method ) } );
                listHeader.push_back( HpackHeader{ ":scheme", "https" } );
                listHeader.push_back( HpackHeader{ ":authority", authority } );
                listHeader.push_back( HpackHeader{ ":path", url._target } );
                for ( const HTTPHeader& header : request._listHeader )
                {
                    string name = StringUtil::toLower( header._name.c_str() );
                    if ( isConnectionSpecificHeader( name ) )
                        continue;
                    listHeader.push_back( HpackHeader{ std::move( name ), header._value } );
                }
                if ( request._bodyBytes.empty() == false || request._method == HTTPMethod::Post )
                    listHeader.push_back( HpackHeader{ "content-length", to_string( static_cast<uint64>( request._bodyBytes.size() ) ) } );
                return listHeader;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HTTP2Connection::HTTP2Connection( const HTTP2ConnectionConfig& settings )
        : _decoder{}
        , _settings{ settings }
        , _mapStream{}
        , _listQueued{}
        , _listDone{}
        , _inBytes{}
        , _listHeaderBlock{}
        , _headerStreamID{ 0 }
        , _connectionSendWindow{ kDefaultWindowSize }
        , _peerInitialWindow{ kDefaultWindowSize }
        , _peerMaxFrameSize{ HTTP2FrameUtil::kDefaultMaxFrameSize }
        , _peerMaxConcurrentStreams{ kUnlimitedStreams }
        , _nextStreamID{ 1 }
        , _continuationStreamID{ 0 }
        , _bHeaderEndStream{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _bGoAway{ SW_FALSE }
        , _bFailed{ SW_FALSE }
    {
    }

    bool HTTP2Connection::isUsable() const
    {
        return _bStarted == SW_TRUE && _bGoAway == SW_FALSE && _bFailed == SW_FALSE && _nextStreamID <= HTTP2FrameUtil::kMaxStreamID;
    }

    void HTTP2Connection::start( vector<uint8>& outBytes )
    {
        if ( _bStarted == SW_TRUE )
            return;
        _bStarted             = SW_TRUE;
        const uint8* pPreface = reinterpret_cast<const uint8*>( HTTP2FrameUtil::kConnectionPreface.data() );
        outBytes.insert( outBytes.end(), pPreface, pPreface + HTTP2FrameUtil::kConnectionPreface.size() );
        vector<HTTP2Setting> listSetting;
        listSetting.push_back( HTTP2Setting{ HTTP2FrameUtil::kSettingsEnablePush, 0 } ); // 푸시를 받지 않는다
        (void)HTTP2FrameUtil::writeSettings( listSetting, false, outBytes );             // 값이 상수라 범위 안이다
    }

    void HTTP2Connection::submit( uint64 requestID, const HTTPClientRequest& request, const HTTPAddress& url, vector<uint8>& outBytes )
    {
        if ( isUsable() == false )
        {
            pushFailure( requestID, _bGoAway == SW_TRUE ? "server is going away" : "HTTP/2 connection is not usable" );
            return;
        }
        Queued queued;
        queued._requestID = requestID;
        queued._bodyBytes = request._bodyBytes;
        if ( HpackEncoder::encode( HTTP2ConnectionInternal::makeRequestHeaders( request, url ), queued._listHeaderBlock ) == false )
        {
            pushFailure( requestID, "malformed request header" );
            return;
        }
        if ( queued._listHeaderBlock.size() > _peerMaxFrameSize )
        {
            pushFailure( requestID, "request headers exceed the HTTP/2 frame size" );
            return;
        }
        _listQueued.push_back( std::move( queued ) );
        promoteQueued( outBytes );
    }

    void HTTP2Connection::promoteQueued( vector<uint8>& outBytes )
    {
        while ( _listQueued.empty() == false && _mapStream.size() < _peerMaxConcurrentStreams )
        {
            Queued queued = std::move( _listQueued.front() );
            _listQueued.erase( _listQueued.begin() );
            if ( isUsable() == false )
            {
                pushFailure( queued._requestID, "HTTP/2 connection is not usable" );
                continue;
            }
            openStream( std::move( queued ), outBytes );
        }
        flushData( outBytes );
    }

    void HTTP2Connection::openStream( Queued&& queued, vector<uint8>& outBytes )
    {
        const uint32 streamID = _nextStreamID;
        const bool   bNoBody  = queued._bodyBytes.empty();
        if ( HTTP2FrameUtil::writeHeaders( streamID, queued._listHeaderBlock, bNoBody, _peerMaxFrameSize, outBytes ) == false )
        {
            pushFailure( queued._requestID, "request headers exceed the HTTP/2 frame size" );
            return;
        }
        _nextStreamID += 2;
        Stream& stream        = _mapStream[streamID];
        stream._requestID     = queued._requestID;
        stream._sendBytes     = std::move( queued._bodyBytes );
        stream._sendWindow    = _peerInitialWindow;
        stream._bRequestEnded = bNoBody ? SW_TRUE : SW_FALSE;
    }

    void HTTP2Connection::flushData( vector<uint8>& outBytes )
    {
        for ( auto& [streamID, stream] : _mapStream )
        {
            while ( stream._bRequestEnded == SW_FALSE )
            {
                const size_t remaining = stream._sendBytes.size() - stream._sendOffset;
                const int64  allowed   = std::min( { static_cast<int64>( remaining ), stream._sendWindow, _connectionSendWindow, static_cast<int64>( _peerMaxFrameSize ) } );
                if ( allowed <= 0 )
                    break; // 창이 찰 때까지 기다린다(WINDOW_UPDATE)
                const bool bLast = static_cast<size_t>( allowed ) == remaining;
                if ( HTTP2FrameUtil::writeData( streamID, stream._sendBytes.data() + stream._sendOffset, static_cast<size_t>( allowed ), bLast, _peerMaxFrameSize, outBytes ) == false )
                    break; // 범위는 위에서 맞췄다 — 닿지 않는다
                stream._sendOffset += static_cast<size_t>( allowed );
                stream._sendWindow -= allowed;
                _connectionSendWindow -= allowed;
                if ( bLast )
                {
                    stream._bRequestEnded = SW_TRUE;
                    stream._sendBytes.clear();
                    stream._sendBytes.shrink_to_fit();
                }
            }
        }
    }

    bool HTTP2Connection::feed( const uint8* pData, size_t size, vector<uint8>& outBytes, string& outError )
    {
        if ( _bFailed == SW_TRUE )
        {
            outError = "HTTP/2 connection already failed";
            return false;
        }
        _inBytes.insert( _inBytes.end(), pData, pData + size );
        size_t offset = 0;
        for ( ;; )
        {
            HTTP2FrameHeader header;
            if ( HTTP2FrameUtil::readHeader( _inBytes.data() + offset, _inBytes.size() - offset, header ) == false )
                break;
            if ( header._length > HTTP2FrameUtil::kDefaultMaxFrameSize )
                return failConnection( "frame is larger than the advertised maximum", outError ); // 우리는 MAX_FRAME_SIZE 를 올리지 않았다
            if ( _inBytes.size() - offset < HTTP2FrameUtil::kHeaderSize + header._length )
                break;
            const uint8* pBody = _inBytes.data() + offset + HTTP2FrameUtil::kHeaderSize;
            if ( handleFrame( header, pBody, outBytes, outError ) == false )
                return false;
            offset += HTTP2FrameUtil::kHeaderSize + header._length;
        }
        _inBytes.erase( _inBytes.begin(), _inBytes.begin() + static_cast<ptrdiff_t>( offset ) );
        promoteQueued( outBytes ); // 스트림이 끝나 줄 선 요청이 열릴 수 있고, 창이 늘어 본문을 더 보낼 수 있다
        return true;
    }

    bool HTTP2Connection::handleFrame( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError )
    {
        if ( _continuationStreamID != 0 && ( header._type != static_cast<uint8>( HTTP2FrameType::Continuation ) || header._streamID != _continuationStreamID ) )
            return failConnection( "expected CONTINUATION", outError );
        switch ( static_cast<HTTP2FrameType>( header._type ) )
        {
            case HTTP2FrameType::Settings:
            {
                return handleSettings( header, pBody, outBytes, outError );
            }
            case HTTP2FrameType::WindowUpdate:
            {
                return handleWindowUpdate( header, pBody, outError );
            }
            case HTTP2FrameType::Headers:
            {
                return handleHeaders( header, pBody, outBytes, outError );
            }
            case HTTP2FrameType::Continuation:
            {
                return handleContinuation( header, pBody, outBytes, outError );
            }
            case HTTP2FrameType::Data:
            {
                return handleData( header, pBody, outBytes, outError );
            }
            case HTTP2FrameType::Ping:
            {
                if ( header._streamID != 0 || header._length != kPingOpaqueSize )
                    return failConnection( "malformed PING", outError );
                if ( ( header._flags & HTTP2FrameUtil::kFlagAck ) != 0 )
                    return true;
                uint8 arrOpaque[kPingOpaqueSize];
                for ( size_t index = 0; index < kPingOpaqueSize; ++index )
                {
                    arrOpaque[index] = pBody[index];
                }
                (void)HTTP2FrameUtil::writePing( arrOpaque, true, outBytes ); // 길이가 고정이라 범위 안이다
                return true;
            }
            case HTTP2FrameType::RstStream:
            {
                if ( header._streamID == 0 || header._length != HTTP2ConnectionInternal::kRstStreamBodySize )
                    return failConnection( "malformed RST_STREAM", outError );
                const uint32 errorCode = HTTP2FrameUtil::readUint32( pBody );
                failStream( header._streamID, ( "stream was reset by the server (error " + to_string( errorCode ) + ")" ).c_str(), false, 0, outBytes );
                return true;
            }
            case HTTP2FrameType::GoAway:
            {
                HTTP2GoAway goAway;
                if ( HTTP2FrameUtil::readGoAway( pBody, header._length, goAway ) == false )
                    return failConnection( "malformed GOAWAY", outError );
                handleGoAway( goAway );
                return true;
            }
            case HTTP2FrameType::PushPromise:
            {
                return failConnection( "server push was not enabled", outError );
            }
            case HTTP2FrameType::Priority:
            {
                return true; // 우선순위는 쓰지 않는다
            }
        }
        return true; // 모르는 종류는 무시한다(RFC 9113 4.1)
    }

    bool HTTP2Connection::handleSettings( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError )
    {
        if ( header._streamID != 0 )
            return failConnection( "SETTINGS on a stream", outError );
        if ( ( header._flags & HTTP2FrameUtil::kFlagAck ) != 0 )
            return header._length == 0 ? true : failConnection( "SETTINGS acknowledgement has a body", outError );
        vector<HTTP2Setting> listSetting;
        if ( HTTP2FrameUtil::readSettings( pBody, header._length, listSetting ) == false )
            return failConnection( "malformed SETTINGS", outError );
        for ( const HTTP2Setting& setting : listSetting )
        {
            if ( setting._id == HTTP2FrameUtil::kSettingsInitialWindowSize )
            {
                if ( setting._value > HTTP2ConnectionInternal::kWindowMask )
                    return failConnection( "initial window size is out of range", outError );
                const int64 delta  = static_cast<int64>( setting._value ) - _peerInitialWindow;
                _peerInitialWindow = setting._value;
                for ( auto& [streamID, stream] : _mapStream )
                {
                    (void)streamID;
                    stream._sendWindow += delta;
                    if ( stream._sendWindow > kMaxWindowSize )
                        return failConnection( "stream window overflowed", outError );
                }
            }
            else if ( setting._id == HTTP2FrameUtil::kSettingsMaxFrameSize )
            {
                if ( setting._value < kMinMaxFrameSize || setting._value > kMaxMaxFrameSize )
                    return failConnection( "max frame size is out of range", outError );
                _peerMaxFrameSize = setting._value;
            }
            else if ( setting._id == HTTP2FrameUtil::kSettingsMaxConcurrentStreams )
            {
                _peerMaxConcurrentStreams = setting._value;
            }
            // HEADER_TABLE_SIZE 는 부호기가 동적 표를 쓰지 않아 상관없고, ENABLE_PUSH 와 모르는 항목은 무시한다
        }
        (void)HTTP2FrameUtil::writeSettings( {}, true, outBytes ); // 확인
        return true;
    }

    bool HTTP2Connection::handleWindowUpdate( const HTTP2FrameHeader& header, const uint8* pBody, string& outError )
    {
        if ( header._length != sizeof( uint32 ) )
            return failConnection( "malformed WINDOW_UPDATE", outError );
        const uint32 increment = HTTP2FrameUtil::readUint32( pBody ) & HTTP2ConnectionInternal::kWindowMask;
        if ( increment == 0 )
            return failConnection( "WINDOW_UPDATE with a zero increment", outError );
        if ( header._streamID == 0 )
        {
            _connectionSendWindow += increment;
            return _connectionSendWindow <= kMaxWindowSize ? true : failConnection( "connection window overflowed", outError );
        }
        const auto streamIt = _mapStream.find( header._streamID );
        if ( streamIt == _mapStream.end() )
            return true; // 끝난 스트림
        streamIt->second._sendWindow += increment;
        if ( streamIt->second._sendWindow > kMaxWindowSize )
            return failConnection( "stream window overflowed", outError );
        return true;
    }

    bool HTTP2Connection::handleHeaders( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError )
    {
        using Internal              = HTTP2ConnectionInternal;
        const bool bNotClientStream = header._streamID == 0 || ( header._streamID & 1u ) == 0; // 요청 스트림은 홀수다
        if ( bNotClientStream || header._streamID >= _nextStreamID )
            return failConnection( "HEADERS on an invalid stream", outError );
        size_t offset  = 0;
        size_t padding = 0;
        if ( ( header._flags & HTTP2FrameUtil::kFlagPadded ) != 0 )
        {
            if ( header._length < Internal::kPaddingLengthSize )
                return failConnection( "malformed HEADERS padding", outError );
            padding = pBody[0];
            offset += Internal::kPaddingLengthSize;
        }
        if ( ( header._flags & HTTP2FrameUtil::kFlagPriority ) != 0 )
            offset += Internal::kPrioritySize;
        if ( header._length < offset + padding )
            return failConnection( "malformed HEADERS", outError );
        _listHeaderBlock.assign( pBody + offset, pBody + header._length - padding );
        _headerStreamID   = header._streamID;
        _bHeaderEndStream = ( header._flags & HTTP2FrameUtil::kFlagEndStream ) != 0 ? SW_TRUE : SW_FALSE;
        if ( static_cast<int32>( _listHeaderBlock.size() ) > _settings._maxHeaderBlockBytes )
            return failConnection( "header block is too large", outError );
        if ( ( header._flags & HTTP2FrameUtil::kFlagEndHeaders ) != 0 )
            return finishHeaderBlock( outBytes, outError );
        _continuationStreamID = header._streamID;
        return true;
    }

    bool HTTP2Connection::handleContinuation( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError )
    {
        if ( _continuationStreamID == 0 )
            return failConnection( "CONTINUATION without HEADERS", outError );
        _listHeaderBlock.insert( _listHeaderBlock.end(), pBody, pBody + header._length );
        if ( static_cast<int32>( _listHeaderBlock.size() ) > _settings._maxHeaderBlockBytes )
            return failConnection( "header block is too large", outError );
        if ( ( header._flags & HTTP2FrameUtil::kFlagEndHeaders ) == 0 )
            return true;
        _continuationStreamID = 0;
        return finishHeaderBlock( outBytes, outError );
    }

    bool HTTP2Connection::finishHeaderBlock( vector<uint8>& outBytes, string& outError )
    {
        vector<HpackHeader> listHeader;
        string              decodeError;
        const bool          bDecoded = _decoder.decode( _listHeaderBlock.data(), _listHeaderBlock.size(), listHeader, decodeError ); // 동적 표가 바뀐다 — 스트림을 알든 모르든 읽어야 한다
        _listHeaderBlock.clear();
        if ( bDecoded == false )
            return failConnection( ( "header block could not be decoded: " + decodeError ).c_str(), outError );
        const auto streamIt = _mapStream.find( _headerStreamID );
        if ( streamIt == _mapStream.end() )
            return true; // 끝난 스트림
        Stream& stream = streamIt->second;
        if ( stream._bHeadDone == SW_TRUE )
        {
            // 트레일러 — 읽어서 표 상태만 맞추고 버린다
            if ( _bHeaderEndStream == SW_TRUE )
                completeStream( _headerStreamID, outBytes );
            return true;
        }
        bool               bHasStatus = false;
        int32              status     = 0;
        vector<HTTPHeader> listRegular;
        for ( HpackHeader& entry : listHeader )
        {
            if ( entry._name == ":status" )
                bHasStatus = StringUtil::parseInt( entry._value, status );
            else if ( entry._name.empty() == false && entry._name[0] != ':' )
                listRegular.push_back( HTTPHeader{ std::move( entry._name ), std::move( entry._value ) } );
        }
        if ( bHasStatus == false )
        {
            failStream( _headerStreamID, "response has no :status", true, HTTP2ErrorCode::kProtocolError, outBytes );
            return true;
        }
        if ( status >= HTTP2ConnectionInternal::kStatusInformationalBegin && status < HTTP2ConnectionInternal::kStatusInformationalEnd )
            return true; // 1xx — 최종 응답이 아니다
        stream._statusCode = status;
        stream._listHeader = std::move( listRegular );
        stream._bHeadDone  = SW_TRUE;
        if ( _bHeaderEndStream == SW_TRUE )
            completeStream( _headerStreamID, outBytes );
        return true;
    }

    bool HTTP2Connection::handleData( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError )
    {
        using Internal = HTTP2ConnectionInternal;
        if ( header._streamID == 0 )
            return failConnection( "DATA on the connection", outError );
        size_t offset  = 0;
        size_t padding = 0;
        if ( ( header._flags & HTTP2FrameUtil::kFlagPadded ) != 0 )
        {
            if ( header._length < Internal::kPaddingLengthSize )
                return failConnection( "malformed DATA padding", outError );
            padding = pBody[0];
            offset  = Internal::kPaddingLengthSize;
        }
        if ( header._length < offset + padding )
            return failConnection( "malformed DATA", outError );
        const size_t dataSize = header._length - offset - padding;
        if ( header._length > 0 )
            (void)HTTP2FrameUtil::writeWindowUpdate( 0, header._length, outBytes ); // 채움을 포함한 길이가 창을 쓴다 — 바로 돌려준다
        const auto streamIt = _mapStream.find( header._streamID );
        if ( streamIt == _mapStream.end() )
            return true; // 끝난 스트림 — 연결 창만 돌려주었다
        Stream& stream = streamIt->second;
        if ( stream._bHeadDone == SW_FALSE )
        {
            failStream( header._streamID, "DATA before the response headers", true, HTTP2ErrorCode::kProtocolError, outBytes );
            return true;
        }
        if ( stream._responseBytes.size() + dataSize > static_cast<size_t>( _settings._maxResponseBodyBytes ) )
        {
            failStream( header._streamID, "response body is too large", true, HTTP2ErrorCode::kCancel, outBytes );
            return true;
        }
        stream._responseBytes.insert( stream._responseBytes.end(), pBody + offset, pBody + offset + dataSize );
        if ( ( header._flags & HTTP2FrameUtil::kFlagEndStream ) != 0 )
            completeStream( header._streamID, outBytes );
        else if ( header._length > 0 )
            (void)HTTP2FrameUtil::writeWindowUpdate( header._streamID, header._length, outBytes ); // 길이가 0 보다 크고 프레임 상한 이하라 범위 안이다
        return true;
    }

    void HTTP2Connection::handleGoAway( const HTTP2GoAway& goAway )
    {
        _bGoAway = SW_TRUE;
        vector<uint32> listRefused;
        for ( const auto& [streamID, stream] : _mapStream )
        {
            (void)stream;
            if ( streamID > goAway._lastStreamID )
                listRefused.push_back( streamID );
        }
        vector<uint8> listUnused; // 상대가 이 스트림을 처리하지 않았다 — 취소를 알릴 필요가 없다
        for ( const uint32 streamID : listRefused )
        {
            failStream( streamID, "server is going away", false, 0, listUnused );
        }
        for ( const Queued& queued : _listQueued )
        {
            pushFailure( queued._requestID, "server is going away" );
        }
        _listQueued.clear();
    }

    void HTTP2Connection::completeStream( uint32 streamID, vector<uint8>& outBytes )
    {
        const auto streamIt = _mapStream.find( streamID );
        if ( streamIt == _mapStream.end() )
            return;
        Stream&             stream   = streamIt->second;
        HTTPClientResponse& response = _listDone.emplace_back();
        response._requestID          = stream._requestID;
        response._statusCode         = stream._statusCode;
        response._listHeader         = std::move( stream._listHeader );
        response._bodyBytes          = std::move( stream._responseBytes );
        if ( stream._bRequestEnded == SW_FALSE )
            (void)HTTP2FrameUtil::writeRstStream( streamID, HTTP2ErrorCode::kNoError, outBytes ); // 응답이 먼저 끝났다 — 남은 본문은 보내지 않는다(RFC 9113 8.1)
        _mapStream.erase( streamIt );
    }

    void HTTP2Connection::failStream( uint32 streamID, const utf8* pFailure, bool bResetPeer, uint32 errorCode, vector<uint8>& outBytes )
    {
        const auto streamIt = _mapStream.find( streamID );
        if ( streamIt == _mapStream.end() )
            return;
        pushFailure( streamIt->second._requestID, pFailure );
        if ( bResetPeer )
            (void)HTTP2FrameUtil::writeRstStream( streamID, errorCode, outBytes ); // 스트림 번호가 0 이 아니라 범위 안이다
        _mapStream.erase( streamIt );
    }

    void HTTP2Connection::pushFailure( uint64 requestID, const utf8* pFailure )
    {
        HTTPClientResponse& response = _listDone.emplace_back();
        response._requestID          = requestID;
        response._bTransportFailed   = SW_TRUE;
        response._failureText        = pFailure;
    }

    bool HTTP2Connection::failConnection( const utf8* pReason, string& outError )
    {
        outError = pReason;
        failAll( pReason );
        return false;
    }

    void HTTP2Connection::cancel( uint64 requestID, const utf8* pFailure, vector<uint8>& outBytes )
    {
        for ( const auto& [streamID, stream] : _mapStream )
        {
            if ( stream._requestID != requestID )
                continue;
            failStream( streamID, pFailure, true, HTTP2ErrorCode::kCancel, outBytes );
            return; // 지운 뒤라 순회를 바로 끝낸다
        }
        for ( size_t index = 0; index < _listQueued.size(); ++index )
        {
            if ( _listQueued[index]._requestID != requestID )
                continue;
            pushFailure( requestID, pFailure );
            _listQueued.erase( _listQueued.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    void HTTP2Connection::failAll( const utf8* pFailure )
    {
        _bFailed = SW_TRUE;
        for ( const auto& [streamID, stream] : _mapStream )
        {
            (void)streamID;
            pushFailure( stream._requestID, pFailure );
        }
        _mapStream.clear();
        for ( const Queued& queued : _listQueued )
        {
            pushFailure( queued._requestID, pFailure );
        }
        _listQueued.clear();
    }

    int32 HTTP2Connection::takeResponses( vector<HTTPClientResponse>& outListResponse )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( HTTPClientResponse& response : _listDone )
        {
            outListResponse.push_back( std::move( response ) );
        }
        _listDone.clear();
        return count;
    }
} // namespace sw
