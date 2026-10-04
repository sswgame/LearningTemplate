/**
 * @file NetConnection.h
 * @brief 한 상대와의 신뢰성 계층 — 패킷 시퀀스 · 확인(ack + 32 비트 묶음), 채널별 메시지(신뢰 순서 · 순서만 · 비신뢰), 재전송, RTT · 손실률 · 대역폭 통계입니다.
 * @details 주소 · 핸드셰이크 · 타임아웃은 `NetHost` 가 맡고 여기는 "연결된 뒤 한 패킷의 몸" 만 씁니다(Gaffer "Reliability and Congestion Avoidance over UDP").
 *          패킷마다 그 패킷에 실은 신뢰 메시지 id 를 기억해 두었다가, 패킷이 확인되면 메시지도 확인된 것으로 봅니다. 확인되지 않은 신뢰 메시지는 RTT 의 1.5 배
 *          (최소 0.1 초)가 지나면 다음 패킷에 다시 싣습니다. 메시지 하나는 `kMaxMessageSize` 바이트까지입니다(조각내기는 하지 않는다 — 큰 덩어리는 게임이 나눈다).
 *          패킷 끝의 1 비트가 "확인을 바로 돌려 달라" 입니다. 메시지를 실었거나 확인할 것이 없던(유지) 패킷만 켜고, 확인만 담은 답은 끈다 — 그래야 한가할 때
 *          두 쪽이 확인에 확인으로 끝없이 주고받지 않고 `NetHostSettings::_keepAliveInterval` 마다만 오간다. RTT · 손실률도 이 비트를 켠 패킷으로만 잰다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/SequenceBuffer.h"

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
        static constexpr int32   kMaxMessageSize            = 1024;
        static constexpr int32   kMessageSizeBits           = 11; ///< 0..1024 — 길이 칸(예전 16 비트)
        static constexpr float64 kDefaultAckRequestInterval = 0.25;
        static constexpr int32   kMaxReliablePerPacket      = 64;  ///< 패킷 하나에 싣는 신뢰 메시지 상한(보낸 패킷 기록이 힙을 쓰지 않게)
        static constexpr int32   kReliableWindow            = 256; ///< 확인 안 된 신뢰 메시지 상한(넘으면 `sendMessage` 가 거절 — 막힘)
        static constexpr int32   kPacketHeaderBits          = 16 + 16 + 32;

        NetConnection();

        void reset();
        /** @brief 메시지를 채널에 넣습니다. 너무 크거나 신뢰 창이 찼으면 false 입니다. */
        [[nodiscard]] bool sendMessage( NetChannelType channel, const uint8* pData, int32 size );
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
        };

        struct ReceivedPacket
        {
            float64 _time{ 0.0 };
            int32   _byteCount{ 0 };
        };

        struct OutgoingReliable
        {
            vector<uint8> _buffer{};
            float64       _lastSentTime{ -1.0 };
        };

        struct IncomingReliable
        {
            vector<uint8> _buffer{};
        };

        struct ParsedMessage
        {
            vector<uint8>  _buffer{};
            uint16         _id{ 0 };
            NetChannelType _channel{ NetChannelType::Unreliable };
        };

        void    processAcks( float64 time, uint16 ack, uint32 ackBits );
        void    updateStats( float64 time );
        uint32  computeAckBits( uint16 ack ) const;
        float64 computeResendDelay() const;

        SequenceBuffer<SentPacket>       _sentPackets;
        SequenceBuffer<ReceivedPacket>   _receivedPackets;
        SequenceBuffer<OutgoingReliable> _outgoingReliable;
        SequenceBuffer<IncomingReliable> _incomingReliable;
        deque<vector<uint8>>             _arrIncoming[static_cast<int32>( NetChannelType::Count )];
        deque<vector<uint8>>             _listOutgoingSequenced;
        deque<vector<uint8>>             _listOutgoingUnreliable;
        vector<ParsedMessage>            _listParsedScratch; ///< 받은 패킷을 먼저 다 읽어 두는 자리(용량을 다시 쓴다)
        NetConnectionStats               _stats;
        float64                          _lastStatsTime;
        float64                          _lastAckRequestTime; ///< 확인을 바란 패킷을 마지막으로 보낸 때
        uint16                           _nextPacketSequence;
        uint16                           _nextReliableSendId;
        uint16                           _oldestUnackedReliableId;
        uint16                           _nextReliableReceiveId;
        uint16                           _nextSequencedSendId;
        uint16                           _lastSequencedReceiveId;
        uint8                            _bHasSequencedReceive;
        uint8                            _bAckPending; ///< 받은 패킷이 있어 확인을 돌려줘야 한다
    };
} // namespace sw
