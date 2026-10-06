/**
 * @file NetMessage.h
 * @brief 메시지 첫 바이트(종류)로 나뉘는 키트 · 게임 메시지를 쓰고 나눠 주는 도구 — `NetMessageWriter`(버퍼를 다시 쓰는 쓰기) · `INetMessageHandler` ·
 *        `NetMessageRouter`(호스트의 받은 메시지를 영역에 맞는 처리기로)입니다.
 * @details 네트워크 키트(`GF_Net*`)와 게임은 모두 "종류 바이트 + 비트 몸" 메시지를 주고받습니다. 메시지마다 `BitWriter` 를 새로 만들지 말고
 *          `NetMessageWriter` 를 다시 쓰며, 받은 메시지는 `NetMessageRouter` 가 종류마다 맡은 처리기 하나에게(영역 + 종류 마스크) 나눠 줍니다.
 *          연결 사건(열림 · 닫힘)도 라우터가 메시지보다 먼저 처리기에 알려, 키트는 수신 가드 · 연결 수명 손 배선 없이 자기 종류만 읽는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
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
        /** @brief 상대 모두에게 보냅니다 — 서버는 연결된 모두에게, 클라이언트는 서버(연결 0)에게. 보낸 수입니다(락스텝 · 롤백처럼 모두가 같은 메시지를 나눌 때). */
        int32 sendToPeers( NetHost& host, NetChannelType channel ) const;

        const vector<uint8>& getBytes() const { return _writer.getBytes(); }
        int32                getByteCount() const { return _writer.getByteCount(); }

    private:
        BitWriter _writer;
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기가 메시지 하나를 어떻게 했는가입니다. */
    enum class NetHandleResult : uint8
    {
        Handled = 0, ///< 받아 처리했다(낡아서 버린 것도 — 형식은 맞았다)
        Malformed,   ///< 내 종류인데 몸이 깨졌다 — 라우터가 세고 버린다(다른 처리기 · 남은 목록으로 가지 않는다)
        NotMine      ///< 맡은 종류가 아니다(손 배달 `handleMessage` 만 돌려준다 — 라우터는 마스크로 미리 거른다)
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 메시지 하나의 자리입니다 — 보낸 연결(클라이언트에서는 서버 = 0), 채널, 종류(첫 바이트), 메시지 통째(종류 포함 — 그대로 중계할 때)입니다. */
    struct NetMessageContext
    {
        const uint8*   _pMessage{ nullptr }; ///< 처리기 호출 동안만 유효하다
        int32          _messageSize{ 0 };
        int32          _connectionId{ -1 };
        NetChannelType _channel{ NetChannelType::ReliableOrdered };
        uint8          _kind{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetMessageHandler
     * @brief 메시지 영역 하나(`NetMessageRange`) 안에서 종류 마스크로 고른 종류를 맡는 쪽입니다 — 네트워크 키트의 서버 · 클라이언트, 게임의 메시지 처리기.
     * @details - 라우터는 종류마다 맡은 처리기 하나에게만 준다. 그래서 처리기는 종류 · 영역을 다시 검사하지 않는다(종류 바이트는 이미 읽었다).
     *          - 연결 사건(열림 · 닫힘)은 라우터가 메시지보다 먼저 모든 처리기에 알린다. 연결 id 는 자리 번호라 다시 쓰인다 — 연결마다의 상태는
     *            `onConnectionOpened` 에서 새로 시작한다(같은 자리에 새 연결이 오면 닫힘 → 열림이 그 연결의 메시지보다 앞선다).
     */
    class SW_API INetMessageHandler
    {
    public:
        INetMessageHandler()          = default;
        virtual ~INetMessageHandler() = default;

        INetMessageHandler( const INetMessageHandler& )            = default;
        INetMessageHandler& operator=( const INetMessageHandler& ) = default;

        /** @brief 맡은 영역의 첫 값입니다(키트는 `NetKitMessageRange::kClientServer` …). 게임 영역(0x80..)은 16 개씩 나눠 여럿이 맡을 수 있다. */
        virtual uint8 getMessageRangeBase() const = 0;
        /** @brief 영역 안에서 맡는 종류입니다 — 비트 n 이 (영역 + n). 한 영역을 여럿이 나눠 맡을 때 겹치지 않게 고른다. 0 이면 사건만 받는다. */
        virtual uint16 getMessageKindMask() const { return 0xFFFFu; }
        /**
         * @brief 맡은 종류의 메시지 하나입니다. @p body 는 종류 바이트 다음부터입니다.
         * @return 몸을 읽지 못했으면 Malformed — 라우터가 세고 버린다. 낡은 것을 버린 것은 Handled 다.
         */
        virtual NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) = 0;
        /** @brief 연결이 열렸습니다(서버 — 클라이언트, 클라이언트 — 서버 = 0). 이 연결 id 의 상태를 새로 시작한다. */
        virtual void onConnectionOpened( int32 connectionId ) { (void)connectionId; }
        /** @brief 연결이 닫혔습니다. 이 연결 id 의 상태를 놓는다(같은 id 는 다음 연결이 다시 쓴다). */
        virtual void onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
        {
            (void)connectionId;
            (void)reason;
        }

        /** @brief @p kind 가 이 처리기의 영역 · 마스크 안이면 true 입니다. */
        bool isMessageKindHandled( uint8 kind ) const;
        /**
         * @brief 손 배달 — 메시지 하나(첫 바이트 = 종류)를 라우터 없이 넘깁니다(라우터를 쓰지 않는 게임 · 시험). 맡은 종류가 아니면 NotMine 입니다.
         * @details 손 배달하는 쪽은 연결 사건도 `onConnectionOpened` · `onConnectionClosed` 로 직접 넘긴다.
         */
        NetHandleResult handleMessage( int32 connectionId, const uint8* pData, int32 size, NetChannelType channel = NetChannelType::ReliableOrdered );
        NetHandleResult handleMessage( int32 connectionId, const vector<uint8>& buffer )
        {
            return handleMessage( connectionId, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기가 없는 메시지입니다. */
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
     * @brief 받은 메시지를 종류(첫 바이트)마다 맡은 처리기 하나에게 바로 줍니다(종류 256 칸 표). 연결 사건은 메시지보다 먼저 모든 처리기에 알립니다.
     * @details - 처리기는 빌려 씁니다(라우터보다 오래 살거나 `removeHandler` 로 뺀다). 한 종류를 둘이 맡으려 하면 나중 것을 받지 않는다(`addHandler` 가 false).
     *          - `pump` 는 호스트에서 사건과 메시지를 잠금 한 번에 꺼내(`NetHost::drainInbound`) 잠금 밖에서 나눠 준다 — 처리기는 그 안에서 보내도 된다.
     *          - 깨진 메시지(처리기가 Malformed)는 세고(`getMalformedCount`) 버린다.
     * @code
     *     router.addHandler( &replicationServer );
     *     router.addHandler( &gameMessages );          // 0x80..0x8F
     *     router.pump( host, &listUnhandled );          // 매 틱 host.update 뒤
     * @endcode
     */
    class SW_API NetMessageRouter
    {
    public:
        static constexpr int32 kKindCount = 256;

        NetMessageRouter();

        NetMessageRouter( const NetMessageRouter& )            = delete;
        NetMessageRouter& operator=( const NetMessageRouter& ) = delete;

        /**
         * @brief 처리기를 답니다. 맡으려는 종류 하나라도 이미 다른 처리기가 맡았으면 **통째로 받지 않고** 알린 뒤 false 입니다(반쯤 단 처리기는 없다).
         * @details 섞인 게임은 false 를 기동 실패로 다룬다 — 키트는 제 영역(`NetKitMessageRange`), 게임 처리기 여럿은 영역 안에서 서로 다른 마스크.
         *          이미 단 처리기를 다시 달면 true 입니다.
         */
        bool addHandler( INetMessageHandler* pHandler );
        void removeHandler( INetMessageHandler* pHandler );
        /** @brief 메시지 하나를 그 종류의 처리기에 넘깁니다. 처리기가 없으면 NotMine 입니다(빈 메시지도). */
        NetHandleResult dispatch( const NetMessageContext& context, const uint8* pData, int32 size );
        /** @brief 연결 사건 하나를 모든 처리기에 알립니다(등록 순서). */
        void dispatchEvent( const NetHostEvent& event );
        /**
         * @brief @p host 에 쌓인 연결 사건과 받은 메시지를 모두 꺼내 사건 먼저 나눠 줍니다. 꺼낸 메시지 수입니다.
         * @param pOutUnhandled 처리기가 없는 메시지를 뒤에 붙입니다(nullptr 이면 버린다).
         * @param pOutListEvent 꺼낸 사건을 뒤에 붙입니다(처리기 말고도 게임이 알아야 할 때).
         */
        int32 pump( NetHost& host, vector<NetReceivedMessage>* pOutUnhandled = nullptr, vector<NetHostEvent>* pOutListEvent = nullptr );
        /**
         * @brief 서버가 받은 메시지를 보낸 연결 말고 모두에게 **받은 채널 그대로** 다시 보냅니다. 클라이언트에서는 하지 않습니다(0). 보낸 수입니다.
         * @details 처리기 안에서 부른다(@p context 의 메시지 바이트는 처리기 호출 동안만 유효하다).
         */
        static int32 relayToOtherPeers( NetHost& host, const NetMessageContext& context );
        uint64       getMalformedCount() const { return _malformedCount; }

    private:
        void rebuildKindTable();

        vector<INetMessageHandler*> _listHandler; ///< 등록 순서
        INetMessageHandler*         _arrKindHandler[kKindCount];
        NetInbound                  _inbound; ///< `pump` 가 다시 쓴다
        uint64                      _malformedCount;
    };
} // namespace sw
