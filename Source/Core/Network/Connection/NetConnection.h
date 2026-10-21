/**
 * @file NetConnection.h
 * @brief 한 상대와의 신뢰성 계층을 구현합니다.
 * @details 패킷 시퀀스와 확인(ack와 32비트 ack 비트), 채널별 메시지(신뢰 순서, 신뢰 순서 없음, 순서만, 비신뢰), 재전송, RTT와 손실률과 대역폭 통계를 다룬다.
 *          주소, 핸드셰이크, 타임아웃은 `NetHost` 가 맡고, 이 클래스는 "연결된 뒤 패킷 한 개의 본문"만 쓴다(Gaffer 의 "Reliability and Congestion Avoidance over UDP").
 *
 *          패킷마다 그 패킷에 실은 신뢰 메시지 ID를 기억해 두었다가, 패킷이 확인되면 메시지도 확인된 것으로 본다.
 *          확인되지 않은 신뢰 메시지는 RTT + 50ms(최소 0.1초)가 지나면 다음 패킷에 다시 싣는다.
 *          그보다 먼저, 뒤에 보낸 패킷이 `kFastResendGap` 개를 넘게 확인됐는데 확인이 없는 패킷은 잃은 것으로 보고 그 신뢰 메시지를 바로 다시 싣는다.
 *          이것이 빠른 재전송이다. TCP 의 중복 ACK 3개나 NACK 에 해당하며, 순서가 바뀐 패킷은 그 간격 안에서 견딘다.
 *
 *          신뢰 순서 메시지는 `kMaxReliableMessageSize`(64KB)까지 보낼 수 있다.
 *          `kMaxSingleMessageSize`(1KB) 조각으로 나눠 조각마다 신뢰 ID 하나를 쓰므로 재전송, 확인, 창이 모두 조각 단위다.
 *          조각 헤더의 1비트는 "다음 ID가 같은 메시지"라는 표시다. 받는 쪽은 순서대로 모아 마지막 조각에서 한 메시지로 넘긴다(언리얼의 partial bunch).
 *          다른 채널은 조각나지 않으므로 `kMaxSingleMessageSize` 까지만 보낼 수 있다.
 *          상한을 넘으면 오류 로그를 남기고 false 를 반환한다. 창이 가득 찬 것은 오류가 아니므로 로그 없이 false 를 반환한다.
 *          조각은 패킷을 거의 채우므로, 앞 패킷이 순서만과 비신뢰 메시지를 남겼으면 다음 패킷은 그쪽부터 싣는다. 큰 전송이 스냅샷을 굶기지 않게 번갈아 싣는 것이다.
 *
 *          신뢰 순서 없음은 신뢰 순서와 ID, 창, 재전송을 같이 쓴다.
 *          받는 쪽은 받는 대로 넘기고 그 슬롯에 "넘겼다"는 표시만 남긴다. 순서 커서는 그 표시를 지나가고, 늦게 온 중복은 표시가 걸러 낸다.
 *
 *          패킷 끝의 1비트는 "확인을 바로 돌려 달라"는 요청이다.
 *          메시지를 실었거나 확인할 것이 없던(연결 유지) 패킷만 이 비트를 켜고, 확인만 담은 답은 끈다.
 *          그래야 한가할 때 양쪽이 확인에 확인으로 끝없이 주고받지 않고 `NetHostConfig::_keepAliveInterval` 마다만 오간다.
 *          RTT와 손실률도 이 비트를 켠 패킷으로만 측정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/SequenceBuffer.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 연결 통계입니다. */
    struct NetConnectionStats
    {
        float32 _rtt{ 0.0f };           ///< 초 단위(평활화한 값).
        float32 _packetLoss{ 0.0f };    ///< 0~1.
        float32 _sentBandwidth{ 0.0f }; ///< 초당 킬로비트.
        float32 _receivedBandwidth{ 0.0f };
        uint64  _sentPacketCount{ 0 };
        uint64  _receivedPacketCount{ 0 };
        uint64  _ackedPacketCount{ 0 };
        uint64  _resentMessageCount{ 0 };
        uint64  _expiredUnreliableCount{ 0 }; ///< 줄에서 나이 상한을 넘겨 버린 비신뢰 메시지(대역폭이 포화돼 오래 기다린 것).
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetConnection
     * @brief 보낼 메시지를 채널에 쌓고(`sendMessage`), 매 송신마다 `writePacket` 이 시퀀스, 확인, 메시지를 한 패킷에 담습니다.
     *        받은 패킷은 `readPacket` 이 읽어 채널별 수신 큐에 넣고, 게임은 `receiveMessage` 로 꺼냅니다.
     */
    class SW_API NetConnection
    {
    public:
        static constexpr int32   kMaxSingleMessageSize      = 1024;      ///< 조각나지 않는 메시지(순서만, 비신뢰)와 신뢰 순서 조각 하나의 상한. 패킷 하나에 통째로 들어간다.
        static constexpr int32   kMaxReliableMessageSize    = 64 * 1024; ///< 신뢰 순서 메시지 하나의 상한. `kMaxSingleMessageSize` 크기 조각으로 나눠 보내고, 받는 쪽이 모아서 넘긴다.
        static constexpr int32   kMessageSizeBits           = 11;        ///< 0~1024. 메시지 또는 조각 하나의 길이 필드 비트 수.
        static constexpr float64 kDefaultAckRequestInterval = 0.25;
        static constexpr int32   kMaxReliablePerPacket      = 64;  ///< 패킷 하나에 싣는 신뢰 메시지 상한(보낸 패킷 기록이 힙을 쓰지 않게 한다).
        static constexpr int32   kReliableWindow            = 256; ///< 확인되지 않은 신뢰 메시지의 상한. 넘으면 `sendMessage` 가 거절한다(막힘). 2 의 거듭제곱(`SequenceBuffer`).
        static constexpr uint32  kPacketWindow              = 256; ///< 보낸 · 받은 패킷을 기억하는 슬롯 수. 2 의 거듭제곱(`SequenceBuffer`).
        static constexpr int32   kPacketHeaderBits          = 16 + 16 + 32;
        static constexpr int32   kFastResendGap             = 3;    ///< 이만큼 뒤의 패킷이 확인됐는데 확인이 없으면 잃은 것으로 본다.
        static constexpr float64 kResendMargin              = 0.05; ///< 재전송 간격은 RTT에 이 여유를 더한 값이다(흔들림과 상대의 보내기 간격을 흡수한다).
        static constexpr int32   kMaxSpareBuffers           = 64;   ///< 다시 쓸 메시지 버퍼를 이만큼까지 둔다(조각 하나 크기라 연결 하나에 64KB 안쪽).
        /**
         * @brief 비신뢰 메시지가 보낼 줄에서 기다릴 수 있는 시간(초)의 기본값입니다. 넘으면 싣지 않고 버립니다.
         * @details 대역폭이 오래 포화되면 옛 비신뢰가 쌓였다가 몰려 나간다. 비신뢰는 늦으면 쓸모가 없으므로(위치 · 효과) 버리는 쪽이 맞다.
         *          언리얼은 포화된 연결(`IsNetReady` 거짓)에 비신뢰 번치를 아예 싣지 않는다. 30 Hz 기준 8 틱쯤이다.
         */
        static constexpr float64 kDefaultMaxUnreliableAge = 0.25;

        static_assert( ( 1 << kMessageSizeBits ) > kMaxSingleMessageSize, "the length field must hold one whole fragment" );
        static_assert( kMaxReliableMessageSize % kMaxSingleMessageSize == 0 && kMaxReliableMessageSize / kMaxSingleMessageSize < kReliableWindow,
                       "a whole reliable message must fit in the reliable window" );

        NetConnection();

        void reset();
        /**
         * @brief 메시지를 채널에 넣습니다. 채널 상한(`getMaxMessageSize`)을 넘으면 오류 로그와 함께 false 를 반환하고,
         *        신뢰 창이 메시지 전체(조각 모두)를 받을 수 없으면 로그 없이 false 를 반환합니다. 둘 다 아무것도 넣지 않으므로 반쪽 메시지가 생기지 않습니다.
         */
        [[nodiscard]] bool sendMessage( NetChannelType channel, const uint8* pData, int32 size );
        /** @brief 채널 하나의 메시지 상한입니다. 신뢰 순서는 `kMaxReliableMessageSize`, 나머지는 `kMaxSingleMessageSize` 입니다. */
        static int32 getMaxMessageSize( NetChannelType channel );
        /** @brief 받은 메시지 하나를 꺼냅니다. 없으면 false 를 반환합니다. @p outBuffer 의 옛 용량은 연결이 받아 다시 쓴다(같은 버퍼로 계속 받으면 할당이 없다). */
        [[nodiscard]] bool receiveMessage( NetChannelType channel, vector<uint8>& outBuffer );
        /** @brief 받은 메시지 하나를 꺼내 @p inoutBytes 뒤에 덧붙입니다. 여러 메시지를 아레나 하나에 모을 때 중간 버퍼 복사가 없습니다. 없으면 false 이고 그대로 둡니다. */
        [[nodiscard]] bool receiveMessageAppend( NetChannelType channel, vector<uint8>& inoutBytes );

        /**
         * @brief 패킷 본문을 씁니다. 헤더(시퀀스, ack, ack 비트), 들어가는 만큼의 메시지, 확인 요청 비트 순서입니다.
         * @param ackRequestInterval 확인만 담은 답이라도 마지막 확인 요청에서 이 간격이 지났으면 확인을 요청합니다.
         *                           양쪽의 연결 유지 시각이 맞물려 한쪽이 늘 답만 보내게 되어도 그쪽 RTT 를 측정하기 위해서입니다(`NetHostConfig::_keepAliveInterval`).
         */
        void writePacket( float64 time, BitWriter& writer, int32 maxBytes, float64 ackRequestInterval = kDefaultAckRequestInterval,
                          float64 maxUnreliableAgeSeconds = kDefaultMaxUnreliableAge );
        /** @brief 패킷 본문을 읽습니다. 깨졌거나 오래된 중복이면 false 를 반환합니다. */
        [[nodiscard]] bool readPacket( float64 time, BitReader& reader );
        /** @brief 보낼 것이 있는지 반환합니다. 새 메시지, 재전송 시각이 된 신뢰 메시지, 확인해 줄 패킷이 해당합니다. */
        bool hasDataToSend( float64 time ) const;
        /** @brief 받은 패킷을 확인해 줘야 하는지 반환합니다. 대역폭 토큰을 다 쓴 호스트가 메시지 없이 확인만 보낼지 정할 때 씁니다. */
        bool isAckPending() const { return _bAckPending != SW_FALSE; }

        const NetConnectionStats& getStats() const { return _stats; }
        int32                     getPendingReliableCount() const;
        /** @brief 보낼 줄에서 기다리는 비신뢰 메시지 수입니다. 0 보다 크면 앞 패킷이 다 싣지 못했다(대역폭 포화). */
        int32  getQueuedUnreliableCount() const { return static_cast<int32>( _listOutgoingUnreliable.size() ); }
        uint16 getNextPacketSequence() const { return _nextPacketSequence; }
        /**
         * @brief 신뢰 순서 채널에 넣은 메시지 수(조각이 아니라 메시지)와 그 채널에서 꺼낸 메시지 수입니다.
         * @details 신뢰 순서는 보낸 순서 그대로 한 번씩 넘기므로 한쪽의 "넣은 N 번째" 가 상대의 "꺼낸 N 번째" 다. 트레이스의 흐름 id 가 이것으로
         *          양쪽을 잇는다(`NetFlowLog::makeFlowID`) — 프로토콜에 칸을 더하지 않는다.
         */
        uint64 getOrderedSentCount() const { return _orderedSentCount; }
        uint64 getOrderedReceivedCount() const { return _orderedReceivedCount; }

    private:
        struct SentPacket
        {
            static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::Assign;

            uint16  _arrReliableID[kMaxReliablePerPacket]{};
            float64 _time{ 0.0 };
            int32   _byteCount{ 0 };
            int32   _reliableCount{ 0 };
            uint8   _bAcked{ SW_FALSE };
            uint8   _bAckRequested{ SW_FALSE }; ///< 상대가 바로 확인한다. RTT와 손실률의 표본이 된다.
            uint8   _bLossDetected{ SW_FALSE }; ///< 빠른 재전송이 이미 잃은 것으로 판단했다.
        };

        struct ReceivedPacket
        {
            static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::Assign;

            float64 _time{ 0.0 };
            int32   _byteCount{ 0 };
        };

        /** @brief 보낸 신뢰 메시지 하나입니다. 큰 메시지면 그 조각 하나입니다. */
        struct OutgoingReliable
        {
            static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::ReuseBuffer;

            /** @brief 모든 필드를 기본값으로 되돌리되 버퍼 용량은 남깁니다(`SequenceBuffer` 가 슬롯을 다시 쓸 때 부른다). */
            void resetForReuse()
            {
                vector<uint8> keptBuffer = std::move( _buffer );
                keptBuffer.clear();
                *this   = OutgoingReliable{};
                _buffer = std::move( keptBuffer );
            }

            vector<uint8>  _buffer{};
            float64        _lastSentTime{ -1.0 };
            uint8          _bResendNow{ SW_FALSE };                     ///< 실린 패킷을 잃었다. 재전송 간격을 기다리지 않는다.
            uint8          _bMore{ SW_FALSE };                          ///< 다음 ID가 같은 메시지의 이어지는 조각이다(신뢰 순서만).
            NetChannelType _channel{ NetChannelType::ReliableOrdered }; ///< 신뢰 순서와 신뢰 순서 없음이 ID와 창을 하나로 공유한다.
        };

        /** @brief 받은 신뢰 메시지 하나(조각 하나)입니다. 순서가 오면 `deliverReliable` 이 넘기거나 모읍니다. */
        struct IncomingReliable
        {
            static constexpr SequenceSlotReset kSlotReset = SequenceSlotReset::ReuseBuffer;

            /** @brief 모든 필드를 기본값으로 되돌리되 버퍼 용량은 남깁니다(`SequenceBuffer` 가 슬롯을 다시 쓸 때 부른다). */
            void resetForReuse()
            {
                vector<uint8> keptBuffer = std::move( _buffer );
                keptBuffer.clear();
                *this   = IncomingReliable{};
                _buffer = std::move( keptBuffer );
            }

            vector<uint8> _buffer{};
            uint8         _bMore{ SW_FALSE };
            uint8         _bDelivered{ SW_FALSE }; ///< 신뢰 순서 없음에서 받을 때 이미 넘겼다는 표시(순서 커서는 지나가기만 한다).
        };

        /** @brief 순서만 채널의 흐름 하나(메시지 첫 바이트가 종류)에서 마지막으로 넘긴 메시지 번호입니다. */
        struct SequencedStream
        {
            uint16 _lastID{ 0 };
            uint8  _kind{ 0 };
        };

        struct ParsedMessage
        {
            vector<uint8>  _buffer{};
            uint16         _id{ 0 };
            NetChannelType _channel{ NetChannelType::Unreliable };
            uint8          _bMore{ SW_FALSE }; ///< 신뢰 순서에서 다음 ID가 같은 메시지다.
        };

        /** @brief 보낼 줄의 비신뢰 메시지 하나입니다. */
        struct QueuedUnreliable
        {
            vector<uint8> _buffer{};
            float64       _queuedTime{ -1.0 }; ///< 처음 `writePacket` 을 만난 시각. 음수면 아직 만나지 않았다(나이를 세지 않는다)
        };

        /** @brief `writePacket` 이 쓰는 중인 패킷 본문입니다. 채널마다 싣는 함수가 같은 예산과 메시지 수를 나눠 씁니다. */
        struct PacketBody
        {
            BitWriter*  _pWriter{ nullptr };
            SentPacket* _pSent{ nullptr };
            float64     _time{ 0.0 };
            float64     _resendDelay{ 0.0 };
            int32       _budgetBits{ 0 };
            int32       _usedBits{ 0 };
            int32       _written{ 0 };

            /** @brief 메시지 하나를 "더 있다" 1비트와 함께 씁니다. 예산이나 메시지 수 상한을 넘으면 쓰지 않고 false 를 반환합니다. */
            [[nodiscard]] bool tryWriteMessage( NetChannelType channel, uint16 messageID, const vector<uint8>& buffer, bool bMore );
        };

        /** @brief 비신뢰 줄의 나이를 셉니다. 처음 만난 메시지는 지금으로 적고, @p maxAgeSeconds 를 넘게 기다린 것은 버립니다. */
        void expireQueuedUnreliable( float64 time, float64 maxAgeSeconds );
        /** @brief 처음 보내거나 재전송 시각이 된 신뢰 메시지(조각)를 오래된 것부터 싣습니다. */
        void writeReliableMessages( PacketBody& body );
        /** @brief 순서만 메시지를 종류마다 가장 새 것 하나씩 싣습니다. 못 실은 것은 다음 패킷으로 남깁니다. */
        void writeSequencedMessages( PacketBody& body );
        /** @brief 비신뢰 메시지를 들어가는 만큼 싣습니다. 못 실은 것은 다음 패킷으로 남깁니다. */
        void writeUnreliableMessages( PacketBody& body );

        /** @brief 패킷의 메시지를 모두 읽어 `_listParsedScratch` 에 둡니다. 깨졌으면 false 이고, 그때는 아무것도 적용하지 않습니다. */
        [[nodiscard]] bool parseMessages( BitReader& reader );
        /** @brief 읽은 메시지 하나를 채널 규칙대로 받은 큐나 신뢰 슬롯에 넣습니다(중복 · 창 밖 · 늦은 순서만은 버린다). */
        void dispatchParsedMessage( ParsedMessage& message );

        /** @brief 다시 쓸 메시지 버퍼 하나를 꺼냅니다. 없으면 빈 버퍼입니다(처음 쓸 때 할당한다). */
        vector<uint8> acquireBuffer();
        /** @brief 다 쓴 메시지 버퍼를 비워 다시 쓸 줄에 넣습니다. 용량이 없거나 조각 하나보다 크거나(모은 큰 메시지) 줄이 차면 버립니다. @p buffer 는 빈다. */
        void recycleBuffer( vector<uint8>& buffer );

        void    processAcks( float64 time, uint16 ack, uint32 ackBits );
        void    detectLostPackets( uint16 ack );
        bool    isReliableDue( const OutgoingReliable& message, float64 time, float64 resendDelay ) const;
        void    updateStats( float64 time );
        uint32  computeAckBits( uint16 ack ) const;
        float64 computeResendDelay() const;
        /** @brief 신뢰 메시지를 보낼 큐에 넣습니다(신뢰 순서는 조각으로 나눠 넣습니다). 창이 조각 모두를 받을 수 없으면 아무것도 넣지 않고 false 를 반환합니다. */
        bool queueReliable( NetChannelType channel, const uint8* pData, int32 size );
        /** @brief 순서가 온 신뢰 조각 하나를 넘기거나(조각이 하나뿐인 경우) 모읍니다(마지막 조각에서 한 메시지로 넘깁니다). */
        void deliverReliable( IncomingReliable& incoming );

        SequenceBuffer<SentPacket, kPacketWindow>                                _sentPackets;
        SequenceBuffer<ReceivedPacket, kPacketWindow>                            _receivedPackets;
        SequenceBuffer<OutgoingReliable, static_cast<uint32>( kReliableWindow )> _outgoingReliable;
        SequenceBuffer<IncomingReliable, static_cast<uint32>( kReliableWindow )> _incomingReliable;
        deque<vector<uint8>>                                                     _arrIncoming[static_cast<int32>( NetChannelType::Count )];
        vector<vector<uint8>>                                                    _listOutgoingSequenced; ///< 종류(첫 바이트)마다 가장 새 메시지 하나. 쌓인 순서대로 보관한다.
        deque<QueuedUnreliable>                                                  _listOutgoingUnreliable;
        vector<ParsedMessage>                                                    _listParsedScratch;    ///< 받은 패킷을 먼저 다 읽어 두는 스크래치 버퍼(용량을 다시 쓴다).
        vector<SequencedStream>                                                  _listSequencedReceive; ///< 순서만 채널에서 종류마다 마지막으로 넘긴 번호.
        vector<uint8>                                                            _listReliableAssembly; ///< 모으는 중인 신뢰 순서 메시지(앞 조각들). 마지막 조각이 오면 넘긴다.
        vector<vector<uint8>>                                                    _listSpareBuffer;      ///< 다 쓴 메시지 버퍼(용량만). 받기 · 비신뢰 보내기가 메시지마다 할당하지 않게 한다.
        NetConnectionStats                                                       _stats;
        float64                                                                  _lastStatsTime;
        float64                                                                  _lastAckRequestTime;   ///< 확인을 요청한 패킷을 마지막으로 보낸 시각.
        uint64                                                                   _orderedSentCount;     ///< 신뢰 순서 채널에 넣은 메시지 수
        uint64                                                                   _orderedReceivedCount; ///< 신뢰 순서 채널에서 꺼낸 메시지 수
        uint16                                                                   _nextPacketSequence;
        uint16                                                                   _nextReliableSendID;
        uint16                                                                   _oldestUnackedReliableID;
        uint16                                                                   _nextReliableReceiveID;
        uint16                                                                   _nextSequencedSendID;
        uint8                                                                    _bAckPending;         ///< 받은 패킷이 있어 확인을 돌려줘야 한다.
        uint8                                                                    _bDiscardingAssembly; ///< 모으던 메시지가 상한을 넘었다(상대의 규약 위반). 마지막 조각까지 버린다.
        uint8                                                                    _bUnreliableFirst;    ///< 다음 패킷은 순서만과 비신뢰 메시지부터 싣는다(앞 패킷의 조각이 그쪽을 남겼다).
    };
} // namespace sw
