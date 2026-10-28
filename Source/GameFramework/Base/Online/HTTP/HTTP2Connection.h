/**
 * @file HTTP2Connection.h
 * @brief HTTP/2 클라이언트 연결 하나의 상태 기계입니다(RFC 9113). 연결 서문 · SETTINGS · 스트림 · 흐름 제어 · PING · GOAWAY 를 다루고 요청마다 응답 하나를 만듭니다(온라인 키트 G4, H2-4).
 * @details - 소켓과 TLS 를 모른다. 받은 평문 바이트를 `feed` 로 넣으면 보낼 바이트가 `outBytes` 뒤에 붙고, 끝난 응답은 `takeResponses` 로 꺼낸다(`HTTP2Client` 가 길을 잇는다). 시계도 모른다 — 시한은 부르는 쪽이 `cancel` 로 건다.
 *          - 요청 하나는 응답 하나로 반드시 끝난다(실패도 `_bTransportFailed` 응답). 같은 요청 id 로 두 번 끝나지 않는다.
 *          - 동시 스트림 상한(상대의 SETTINGS)을 넘는 요청은 줄을 서고 스트림이 끝나면 열린다. 연결 · 스트림의 보내기 창이 모자라면 본문은 WINDOW_UPDATE 가 올 때까지 기다린다.
 *          - 받은 DATA 는 쓰는 대로 곧바로 창을 돌려준다(연결 · 스트림 WINDOW_UPDATE). 응답 본문이 상한(`_maxResponseBodyBytes`)을 넘으면 그 스트림만 취소한다.
 *          - 부호기는 동적 표를 쓰지 않으므로 상대의 HEADER_TABLE_SIZE 에 기대지 않는다. 헤더 블록 하나가 프레임 상한(상대의 MAX_FRAME_SIZE)을 넘으면 그 요청은 실패다(CONTINUATION 쓰기는 없다).
 *          - 연결 오류(틀린 프레임 · 압축 상태 오류 · 흐름 제어 위반)는 `feed` 가 false 와 이유를 주고 모든 요청을 실패로 끝낸다 — 부르는 쪽이 전송을 닫는다. 푸시는 켜지 않는다(PUSH_PROMISE 는 연결 오류).
 *          한 스레드가 쓴다(`HTTP2Client` 가 잠금 안에서 부른다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief HTTP/2 오류 코드입니다(RFC 9113 7절). */
    struct HTTP2ErrorCode
    {
        static constexpr uint32 kNoError          = 0;
        static constexpr uint32 kProtocolError    = 1;
        static constexpr uint32 kFlowControlError = 3;
        static constexpr uint32 kFrameSizeError   = 6;
        static constexpr uint32 kCancel           = 8;
        static constexpr uint32 kCompressionError = 9;
    };
} // namespace sw

namespace sw
{
    /** @brief 연결 설정입니다. */
    struct HTTP2ConnectionConfig
    {
        int32 _maxResponseBodyBytes{ HTTPConstant::kDefaultMaxBodyBytes };
        int32 _maxHeaderBlockBytes{ 64 * 1024 }; ///< 받는 헤더 블록(HEADERS + CONTINUATION)의 합 상한 — 넘으면 연결 오류
    };
} // namespace sw

namespace sw
{
    /**
     * @class HTTP2Connection
     * @brief 클라이언트 쪽 HTTP/2 연결입니다.
     */
    class SW_GF_API HTTP2Connection
    {
    public:
        static constexpr uint32 kDefaultWindowSize = 65535; ///< 연결 · 스트림 창의 처음 크기(RFC 9113 6.9.2)
        static constexpr int64  kMaxWindowSize     = 0x7FFFFFFF;
        static constexpr uint32 kMinMaxFrameSize   = 16384;
        static constexpr uint32 kMaxMaxFrameSize   = 0xFFFFFFu;
        static constexpr uint32 kUnlimitedStreams  = 0xFFFFFFFFu;
        static constexpr size_t kPingOpaqueSize    = 8;

        explicit HTTP2Connection( const HTTP2ConnectionConfig& settings );

        HTTP2Connection( const HTTP2Connection& )            = delete;
        HTTP2Connection& operator=( const HTTP2Connection& ) = delete;

        /** @brief 연결 서문과 첫 SETTINGS(푸시 끔)를 @p outBytes 에 씁니다. TLS 가 선 뒤 한 번 부릅니다. */
        void start( vector<uint8>& outBytes );
        /**
         * @brief 요청 하나를 보냅니다(동시 스트림 상한이면 줄을 선다). 보내지 못하는 요청(연결이 쓸 수 없음 · 헤더가 프레임을 넘음)은 실패 응답으로 끝납니다.
         * @param requestID 0 이 아닌 요청 id(응답의 `_requestID`). @param url 는 `https://` 로 나눈 주소(`:authority` · `:path` 가 된다).
         */
        void submit( uint64 requestID, const HTTPClientRequest& request, const HTTPAddress& url, vector<uint8>& outBytes );
        /** @brief 받은 평문 바이트를 먹입니다. false 면 연결 오류이고 @p outError 에 이유가 있으며 요청은 모두 실패로 끝났습니다. */
        [[nodiscard]] bool feed( const uint8* pData, size_t size, vector<uint8>& outBytes, string& outError );
        /** @brief 요청 하나를 실패(@p pFailure)로 끝내고 열린 스트림이면 RST_STREAM(CANCEL)을 씁니다. 모르는 요청 id 는 무시합니다. */
        void cancel( uint64 requestID, const utf8* pFailure, vector<uint8>& outBytes );
        /** @brief 남은 요청을 모두 실패로 끝내고 연결을 쓸 수 없게 합니다(전송이 닫혔다). */
        void failAll( const utf8* pFailure );
        /** @brief 끝난 응답을 @p outListResponse 뒤에 붙입니다. 붙인 수입니다. */
        int32 takeResponses( vector<HTTPClientResponse>& outListResponse );

        /** @brief 새 요청을 받을 수 있는가 — 시작했고 GOAWAY · 연결 오류가 없고 스트림 id 가 남았다. */
        bool isUsable() const;
        bool isStarted() const { return _bStarted == SW_TRUE; }
        bool hasGoneAway() const { return _bGoAway == SW_TRUE; }
        /** @brief 열린 스트림과 줄 선 요청의 합입니다. */
        int32 getActiveCount() const { return static_cast<int32>( _mapStream.size() + _listQueued.size() ); }
        /** @brief 지금 보낼 수 있는 연결 창(상대가 허락한 남은 바이트)입니다. */
        int64  getConnectionSendWindow() const { return _connectionSendWindow; }
        uint32 getPeerMaxFrameSize() const { return _peerMaxFrameSize; }

    private:
        struct Stream
        {
            vector<HTTPHeader> _listHeader{};
            vector<uint8>      _responseBytes{};
            vector<uint8>      _sendBytes{}; ///< 아직 보내지 않은 요청 본문(앞 `_sendOffset` 바이트는 보냈다)
            size_t             _sendOffset{ 0 };
            int64              _sendWindow{ 0 };
            uint64             _requestID{ 0 };
            int32              _statusCode{ 0 };
            uint8              _bHeadDone{ SW_FALSE };
            uint8              _bRequestEnded{ SW_FALSE }; ///< END_STREAM 을 보냈다
        };

        struct Queued
        {
            vector<uint8> _listHeaderBlock{};
            vector<uint8> _bodyBytes{};
            uint64        _requestID{ 0 };
        };

        void               openStream( Queued&& queued, vector<uint8>& outBytes );
        void               promoteQueued( vector<uint8>& outBytes );
        void               flushData( vector<uint8>& outBytes );
        [[nodiscard]] bool handleFrame( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError );
        [[nodiscard]] bool handleSettings( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError );
        [[nodiscard]] bool handleWindowUpdate( const HTTP2FrameHeader& header, const uint8* pBody, string& outError );
        [[nodiscard]] bool handleHeaders( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError );
        [[nodiscard]] bool handleContinuation( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError );
        [[nodiscard]] bool handleData( const HTTP2FrameHeader& header, const uint8* pBody, vector<uint8>& outBytes, string& outError );
        [[nodiscard]] bool finishHeaderBlock( vector<uint8>& outBytes, string& outError );
        void               handleGoAway( const HTTP2GoAway& goAway );
        void               completeStream( uint32 streamID, vector<uint8>& outBytes );
        /** @brief 스트림 하나를 실패 응답으로 끝내고 지웁니다. @p bResetPeer 면 RST_STREAM(@p errorCode)을 씁니다. */
        void               failStream( uint32 streamID, const utf8* pFailure, bool bResetPeer, uint32 errorCode, vector<uint8>& outBytes );
        void               pushFailure( uint64 requestID, const utf8* pFailure );
        [[nodiscard]] bool failConnection( const utf8* pReason, string& outError );

        HpackDecoder                  _decoder;
        HTTP2ConnectionConfig         _settings;
        unordered_map<uint32, Stream> _mapStream;
        vector<Queued>                _listQueued;
        vector<HTTPClientResponse>    _listDone;
        vector<uint8>                 _inBytes;
        vector<uint8>                 _listHeaderBlock; ///< 이어 받는 헤더 블록
        uint32                        _headerStreamID;
        int64                         _connectionSendWindow;
        int64                         _peerInitialWindow;
        uint32                        _peerMaxFrameSize;
        uint32                        _peerMaxConcurrentStreams;
        uint32                        _nextStreamID;
        uint32                        _continuationStreamID; ///< 0 이 아니면 CONTINUATION 만 와야 한다
        uint8                         _bHeaderEndStream;
        uint8                         _bStarted;
        uint8                         _bGoAway;
        uint8                         _bFailed;
    };
} // namespace sw
