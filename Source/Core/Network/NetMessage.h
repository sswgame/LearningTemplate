/**
 * @file NetMessage.h
 * @brief 메시지 첫 바이트(종류)로 나뉘는 키트 · 게임 메시지를 쓰고 나눠 주는 도구 — `NetMessageWriter`(버퍼를 다시 쓰는 쓰기) · `INetMessageHandler` ·
 *        `NetMessageRouter`(호스트의 받은 메시지를 영역에 맞는 처리기로)입니다.
 * @details 네트워크 키트(`GF_Net*`)와 게임은 모두 "종류 바이트 + 비트 몸" 메시지를 주고받습니다. 예전에는 키트마다 메시지마다 `BitWriter` 를 새로 만들고,
 *          게임은 `if ( a.handleMessage( … ) ) continue; if ( b.handleMessage( … ) ) …` 사슬로 나눠 줬습니다. 영역은 `NetMessageRange`(16 개씩)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    class NetHost;

    /**
     * @class NetMessageWriter
     * @brief 종류 바이트를 쓰고 몸은 돌려준 `BitWriter` 에 이어 쓴 뒤 보냅니다. 버퍼를 다시 써서 메시지마다 할당하지 않습니다.
     * @code
     *     BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kInput );
     *     writer.writeVarUint( tick );
     *     (void)_messageWriter.send( *_pHost, connectionId, NetChannelType::ReliableOrdered );
     * @endcode
     */
    class SW_API NetMessageWriter
    {
    public:
        NetMessageWriter();

        /** @brief 비우고 종류 바이트를 씁니다. 몸은 돌려준 쓰기에 이어 씁니다. */
        BitWriter&         begin( uint8 kind );
        [[nodiscard]] bool send( NetHost& host, int32 connectionId, NetChannelType channel ) const;
        /** @brief 연결된 모두에게(@p exceptId 는 빼고) 보냅니다. 보낸 수입니다. */
        int32 broadcast( NetHost& host, NetChannelType channel, int32 exceptId = -1 ) const;

        const vector<uint8>& getBytes() const { return _writer.getBytes(); }
        int32                getByteCount() const { return _writer.getByteCount(); }

    private:
        BitWriter _writer;
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetMessageHandler
     * @brief 메시지 영역 하나(`NetMessageRange`)를 맡는 쪽입니다 — 네트워크 키트의 서버 · 클라이언트, 게임의 메시지 처리기.
     */
    class SW_API INetMessageHandler
    {
    public:
        INetMessageHandler()          = default;
        virtual ~INetMessageHandler() = default;

        INetMessageHandler( const INetMessageHandler& )            = default;
        INetMessageHandler& operator=( const INetMessageHandler& ) = default;

        /** @brief 맡은 영역의 첫 값입니다(`NetMessageRange::kClientServer` …). 게임 영역(0x80..)은 16 개씩 나눠 여럿이 맡을 수 있다. */
        virtual uint8 getMessageRangeBase() const = 0;
        /** @brief @p pData[0] 이 종류 바이트입니다. 내 메시지가 아니거나 깨졌으면 false 입니다. 클라이언트 쪽은 @p connectionId 를 쓰지 않는다. */
        virtual bool handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기가 없거나 거절한 메시지입니다. */
    struct NetReceivedMessage
    {
        vector<uint8>  _buffer{};
        int32          _connectionId{ -1 };
        NetChannelType _channel{ NetChannelType::ReliableOrdered };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetMessageRouter
     * @brief 첫 바이트의 위 4 비트(영역)로 처리기를 바로 찾습니다. 한 영역에 처리기가 여럿이면 등록 순서대로 물어 처음 받아들인 쪽에서 멈춥니다.
     * @details 처리기는 빌려 씁니다(라우터보다 오래 살거나 `removeHandler` 로 뺀다).
     * @code
     *     router.addHandler( &replicationServer );
     *     router.addHandler( &gameMessages );          // 0x80..0x8F
     *     router.pump( host, &listUnhandled );          // 매 틱 host.update 뒤
     * @endcode
     */
    class SW_API NetMessageRouter
    {
    public:
        static constexpr int32 kRangeCount = 16;

        NetMessageRouter();

        void addHandler( INetMessageHandler* pHandler );
        void removeHandler( INetMessageHandler* pHandler );
        /** @brief 메시지 하나를 맞는 처리기에 넘깁니다. 받아들인 처리기가 있으면 true 입니다. */
        bool dispatch( int32 connectionId, const uint8* pData, int32 size );
        /**
         * @brief @p host 에 쌓인 받은 메시지를 모두 꺼내 나눠 줍니다. 꺼낸 수입니다.
         * @param pOutUnhandled 처리기가 없거나 거절한 메시지를 뒤에 붙입니다(nullptr 이면 버린다).
         */
        int32 pump( NetHost& host, vector<NetReceivedMessage>* pOutUnhandled = nullptr );

    private:
        vector<INetMessageHandler*> _arrHandler[kRangeCount];
        vector<uint8>               _receiveBuffer;
    };
} // namespace sw
