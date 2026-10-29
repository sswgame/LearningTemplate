/**
 * @file NetMessage.h
 * @brief 메시지 첫 바이트(종류)로 나뉘는 키트와 게임 메시지를 쓰고 나눠 주는 도구를 제공합니다.
 * @details `NetMessageWriter` 는 버퍼를 다시 쓰는 메시지 작성기이고, `INetMessageHandler` 는 메시지를 맡는 처리기 인터페이스다.
 *          `NetMessageRouter` 는 엔드포인트가 받은 메시지를 영역에 맞는 처리기로 나눠 준다.
 *
 *          네트워크 키트(`GF_Net*`)와 게임은 모두 "종류 바이트 + 비트 본문" 형식의 메시지를 주고받는다.
 *          메시지마다 `BitWriter` 를 새로 만들지 말고 `NetMessageWriter` 를 다시 쓴다.
 *          받은 메시지는 `NetMessageRouter` 가 종류마다 맡은 처리기 하나에게(영역과 종류 마스크로) 나눠 준다.
 *          연결 사건(열림, 닫힘)도 라우터가 메시지보다 먼저 처리기에 알린다. 그래서 키트는 수신 가드나 연결 수명을 직접 배선하지 않고 자기 종류만 읽으면 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    /**
     * @class NetMessageWriter
     * @brief 종류 바이트를 쓰고 본문은 돌려준 `BitWriter` 에 이어 쓴 뒤 보냅니다. 버퍼를 다시 쓰므로 메시지마다 할당하지 않습니다.
     * @code
     *     BitWriter& writer = _messageWriter.begin( NetLockstepMessage::kInput );
     *     writer.writeVarUint( tick );
     *     (void)_messageWriter.send( *_pHost, connectionID, NetChannelType::ReliableOrdered );
     * @endcode
     */
    class SW_API NetMessageWriter
    {
    public:
        NetMessageWriter();

        /** @brief 비우고 종류 바이트를 씁니다. 본문은 돌려준 `BitWriter` 에 이어 씁니다. */
        BitWriter&         begin( uint8 kind );
        [[nodiscard]] bool send( INetPeers& peers, int32 connectionID, NetChannelType channel ) const;
        /** @brief 연결된 모두에게 보냅니다. @p exceptID 는 제외합니다. 반환값은 보낸 수입니다. */
        int32 broadcast( INetPeers& peers, NetChannelType channel, int32 exceptID = -1 ) const;
        /** @brief 상대 모두에게 보냅니다. 서버는 연결된 모두에게, 클라이언트는 서버(연결 0)에게 보냅니다. 반환값은 보낸 수이며, 락스텝이나 롤백처럼 모두가 같은 메시지를 나눌 때 씁니다. */
        int32 sendToPeers( INetPeers& peers, NetChannelType channel ) const;

        const vector<uint8>& getBytes() const { return _writer.getBytes(); }
        int32                getByteCount() const { return _writer.getByteCount(); }

    private:
        BitWriter _writer;
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기가 메시지 하나를 어떻게 처리했는지 나타냅니다. */
    enum class NetHandleResult : uint8
    {
        Handled = 0, ///< 받아서 처리했다(낡아서 버린 것도 포함한다. 형식은 맞았다).
        Malformed,   ///< 내 종류인데 본문이 깨졌다. 라우터가 세고 버리며, 다른 처리기나 남은 목록으로 넘기지 않는다.
        NotMine      ///< 맡은 종류가 아니다(`handleMessage` 로 직접 전달할 때만 반환한다. 라우터는 마스크로 미리 거른다).
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 메시지 하나의 컨텍스트입니다. 보낸 연결(클라이언트에서는 서버가 0), 채널, 종류(첫 바이트), 메시지 전체(종류 포함, 그대로 중계할 때 씁니다)를 담습니다. */
    struct NetMessageContext
    {
        const uint8*   _pMessage{ nullptr }; ///< 처리기 호출 동안만 유효하다.
        int32          _messageSize{ 0 };
        int32          _connectionID{ -1 };
        NetChannelType _channel{ NetChannelType::ReliableOrdered };
        uint8          _kind{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetMessageHandler
     * @brief 메시지 영역 하나(`NetMessageRange`) 안에서 종류 마스크로 고른 종류를 맡는 쪽입니다. 네트워크 키트의 서버와 클라이언트, 게임의 메시지 처리기가 해당합니다.
     * @details - 라우터는 종류마다 맡은 처리기 하나에게만 메시지를 준다. 그래서 처리기는 종류와 영역을 다시 검사하지 않는다(종류 바이트는 이미 읽었다).
     *          - 연결 사건(열림, 닫힘)은 라우터가 메시지보다 먼저 모든 처리기에 알린다. 연결 ID는 슬롯 번호라 다시 쓰이므로,
     *            연결별 상태는 `onConnectionOpened` 에서 새로 시작한다(같은 슬롯에 새 연결이 오면 닫힘, 열림 순서로 그 연결의 메시지보다 앞서 알린다).
     */
    class SW_API INetMessageHandler
    {
    public:
        INetMessageHandler()          = default;
        virtual ~INetMessageHandler() = default;

        INetMessageHandler( const INetMessageHandler& )            = default;
        INetMessageHandler& operator=( const INetMessageHandler& ) = default;

        /** @brief 맡은 영역의 첫 값입니다(키트는 `NetKitMessageRange::kClientServer` 등). 게임 영역(0x80..)은 16개씩 나눠 여럿이 맡을 수 있습니다. */
        virtual uint8 getMessageRangeBase() const = 0;
        /** @brief 영역 안에서 맡는 종류의 마스크입니다. 비트 n 이 (영역 + n)에 해당하며, 한 영역을 여럿이 나눠 맡을 때 겹치지 않게 고릅니다. 0이면 사건만 받습니다. */
        virtual uint16 getMessageKindMask() const { return 0xFFFFu; }
        /**
         * @brief 맡은 종류의 메시지 하나를 처리합니다. @p body 는 종류 바이트 다음부터입니다.
         * @return 본문을 읽지 못했으면 Malformed 를 반환하며, 라우터가 세고 버립니다. 낡은 것을 버린 경우는 Handled 입니다.
         */
        virtual NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) = 0;
        /** @brief 연결이 열렸습니다(서버에서는 클라이언트, 클라이언트에서는 서버 = 0). 이 연결 ID의 상태를 새로 시작합니다. */
        virtual void onConnectionOpened( int32 connectionID ) { (void)connectionID; }
        /** @brief 연결이 닫혔습니다. 이 연결 ID의 상태를 버립니다(같은 ID는 다음 연결이 다시 씁니다). */
        virtual void onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
        {
            (void)connectionID;
            (void)reason;
        }
        /**
         * @brief 영역 안에서 이 처리기가 전달 통지를 달아 보내는(`INetPeers::sendNotifiedMessage`) 종류의 마스크입니다. 비트 n 이 (영역 + n)입니다.
         * @details 라우터는 그 종류의 판정을 이 처리기의 `onDeliveryNotice` 에 넘긴다. 받는 종류 마스크와 따로 세며, 한 종류를 둘이 보내겠다고 하면 나중 것을 받지 않는다.
         */
        virtual uint16 getNotifiedKindMask() const { return 0; }
        /** @brief 이 처리기가 보낸 전달 통지 메시지 하나의 판정입니다(라우터 `pump` 가 사건 뒤, 메시지 앞에 부른다). */
        virtual void onDeliveryNotice( const NetDeliveryNotice& notice ) { (void)notice; }

        /** @brief @p kind 가 이 처리기의 영역과 마스크 안이면 true 를 반환합니다. */
        bool isMessageKindHandled( uint8 kind ) const;
        /**
         * @brief 메시지 하나(첫 바이트가 종류)를 라우터 없이 직접 전달합니다. 라우터를 쓰지 않는 게임이나 테스트에서 씁니다. 맡은 종류가 아니면 NotMine 을 반환합니다.
         * @details 직접 전달하는 쪽은 연결 사건도 `onConnectionOpened` 와 `onConnectionClosed` 로 직접 넘긴다.
         */
        NetHandleResult handleMessage( int32 connectionID, const uint8* pData, int32 size, NetChannelType channel = NetChannelType::ReliableOrdered );
        NetHandleResult handleMessage( int32 connectionID, const vector<uint8>& buffer )
        {
            return handleMessage( connectionID, buffer.data(), static_cast<int32>( buffer.size() ) );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 처리기가 없는 메시지입니다. */
    struct NetReceivedMessage
    {
        vector<uint8>  _buffer{};
        int32          _connectionID{ -1 };
        NetChannelType _channel{ NetChannelType::ReliableOrdered };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetMessageRouter
     * @brief 받은 메시지를 종류(첫 바이트)마다 맡은 처리기 하나에게 바로 전달합니다(종류 256개짜리 테이블). 연결 사건은 메시지보다 먼저 모든 처리기에 알립니다.
     * @details - 처리기는 빌려 쓴다. 등록은 `registerHandler` 가 돌려주는 구독(`Subscription`)이 소유한다 — 처리기와 같은 수명의 객체(보통 처리기를 가진 쪽)가 들고,
     *            구독이 사라지면 처리기가 빠진다. 라우터가 먼저 사라지면 구독은 무효가 된다. 한 종류를 둘이 맡으려 하면 나중 것을 받지 않는다(빈 구독).
     *          - `pump` 는 엔드포인트에서 사건과 메시지를 잠금 한 번에 꺼내(`INetPeers::drainInbound` — 엔드포인트 하나든 포트 샤드 묶음이든) 잠금 밖에서 나눠 준다. 처리기는 그 안에서 보내도 된다.
     *          - 깨진 메시지(처리기가 Malformed 를 반환)는 세고(`getMalformedCount`) 버린다.
     *          - 등록과 구독의 해제는 `pump` 를 부르는 스레드에서 한다(구독 목록에 잠금이 없다).
     * @code
     *     _replicationSubscription = router.registerHandler( &replicationServer );
     *     _gameSubscription        = router.registerHandler( &gameMessages );     // 0x80..0x8F
     *     router.pump( host, &listUnhandled );                                    // 매 틱 host.update 뒤
     * @endcode
     */
    class SW_API NetMessageRouter
    {
    public:
        static constexpr int32 kKindCount = NetMessageKindTallyTable::kKindCount;

        NetMessageRouter();

        NetMessageRouter( const NetMessageRouter& )            = delete;
        NetMessageRouter& operator=( const NetMessageRouter& ) = delete;

        /**
         * @brief 처리기를 등록하고 그 등록을 소유하는 구독을 반환합니다. 구독이 사라지면 처리기가 빠집니다.
         * @details 맡으려는 종류 중 하나라도 이미 맡은 처리기가 있거나(같은 처리기를 다시 달아도 자기 종류와 겹친다), @p pHandler 가 nullptr 이면 **통째로 받지 않고**
         *          오류를 남긴 뒤 빈 구독(`isActive` 가 false)을 반환합니다(일부만 등록된 처리기는 없습니다). 영역이 겹친 게임은 빈 구독을 기동 실패로 다룬다.
         *          키트는 자기 영역(`NetKitMessageRange`)을 쓰고, 게임 처리기 여럿은 영역 안에서 서로 다른 마스크를 쓴다.
         */
        [[nodiscard]] Subscription registerHandler( INetMessageHandler* pHandler );
        /** @brief 메시지 하나를 그 종류의 처리기에 넘깁니다. 처리기가 없으면(빈 메시지도) NotMine 을 반환합니다. */
        NetHandleResult dispatch( const NetMessageContext& context, const uint8* pData, int32 size );
        /** @brief 연결 사건 하나를 모든 처리기에 등록 순서대로 알립니다. */
        void dispatchEvent( const NetEndpointEvent& event );
        /**
         * @brief @p peers 에 쌓인 연결 사건과 받은 메시지를 모두 꺼내 사건부터 나눠 줍니다. 반환값은 꺼낸 메시지 수입니다.
         * @param pOutUnhandled 처리기가 없는 메시지를 뒤에 붙입니다(nullptr 이면 버립니다).
         * @param pOutListEvent 꺼낸 사건을 뒤에 붙입니다(처리기 말고도 게임이 알아야 할 때 씁니다).
         */
        int32 pump( INetPeers& peers, vector<NetReceivedMessage>* pOutUnhandled = nullptr, vector<NetEndpointEvent>* pOutListEvent = nullptr );
        /**
         * @brief 서버가 받은 메시지를 보낸 연결을 뺀 모두에게 **받은 채널 그대로** 다시 보냅니다. 클라이언트에서는 하지 않고 0을 반환합니다. 반환값은 보낸 수입니다.
         * @details 처리기 안에서 부른다(@p context 의 메시지 바이트는 처리기 호출 동안만 유효하다).
         */
        static int32 relayToOtherPeers( INetPeers& peers, const NetMessageContext& context );
        uint64       getMalformedCount() const { return _malformedCount; }
        /** @brief 그 종류를 보낸다고 등록한 처리기가 없어 버린 전달 통지 수입니다(첫 번째는 오류 로그도 남긴다 — 보낸 쪽이 처리기를 등록하지 않았다). */
        uint64 getUnclaimedNoticeCount() const { return _unclaimedNoticeCount; }
        /** @brief `pump` 가 지금까지 꺼낸 종류별 메시지 수와 바이트입니다(받은 쪽 셈 — 처리기가 없는 메시지도 센다. 보낸 쪽은 `INetPeers::addSentKindTally`). */
        const NetMessageKindTallyTable& getKindTally() const { return _kindTally; }

    private:
        /** @brief 처리기의 등록 구독이 풀렸습니다. 그 처리기(같은 처리기가 여럿 올랐으면 먼저 것 하나)를 빼고 종류 표를 다시 짓습니다. */
        void releaseHandler( INetMessageHandler* pHandler );
        void rebuildKindTable();
        /** @brief 전달 통지 하나를 그 종류를 보내는 처리기에 넘깁니다. 없으면 세고 버립니다. */
        void dispatchNotice( const NetDeliveryNotice& notice );

        vector<INetMessageHandler*> _listHandler; ///< 등록 순서대로 보관한다. 사건만 받는 처리기는 등록마다 하나씩 오르므로(중복 허용) `RegistrationList` 가 아니다.
        INetMessageHandler*         _arrKindHandler[kKindCount];
        INetMessageHandler*         _arrNoticeHandler[kKindCount]; ///< 종류마다 그 종류를 전달 통지로 보내는 처리기(`getNotifiedKindMask`).
        NetMessageKindTallyTable    _kindTally;                    ///< `pump` 가 메시지마다 더한다(꺼내는 반복 안의 덧셈 둘 — 따로 훑지 않는다).
        NetInbound                  _inbound;                      ///< `pump` 가 다시 쓰는 버퍼.
        uint64                      _malformedCount;
        uint64                      _unclaimedNoticeCount;

    private:
        /** @brief 이 객체가 내준 처리기 등록 구독입니다. 풀리면 `releaseHandler` 가 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        ItemSubscriptionList<INetMessageHandler> _subscriptions{ ItemSubscriptionList<INetMessageHandler>::ReleaseFunction::create<&NetMessageRouter::releaseHandler>( this ) };
    };
} // namespace sw
