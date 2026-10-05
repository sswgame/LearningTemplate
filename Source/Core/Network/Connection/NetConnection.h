/**
 * @file NetConnection.h
 * @brief 한 상대와의 신뢰성 계층 — 패킷 시퀀스 · 확인(ack + 32 비트 묶음), 채널별 메시지(신뢰 순서 · 신뢰 순서 없음 · 순서만 · 비신뢰), 재전송, RTT · 손실률 · 대역폭 통계입니다.
 * @details 주소 · 핸드셰이크 · 타임아웃은 `NetHost` 가 맡고 여기는 "연결된 뒤 한 패킷의 몸" 만 씁니다(Gaffer "Reliability and Congestion Avoidance over UDP").
 *          패킷마다 그 패킷에 실은 신뢰 메시지 id 를 기억해 두었다가, 패킷이 확인되면 메시지도 확인된 것으로 봅니다. 확인되지 않은 신뢰 메시지는 RTT + 50 ms
 *          (최소 0.1 초)가 지나면 다음 패킷에 다시 싣습니다. 그보다 먼저, 뒤에 보낸 패킷이 `kFastResendGap` 개 넘게 확인됐는데 확인이 없는 패킷은 잃은 것으로
 *          보고 그 신뢰 메시지를 바로 다시 싣습니다(빠른 재전송 — TCP 의 중복 확인 3 개 · NACK 의 자리, 순서 바뀜은 그 간격 안에서 견딘다).
 *          신뢰 순서 메시지는 `kMaxReliableMessageSize`(64 KB)까지 — `kMaxSingleMessageSize`(1 KB) 조각으로 나눠 조각마다 신뢰 id 하나를 쓰고(재전송 · 확인 · 창이
 *          조각 단위), 조각 머리 1 비트가 "다음 id 가 같은 메시지" 다. 받는 쪽은 순서대로 모아 마지막 조각에서 한 메시지로 건넨다(언리얼 partial bunch).
 *          다른 채널은 조각나지 않아 `kMaxSingleMessageSize` 까지다. 상한을 넘는 보내기는 오류 로그와 함께 false 다(창이 찬 것은 오류가 아니다 — 로그 없이 false).
 *          조각은 패킷을 거의 채우므로, 앞 패킷이 순서만 · 비신뢰를 남겼으면 다음 패킷은 그쪽부터 싣는다(번갈아 — 큰 전송이 스냅샷을 굶기지 않는다).
 *          신뢰 순서 없음은 신뢰 순서와 id · 창 · 재전송을 같이 쓴다 — 받는 쪽은 받는 대로 건네고 그 자리에 "건넸다" 표만 남겨, 순서 커서는 표를 지나가고 늦게 온 중복은 표가 거른다.
 *          패킷 끝의 1 비트가 "확인을 바로 돌려 달라" 입니다. 메시지를 실었거나 확인할 것이 없던(유지) 패킷만 켜고, 확인만 담은 답은 끈다 — 그래야 한가할 때
 *          두 쪽이 확인에 확인으로 끝없이 주고받지 않고 `NetHostSettings::_keepAliveInterval` 마다만 오간다. RTT · 손실률도 이 비트를 켠 패킷으로만 잰다.
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
        float32 _rtt{ 0.0f };           ///< 초(부드럽게 한 값)
        float32 _packetLoss{ 0.0f };    ///< 0..1
        float32 _sentBandwidth{ 0.0f }; ///< 초당 킬로비트
        float32 _receivedBandwidth{ 0.0f };
        uint64  _sentPacketCount{ 0 };
        uint64  _receivedPacketCount{ 0 };
        uint64  _ackedPacketCount{ 0 };
        uint64  _resentMessageCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetConnection
     * @brief 보낼 메시지를 채널에 쌓고(`sendMessage`), 매 송신마다 `writePacket` 이 시퀀스 · 확인 · 메시지를 한 패킷에 담습니다.
     *        받은 패킷은 `readPacket` 이 풀어 채널마다의 받은 줄에 넣고, 게임은 `receiveMessage` 로 꺼냅니다.
     */
    class SW_API NetConnection
    {
    public:
        static constexpr int32   kMaxSingleMessageSize      = 1024;      ///< 조각나지 않는 메시지(순서만 · 비신뢰)와 신뢰 순서 조각 하나의 상한 — 패킷 하나에 통째로 든다
        static constexpr int32   kMaxReliableMessageSize    = 64 * 1024; ///< 신뢰 순서 메시지 하나의 상한 — 조각 `kMaxSingleMessageSize` 로 나눠 보내고 받는 쪽이 모아 건넨다
        static constexpr int32   kMessageSizeBits           = 11;        ///< 0..1024 — 길이 칸(메시지 · 조각 하나)
        static constexpr float64 kDefaultAckRequestInterval = 0.25;
        static constexpr int32   kMaxReliablePerPacket      = 64;  ///< 패킷 하나에 싣는 신뢰 메시지 상한(보낸 패킷 기록이 힙을 쓰지 않게)
        static constexpr int32   kReliableWindow            = 256; ///< 확인 안 된 신뢰 메시지 상한(넘으면 `sendMessage` 가 거절 — 막힘)
        static constexpr int32   kPacketHeaderBits          = 16 + 16 + 32;
        static constexpr int32   kFastResendGap             = 3;    ///< 이만큼 뒤의 패킷이 확인됐는데 확인이 없으면 잃은 것으로 본다
        static constexpr float64 kResendMargin              = 0.05; ///< 재전송 간격 = RTT + 이 여유(흔들림 · 상대의 보내기 간격)

        static_assert( ( 1 << kMessageSizeBits ) > kMaxSingleMessageSize, "the length field must hold one whole fragment" );
        static_assert( kMaxReliableMessageSize % kMaxSingleMessageSize == 0 && kMaxReliableMessageSize / kMaxSingleMessageSize < kReliableWindow,
                       "a whole reliable message must fit in the reliable window" );

        NetConnection();

        void reset();
        /**
         * @brief 메시지를 채널에 넣습니다. 채널 상한(`getMaxMessageSize`)을 넘으면 오류 로그와 함께, 신뢰 창이 메시지 전체(조각 모두)를 받을 수 없으면
         *        로그 없이 false 입니다 — 둘 다 아무것도 넣지 않는다(반쪽 메시지가 없다).
         */
        [[nodiscard]] bool sendMessage( NetChannelType channel, const uint8* pData, int32 size );
        /** @brief 채널 하나의 메시지 상한입니다 — 신뢰 순서는 `kMaxReliableMessageSize`, 나머지는 `kMaxSingleMessageSize`. */
        static int32 getMaxMessageSize( NetChannelType channel );
        /** @brief 받은 메시지 하나를 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool receiveMessage( NetChannelType channel, vector<uint8>& outBuffer );

        /**
         * @brief 패킷 몸을 씁니다 — 헤더(시퀀스 · ack · ack 비트) + 들어가는 만큼의 메시지 + 확인 요청 비트.
         * @param ackRequestInterval 확인만 담은 답이라도 마지막 확인 요청에서 이만큼 지났으면 확인을 바란다 — 양쪽 유지 시각이 맞물려
         *                           한쪽이 늘 답만 보내게 되어도 그쪽 RTT 를 잴 수 있게(`NetHostSettings::_keepAliveInterval`).
         */
        void writePacket( float64 time, BitWriter& writer, int32 maxBytes, float64 ackRequestInterval = kDefaultAckRequestInterval );
        /** @brief 패킷 몸을 읽습니다. 깨졌거나 오래된 중복이면 false 입니다. */
        [[nodiscard]] bool readPacket( float64 time, BitReader& reader );
        /** @brief 보낼 것이 있는가 — 새 메시지 · 재전송 시각이 된 신뢰 메시지 · 확인해 줄 패킷. */
        bool hasDataToSend( float64 time ) const;
        /** @brief 받은 패킷을 확인해 줘야 하는가 — 대역폭 몫을 다 쓴 호스트가 메시지 없이 확인만 보낼지 정한다. */
        bool isAckPending() const { return _bAckPending != SW_FALSE; }

        const NetConnectionStats& getStats() const { return _stats; }
        int32                     getPendingReliableCount() const;
        uint16                    getNextPacketSequence() const { return _nextPacketSequence; }

    private:
        struct SentPacket
        {
            uint16  _arrReliableId[kMaxReliablePerPacket]{};
            float64 _time{ 0.0 };
            int32   _byteCount{ 0 };
            int32   _reliableCount{ 0 };
            uint8   _bAcked{ SW_FALSE };
            uint8   _bAckRequested{ SW_FALSE }; ///< 상대가 바로 확인한다 — RTT · 손실률 표본
            uint8   _bLossDetected{ SW_FALSE }; ///< 빠른 재전송이 이미 잃은 것으로 보았다
        };

        struct ReceivedPacket
        {
            float64 _time{ 0.0 };
            int32   _byteCount{ 0 };
        };

        /** @brief 보낸 신뢰 메시지 하나 — 큰 메시지면 그 조각 하나입니다. */
        struct OutgoingReliable
        {
            vector<uint8>  _buffer{};
            float64        _lastSentTime{ -1.0 };
            uint8          _bResendNow{ SW_FALSE };                     ///< 실린 패킷을 잃었다 — 재전송 간격을 기다리지 않는다
            uint8          _bMore{ SW_FALSE };                          ///< 다음 id 가 같은 메시지의 이어지는 조각이다(신뢰 순서만)
            NetChannelType _channel{ NetChannelType::ReliableOrdered }; ///< 신뢰 순서 · 신뢰 순서 없음 — id · 창은 하나다
        };

        /** @brief 받은 신뢰 메시지 하나(조각 하나) — 순서가 오면 `deliverReliable` 이 건네거나 모은다. */
        struct IncomingReliable
        {
            vector<uint8> _buffer{};
            uint8         _bMore{ SW_FALSE };
            uint8         _bDelivered{ SW_FALSE }; ///< 신뢰 순서 없음 — 받을 때 이미 건넸다(순서 커서가 지나가기만 한다)
        };

        /** @brief 순서만 채널의 흐름 하나(메시지 첫 바이트 = 종류)에서 마지막으로 건넨 메시지 번호입니다. */
        struct SequencedStream
        {
            uint16 _lastId{ 0 };
            uint8  _kind{ 0 };
        };

        struct ParsedMessage
        {
            vector<uint8>  _buffer{};
            uint16         _id{ 0 };
            NetChannelType _channel{ NetChannelType::Unreliable };
            uint8          _bMore{ SW_FALSE }; ///< 신뢰 순서 — 다음 id 가 같은 메시지
        };

        void    processAcks( float64 time, uint16 ack, uint32 ackBits );
        void    detectLostPackets( uint16 ack );
        bool    isReliableDue( const OutgoingReliable& message, float64 time, float64 resendDelay ) const;
        void    updateStats( float64 time );
        uint32  computeAckBits( uint16 ack ) const;
        float64 computeResendDelay() const;
        /** @brief 신뢰 메시지를 보낼 줄에 넣습니다(신뢰 순서는 조각으로 나눠). 창이 조각 모두를 받을 수 없으면 아무것도 넣지 않고 false 입니다. */
        bool queueReliable( NetChannelType channel, const uint8* pData, int32 size );
        /** @brief 순서가 온 신뢰 조각 하나를 건네거나(조각 하나짜리) 모읍니다(마지막 조각에서 한 메시지로). */
        void deliverReliable( IncomingReliable& incoming );

        SequenceBuffer<SentPacket>       _sentPackets;
        SequenceBuffer<ReceivedPacket>   _receivedPackets;
        SequenceBuffer<OutgoingReliable> _outgoingReliable;
        SequenceBuffer<IncomingReliable> _incomingReliable;
        deque<vector<uint8>>             _arrIncoming[static_cast<int32>( NetChannelType::Count )];
        vector<vector<uint8>>            _listOutgoingSequenced; ///< 종류(첫 바이트)마다 가장 새 것 하나 — 쌓인 순서
        deque<vector<uint8>>             _listOutgoingUnreliable;
        vector<ParsedMessage>            _listParsedScratch;    ///< 받은 패킷을 먼저 다 읽어 두는 자리(용량을 다시 쓴다)
        vector<SequencedStream>          _listSequencedReceive; ///< 순서만 — 종류마다 마지막으로 건넨 번호
        vector<uint8>                    _listReliableAssembly; ///< 모으는 중인 신뢰 순서 메시지(앞 조각들) — 마지막 조각이 오면 건넨다
        NetConnectionStats               _stats;
        float64                          _lastStatsTime;
        float64                          _lastAckRequestTime; ///< 확인을 바란 패킷을 마지막으로 보낸 때
        uint16                           _nextPacketSequence;
        uint16                           _nextReliableSendId;
        uint16                           _oldestUnackedReliableId;
        uint16                           _nextReliableReceiveId;
        uint16                           _nextSequencedSendId;
        uint8                            _bAckPending;         ///< 받은 패킷이 있어 확인을 돌려줘야 한다
        uint8                            _bDiscardingAssembly; ///< 모으던 메시지가 상한을 넘었다(상대의 규약 위반) — 마지막 조각까지 버린다
        uint8                            _bUnreliableFirst;    ///< 다음 패킷은 순서만 · 비신뢰부터 싣는다(앞 패킷의 조각이 그쪽을 남겼다)
    };
} // namespace sw
