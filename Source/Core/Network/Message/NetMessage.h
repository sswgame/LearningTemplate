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
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    /** @brief 메시지 서술자의 채널을 누가 정하는가입니다. 표 메시지는 모두 둘 중 하나를 적는다(`NetMessageDef` · `NetMessageSenderChannelDef`). */
    enum class NetChannelPolicy : uint8
    {
        Descriptor = 0, ///< 서술자의 채널(`kChannel`)로만 나간다
        SenderChooses   ///< 보내는 쪽이 상태에 따라 고른다(원격 호출의 신뢰 여부, 멈춘 자세) — `kChannel` 은 평소 채널이다
    };

    /** @brief 줄에 세웠다가 나중에 보내는 메시지 하나입니다 — 서술자에 묶인 채널을 바이트와 함께 든다(꺼낼 때 채널을 다시 고르지 않는다). */
    struct NetQueuedMessage
    {
        vector<uint8>  _bytes{};
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
    };

    /**
     * @class NetMessageWriter
     * @brief 종류 바이트를 쓰고 본문은 돌려준 `BitWriter` 에 이어 쓴 뒤 보냅니다. 버퍼를 다시 쓰므로 메시지마다 할당하지 않습니다.
     * @details 표 메시지의 채널은 서술자 한 곳에서만 나온다.
     *          - 본문 형식이 있는 메시지(`writeMessage`)와 손으로 쓰는 표 메시지(`beginMessage`)는 서술자의 채널로만 나간다 — `sendMessage` · `trySendMessage` ·
     *            `broadcastMessage` · `sendMessageToPeers` · `sendNotifiedMessage` 는 채널을 받지 않는다. 보내는 쪽이 고르는 메시지(`NetChannelPolicy::SenderChooses`)는
     *            `beginMessage( channel )` 에서 한 번 고른다 — 서술자가 허락하지 않으면 컴파일되지 않는다.
     *          - 채널을 받는 `send` · `broadcast` · `sendToPeers` 는 표 밖 종류(`begin( kind )` — 라우터 판 알림, 채널 등록 메시지)만 보낸다. 표 메시지를 그쪽으로 보내려 하면
     *            오류 로그와 함께 보내지 않는다(서술자와 다른 채널로 나가던 스냅샷 확인 · 복제 입력 자리).
     * @code
     *     if ( _messageWriter.writeMessage<NetLockstepMessage::Table, NetLockstepMessage::Input>( body ) ) // 본문 형식이 있는 메시지(`NetMessageBodyDef`)
     *         (void)_messageWriter.sendMessage( *_pHost, connectionID );                              // 서술자의 채널
     *     BitWriter& writer = _messageWriter.beginMessage<NetLockstepMessage::Table, NetLockstepMessage::RollbackInput>(); // 손으로 쓰는 표 메시지
     *     writer.writeVarUint( frame );
     *     (void)_messageWriter.sendMessageToPeers( *_pHost );
     * @endcode
     */
    class SW_API NetMessageWriter
    {
    public:
        NetMessageWriter();

        /** @brief 비우고 표 밖 종류 바이트를 씁니다(라우터 판 알림 · 채널 등록 메시지 · 테스트). 채널을 받는 `send` 계열로만 보냅니다. 표 메시지는 `beginMessage` 입니다. */
        BitWriter& begin( uint8 kind );
        /** @brief 비우고 손으로 쓰는 표 메시지 @p TMessage 의 종류 바이트를 씁니다. 서술자의 채널로만 보냅니다(`sendMessage` 계열). */
        template <typename TTable, typename TMessage>
        BitWriter& beginMessage()
        {
            static_assert( TMessage::kChannelPolicy == NetChannelPolicy::Descriptor, "the sender chooses this message's channel - pass it to beginMessage( channel )" );
            return beginDescribed( TTable::template getKindOf<TMessage>(), TMessage::kChannel );
        }
        /** @brief 보내는 쪽이 채널을 고르는 표 메시지(`NetChannelPolicy::SenderChooses`)를 @p channel 로 시작합니다. 서술자가 허락하지 않으면 컴파일되지 않는다. */
        template <typename TTable, typename TMessage>
        BitWriter& beginMessage( NetChannelKind channel )
        {
            static_assert( TMessage::kChannelPolicy == NetChannelPolicy::SenderChooses, "this message travels on its descriptor channel - use beginMessage()" );
            return beginDescribed( TTable::template getKindOf<TMessage>(), channel );
        }
        /**
         * @brief 비우고 표 @p TTable 의 메시지 @p TMessage 의 종류 바이트와 본문(고정 배치형, `NetMessageBodyDef`)을 씁니다.
         * @return 범위 밖 값이 있으면 오류를 남기고 false 입니다 — 쓰다 만 바이트이므로 보내지 않는다.
         */
        template <typename TTable, typename TMessage>
        [[nodiscard]] bool writeMessage( const typename TMessage::Body& body )
        {
            const bool bWritten = TMessage::writeBody( body, begin( TTable::template getKindOf<TMessage>() ) );
            _messageChannel     = TMessage::kChannel;
            _state              = bWritten ? State::Typed : State::Failed;
            return bWritten;
        }
        /** @brief 손으로 쓴 본문(`begin`)을 @p channel 로 보냅니다. `writeMessage` 로 쓴 메시지는 받지 않습니다(`sendMessage`). */
        [[nodiscard]] bool send( INetPeers& peers, int32 connectionID, NetChannelKind channel ) const;
        /** @brief 손으로 쓴 본문을 연결된 모두에게 보냅니다. @p exceptID 는 제외합니다. 반환값은 보낸 수입니다. */
        int32 broadcast( INetPeers& peers, NetChannelKind channel, int32 exceptID = constant::invalid_index::kInt32 ) const;
        /** @brief 손으로 쓴 본문을 상대 모두에게 보냅니다. 서버는 연결된 모두에게, 클라이언트는 서버(연결 0)에게 보냅니다. 반환값은 보낸 수입니다. */
        int32 sendToPeers( INetPeers& peers, NetChannelKind channel ) const;
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 보냅니다. 쓰지 못했거나 손으로 쓴 본문이면 오류 로그와 false 입니다. */
        [[nodiscard]] bool sendMessage( INetPeers& peers, int32 connectionID ) const;
        /** @brief `sendMessage` 와 같되 신뢰 창이 차면 기다리지 않고 false 입니다(`INetPeers::trySendMessage`). */
        [[nodiscard]] bool trySendMessage( INetPeers& peers, int32 connectionID ) const;
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 연결된 모두에게 보냅니다. 반환값은 보낸 수입니다. */
        int32 broadcastMessage( INetPeers& peers, int32 exceptID = constant::invalid_index::kInt32 ) const;
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 상대 모두에게 보냅니다. 반환값은 보낸 수입니다. */
        int32 sendMessageToPeers( INetPeers& peers ) const;
        /** @brief 전달 통지를 달아 보냅니다(`INetPeers::sendNotifiedMessage` — 비신뢰). 서술자 채널이 `Unreliable` 이 아니면 오류 로그와 false 입니다. */
        [[nodiscard]] bool sendNotifiedMessage( INetPeers& peers, int32 connectionID, uint32 tag ) const;
        /** @brief `writeMessage` 로 쓴 메시지의 채널입니다(줄에 세웠다가 나중에 보내는 쪽이 바이트와 함께 든다). 손으로 쓴 본문이면 nullopt 입니다. */
        optional<NetChannelKind> getMessageChannel() const;
        /** @brief 서술자에 묶인 메시지를 줄에 세울 사본으로 만듭니다. 표 밖 종류(`begin`)이거나 쓰지 못한 메시지면 오류 로그와 nullopt 입니다. */
        [[nodiscard]] optional<NetQueuedMessage> makeQueuedMessage() const;

        /** @brief 검사 · 테스트용 바이트입니다. 보내는 길은 위의 send 계열이다(바이트를 꺼내 채널을 따로 고르지 않는다). */
        const vector<uint8>& getBytes() const { return _writer.getBytes(); }
        int32                getByteCount() const { return _writer.getByteCount(); }

    private:
        /** @brief 지금 버퍼의 메시지가 어느 길로 쓰였는지입니다 — 보내는 함수가 채널을 고를 수 있는지를 정한다. */
        enum class State : uint8
        {
            Raw = 0, ///< `begin` — 표 밖 종류. 채널을 받는 함수로만 보낸다
            Typed,   ///< `writeMessage` 성공 · `beginMessage` — 서술자에 묶인 채널로만 보낸다
            Failed   ///< `writeMessage` 실패 — 쓰다 만 바이트라 보내지 않는다
        };

        [[nodiscard]] bool allowsCallerChannel() const;
        [[nodiscard]] bool allowsDescriptorChannel() const;
        BitWriter&         beginDescribed( uint8 kind, NetChannelKind channel );

    private:
        BitWriter      _writer;
        NetChannelKind _messageChannel;
        State          _state;
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
    /** @brief 메시지가 가는 방향입니다. 메시지 서술자(`NetMessageDef`)마다 하나를 적고, 라우터가 받는 쪽 역할과 견준다. 와이어 버전에 섞인다. */
    enum class NetMessageDirection : uint8
    {
        ToServer = 0, ///< 클라이언트 → 서버
        ToClient,     ///< 서버 → 클라이언트
        Both          ///< 양쪽(락스텝 입력처럼 서버가 받아 다시 나눈다)
    };

    /** @brief 라우터가 메시지를 받는 쪽의 역할입니다. 서버는 `ToServer` · `Both` 종류만, 클라이언트는 `ToClient` · `Both` 종류만 받는다. */
    enum class NetPeerRole : uint8
    {
        Server = 0,
        Client
    };

    /**
     * @brief 처리기가 맡는 종류마다 갈 수 있는 방향입니다. 비트 n 이 (영역 + n)이다(XMachinaServer 의 패킷 방향 표 자리).
     * @details 맡는 종류(`getMessageKindMask`)는 모두 둘 중 한쪽 이상에 있어야 한다 — 방향을 적지 않은 종류가 있으면 라우터가 등록을 받지 않는다(폴백 없음).
     *          받는 쪽 역할의 마스크 밖 종류(서버가 받은 `ToClient` 종류)는 라우터가 `Malformed` 로 세고 그 연결을 끊는다.
     */
    struct NetMessageDirectionMask
    {
        uint16 _toServerMask{ 0 }; ///< 클라이언트 → 서버로 갈 수 있는 종류(`ToServer` · `Both`)
        uint16 _toClientMask{ 0 }; ///< 서버 → 클라이언트로 갈 수 있는 종류(`ToClient` · `Both`)

        constexpr uint16 getInboundMask( NetPeerRole role ) const { return role == NetPeerRole::Server ? _toServerMask : _toClientMask; }
        constexpr uint16 getDeclaredMask() const { return static_cast<uint16>( _toServerMask | _toClientMask ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 메시지 하나의 컨텍스트입니다. 보낸 연결(클라이언트에서는 서버가 0), 채널, 종류(첫 바이트), 메시지 전체(종류 포함, 그대로 중계할 때 씁니다)를 담습니다. */
    struct NetMessageContext
    {
        const uint8*   _pMessage{ nullptr }; ///< 처리기 호출 동안만 유효하다.
        int32          _messageSize{ 0 };
        int32          _connectionID{ constant::invalid_index::kInt32 };
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
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

        /** @brief 표를 쓰지 않는 처리기(연결 사건만 받는다)의 `getWireVersion` 값입니다. 라우터는 이 처리기의 영역을 상대에게 알리지 않는다. */
        static constexpr uint32 kNoMessageTable = 0;

        /** @brief 맡은 영역의 첫 값입니다(키트는 `NetKitMessageRange::kClientServer` 등). 게임 영역(`NetMessageRange::kGame`..)은 16개씩 나눠 여럿이 맡을 수 있습니다. */
        virtual uint8 getMessageRangeBase() const = 0;
        /**
         * @brief 이 처리기가 보내거나 받는 메시지 표의 와이어 판입니다(`NetMessageTable::computeWireVersion`). 표를 쓰지 않으면 `kNoMessageTable` 입니다.
         * @details 순수 가상이라 빠뜨릴 수 없다 — 표 기반 처리기(`NetTableHandler`)는 표에서 저절로 낸다. 라우터가 영역마다 이 값을 상대와 맞춰 보고,
         *          다르면 그 연결을 끊는다(`NetMessageRouter`). 받는 종류가 없어도(보내기만 하는 서버 쪽) 보내는 표의 판을 낸다 — 그래야 받는 쪽과 맞춰 본다.
         */
        virtual uint32 getWireVersion() const = 0;
        /** @brief 영역 안에서 맡는 종류의 마스크입니다. 비트 n 이 (영역 + n)에 해당하며, 한 영역을 여럿이 나눠 맡을 때 겹치지 않게 고릅니다. 0이면 사건만 받습니다. */
        virtual uint16 getMessageKindMask() const { return 0xFFFFu; }
        /**
         * @brief 맡는 종류마다 갈 수 있는 방향입니다(`NetMessageDirectionMask`). 표 기반 처리기(`NetTableHandler`)는 표의 `NetMessageDef` 에서 저절로 낸다.
         * @details 순수 가상이라 빠뜨릴 수 없다. 맡는 종류 중 방향이 없는 것이 있으면 라우터가 등록을 거절한다. 사건만 받는 처리기는 빈 값을 낸다.
         */
        virtual NetMessageDirectionMask getMessageDirectionMask() const = 0;
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
         * @brief 클라이언트가 서버의 새 월드 세대를 받았습니다(`NetMessageRouter::openWorld`). 같은 `poll` 의 뒤따르는 메시지보다 먼저 부른다.
         * @details 세대를 세션 번호로 쓰는 처리기(락스텝 판)가 그 세대의 첫 메시지를 받기 전에 갈아 끼우는 자리다. 서버 쪽에는 부르지 않는다(스스로 열었다).
         */
        virtual void onWorldOpened( uint32 generation ) { (void)generation; }
        /**
         * @brief 이번 프레임의 사건 · 메시지를 다 나눠 줬습니다(`NetMessageRouter::poll` 의 끝). 게임 프레임(조종 · 틱)은 이 뒤에 돕니다.
         * @details 프레임 안에서 값이 바뀌면 안 되는 것(화면 시각)을 여기서 한 번 정한다 — 프레임 중간에 움직이면 같은 프레임의 표본끼리 시각이 갈린다.
         */
        virtual void onPollFinished() {}
        /**
         * @brief 영역 안에서 이 처리기가 전달 통지를 달아 보내는(`INetPeers::sendNotifiedMessage`) 종류의 마스크입니다. 비트 n 이 (영역 + n)입니다.
         * @details 라우터는 그 종류의 판정을 이 처리기의 `onDeliveryNotification` 에 넘긴다. 받는 종류 마스크와 따로 세며, 한 종류를 둘이 보내겠다고 하면 나중 것을 받지 않는다.
         */
        virtual uint16 getNotifiedKindMask() const { return 0; }
        /** @brief 이 처리기가 보낸 전달 통지 메시지 하나의 판정입니다(라우터 `poll` 가 사건 뒤, 메시지 앞에 부른다). */
        virtual void onDeliveryNotification( const NetDeliveryNotification& notification ) { (void)notification; }

        /** @brief @p kind 가 이 처리기의 영역과 마스크 안이면 true 를 반환합니다. */
        bool isMessageKindHandled( uint8 kind ) const;
        /**
         * @brief 메시지 하나(첫 바이트가 종류)를 라우터 없이 직접 전달합니다. 라우터를 쓰지 않는 게임이나 테스트에서 씁니다. 맡은 종류가 아니면 NotMine 을 반환합니다.
         * @details 직접 전달하는 쪽은 연결 사건도 `onConnectionOpened` 와 `onConnectionClosed` 로 직접 넘긴다.
         */
        NetHandleResult handleMessage( int32 connectionID, const uint8* pData, int32 size, NetChannelKind channel = NetChannelKind::ReliableOrdered );
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
        int32          _connectionID{ constant::invalid_index::kInt32 };
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetMessageRouter
     * @brief 받은 메시지를 종류(첫 바이트)마다 맡은 처리기 하나에게 바로 전달합니다(종류 256개짜리 테이블). 연결 사건은 메시지보다 먼저 모든 처리기에 알립니다.
     *        영역마다 쓰는 메시지 표의 판을 상대와 맞춰 보고, 다르면 그 연결을 끊습니다(아래 "판 맞춤").
     * @details 판 맞춤: 키트 처리기는 접속한 뒤에 등록되므로(게임이 링크를 세우는 첫 틱) 접속 핸드셰이크 판(`NetEndpointConfig::_wireVersion`)에 섞을 수 없다.
     *          그래서 라우터가 맡는다 — 처리기의 판(`INetMessageHandler::getWireVersion`, 순수 가상)을 연결이 열릴 때와 등록이 바뀐 다음 `poll` 에서 상대 모두에게
     *          알리고(`NetMessageRange::kRouterWireVersionKind`), 상대가 알린 판은 연결마다 들고 있다가 양쪽이 같은 영역을 쓰면 견준다. 다르면 오류를 남기고 그 연결을 끊고,
     *          같은 `poll` 안의 그 연결 메시지는 처리기에 넘기지 않는다. 상대가 아직 등록하지 않은 영역은 견주지 않는다(뒤에 등록하면 그때 알려 온다).
     *          게임이 표 판을 손으로 섞던 방식(`NetWireVersion::combine`)은 빠뜨릴 수 있었다 — 섞는 게임이 하나도 없었다.
     * @details - 처리기는 빌려 쓴다. 등록은 `registerHandler` 가 돌려주는 구독(`Subscription`)이 소유한다 — 처리기와 같은 수명의 객체(보통 처리기를 가진 쪽)가 들고,
     *            구독이 사라지면 처리기가 빠진다. 라우터가 먼저 사라지거나 세션이 닫히면(`closeSession`) 구독은 무효가 된다. 한 종류를 둘이 맡으려 하면 나중 것을 받지 않는다(빈 구독).
     *          - `poll` 는 엔드포인트에서 사건과 메시지를 잠금 한 번에 꺼내(`INetPeers::pollInbound` — 엔드포인트 하나든 포트 샤드 묶음이든) 잠금 밖에서 나눠 준다. 처리기는 그 안에서 보내도 된다.
     *          - 깨진 메시지(처리기가 Malformed 를 반환)는 세고(`getMalformedCount`) 버린다.
     *          - 등록과 구독의 해제는 `poll` 를 부르는 스레드에서 한다(구독 목록에 잠금이 없다).
     * @code
     *     _replicationSubscription = router.registerHandler( &replicationServer );
     *     _gameSubscription        = router.registerHandler( &gameMessages );     // kGame..kGame + 0x0F
     *     router.poll( host, &listUnhandled );                                    // 매 틱 host.update 뒤
     * @endcode
     */
    class SW_API NetMessageRouter
    {
    public:
        static constexpr int32 kKindCount = NetMessageKindStatsTable::kKindCount;

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
        /**
         * @brief 이 라우터를 쓰던 세션이 닫혔습니다 — 등록을 모두 풀고(들고 있던 구독은 무효가 된다) 상대마다 들던 판을 잊으며, 세션 번호를 올립니다.
         * @details 라우터를 여러 세션에 다시 쓰는 주인(`GameNetSession`)이 닫을 때 부른다 — 처리기를 쥔 쪽이 푸는 것을 잊어도 다음 세션의 메시지 · 사건을 받지 않고,
         *          다음 세션의 처리기가 "이미 맡은 종류" 로 거절되지 않는다. 링크(`NetLinkValue`)는 세션 번호로 닫힌 세션을 가려낸다.
         */
        void closeSession();
        /** @brief 지금 세션의 번호입니다(`closeSession` 마다 는다). 번호가 다르면 닫힌 세션의 것이다. */
        uint32 getSessionSerial() const { return _sessionSerial; }
        /**
         * @brief 서버가 새 월드(씬)를 열었습니다 — 세대를 하나 올리고 연결된 모두에게 알립니다(뒤에 붙는 연결은 열릴 때 받는다).
         * @details 오브젝트 번호는 씬마다 다시 매겨진다(씬 번호는 이름 해시라 다음 씬의 같은 이름과 겹친다). 번호를 실은 메시지는 이 세대를 함께 싣고 다르면 버린다
         *          (`SceneNetLink::getWorldGeneration`). 유니티 NGO 처럼 번호를 다시 쓰지 않는 길은 양쪽이 이름으로 따로 내는 씬 번호를 덮지 못해, 언리얼이 레벨 이동마다 NetGUID 를
         *          새로 짜는 것처럼 서버가 정하는 세대로 가른다. 양쪽이 따로 세면 클라이언트 혼자 씬을 다시 여는 때 어긋난다.
         */
        void openWorld( INetPeers& peers );
        /** @brief 지금 월드의 세대입니다. 서버는 자기가 연 것, 클라이언트는 서버에게 받은 것이다. 아직 없으면 0. */
        uint32 getWorldGeneration() const { return _worldGeneration; }
        /**
         * @brief 메시지 하나를 그 종류의 처리기에 넘깁니다. 처리기가 없으면(빈 메시지도) NotMine 을 반환합니다.
         * @details 받는 쪽 역할 @p role 로 갈 수 없는 종류(서버가 받은 `ToClient` 종류)는 처리기에 넘기지 않고 세고 Malformed 를 반환합니다.
         *          `poll` 은 그 연결도 끊는다 — 반대 방향 종류는 상대가 프로토콜을 어긴 것이다.
         */
        NetHandleResult dispatch( NetPeerRole role, const NetMessageContext& context, const uint8* pData, int32 size );
        /** @brief 연결 사건 하나를 모든 처리기에 등록 순서대로 알립니다. */
        void dispatchEvent( const NetEndpointEvent& event );
        /**
         * @brief @p peers 에 쌓인 연결 사건과 받은 메시지를 모두 꺼내 사건부터 나눠 줍니다. 반환값은 꺼낸 메시지 수입니다.
         * @param pOutUnhandled 처리기가 없는 메시지를 뒤에 붙입니다(nullptr 이면 버립니다).
         * @param pOutListEvent 꺼낸 사건을 뒤에 붙입니다(처리기 말고도 게임이 알아야 할 때 씁니다).
         */
        int32 poll( INetPeers& peers, vector<NetReceivedMessage>* pOutUnhandled = nullptr, vector<NetEndpointEvent>* pOutListEvent = nullptr );
        /**
         * @brief 서버가 받은 메시지를 보낸 연결을 뺀 모두에게 **받은 채널 그대로** 다시 보냅니다. 클라이언트에서는 하지 않고 0을 반환합니다. 반환값은 보낸 수입니다.
         * @details 처리기 안에서 부른다(@p context 의 메시지 바이트는 처리기 호출 동안만 유효하다).
         */
        static int32      relayToOtherPeers( INetPeers& peers, const NetMessageContext& context );
        const DevCounter& getMalformedCount() const { return _malformedCount; }
        /** @brief 그 종류를 보낸다고 등록한 처리기가 없어 버린 전달 통지 수입니다(첫 번째는 오류 로그도 남긴다 — 보낸 쪽이 처리기를 등록하지 않았다). */
        uint64 getUnclaimedNotificationCount() const { return _unclaimedNotificationCount; }
        /** @brief `poll` 가 지금까지 꺼낸 종류별 메시지 수와 바이트입니다(받은 쪽 셈 — 처리기가 없는 메시지도 센다. 보낸 쪽은 `INetPeers::addSentKindStats`). */
        const NetMessageKindStatsTable& getKindStats() const { return _kindStats; }

    private:
        /** @brief 처리기의 등록 구독이 풀렸습니다. 그 처리기(같은 처리기가 여럿 올랐으면 먼저 것 하나)를 빼고 종류 표를 다시 짓습니다. */
        void releaseHandler( INetMessageHandler* pHandler );
        void rebuildKindTable();
        /** @brief 전달 통지 하나를 그 종류를 보내는 처리기에 넘깁니다. 없으면 세고 버립니다. */
        void dispatchNotification( const NetDeliveryNotification& notification );
        /** @brief 영역 @p rangeBase 를 쓰는 이 쪽 처리기의 표 판입니다. 없으면 `kNoMessageTable` 입니다. */
        uint32 findWireVersion( uint8 rangeBase ) const;
        /** @brief 이 쪽 처리기의 판 알림을 @p connectionID 에(-1 이면 상대 모두에게) 보냅니다. 알릴 판이 없으면 보내지 않습니다. */
        void sendWireVersions( INetPeers& peers, int32 connectionID );
        /** @brief 상대 @p connectionID 의 판 알림을 읽어 들고 이 쪽과 견줍니다. 깨졌거나 다르면 연결을 끊습니다. */
        void receiveWireVersions( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 지금 월드 세대를 @p connectionID 에(-1 이면 상대 모두에게) 보냅니다. */
        void sendWorldGeneration( INetPeers& peers, int32 connectionID );
        /** @brief 서버가 보낸 월드 세대를 읽어 들고 처리기에 알립니다. 깨졌거나 서버가 받은 것이면 연결을 끊습니다. */
        void receiveWorldGeneration( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 종류 @p kind 를 역할 @p role 쪽이 받을 수 있으면 true 입니다(맡은 처리기의 방향 선언). */
        bool isInboundAllowed( uint8 kind, NetPeerRole role ) const;
        /** @brief 반대 방향 종류를 보낸 연결 @p connectionID 를 세고 끊습니다. 같은 `poll` 의 남은 메시지도 넘기지 않는다. */
        void refuseWrongDirection( INetPeers& peers, int32 connectionID, uint8 kind, NetPeerRole role );
        /** @brief 들고 있는 상대 판과 이 쪽 판을 견주어 다른 연결을 끊습니다. 다르면 true 입니다. */
        bool disconnectOnWireVersionMismatch( INetPeers& peers, int32 connectionID );

        /** @brief 상대가 알린 영역 하나의 표 판입니다. */
        struct PeerWireVersion
        {
            uint32 _wireVersion{ 0 };
            uint8  _rangeBase{ 0 };
        };

        static constexpr uint8 kDirectionToServerBit = 1u << 0;
        static constexpr uint8 kDirectionToClientBit = 1u << 1;

    private:
        vector<INetMessageHandler*>                   _listHandler; ///< 등록 순서대로 보관한다. 사건만 받는 처리기는 등록마다 하나씩 오르므로(중복 허용) `RegistrationList` 가 아니다.
        INetMessageHandler*                           _arrKindHandler[kKindCount];
        INetMessageHandler*                           _arrNotificationHandler[kKindCount]; ///< 종류마다 그 종류를 전달 통지로 보내는 처리기(`getNotifiedKindMask`).
        uint8                                         _arrKindDirection[kKindCount];       ///< 종류마다 맡은 처리기가 적은 방향 비트(`kDirectionToServerBit` · `kDirectionToClientBit`).
        NetMessageKindStatsTable                      _kindStats;                          ///< `poll` 가 메시지마다 더한다(꺼내는 반복 안의 덧셈 둘 — 따로 훑지 않는다).
        NetInbound                                    _inbound;                            ///< `poll` 가 다시 쓰는 버퍼.
        DevCounter                                    _malformedCount;
        uint64                                        _unclaimedNotificationCount;
        unordered_map<int32, vector<PeerWireVersion>> _mapPeerWireVersion;    ///< 연결마다 상대가 알린 표 판. 연결이 닫히면 지운다
        vector<int32>                                 _listRefusedConnection; ///< 이번 `poll` 에서 판이 달라 끊은 연결 — 남은 메시지를 넘기지 않는다
        uint32                                        _sessionSerial;         ///< 지금 세션의 번호(`closeSession` 이 올린다)
        uint32                                        _worldGeneration;       ///< 서버가 연 월드의 세대(`openWorld`). 0 이면 아직 없다
        uint8                                         _bWireVersionsChanged;  ///< 등록이 바뀌었다 — 다음 `poll` 이 상대 모두에게 알리고 들고 있는 상대 판과 견준다
        /** @brief 이 객체가 내준 처리기 등록 구독입니다. 풀리면 `releaseHandler` 가 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        ItemSubscriptionList<INetMessageHandler> _subscriptions{ SW_DELEGATE_METHOD( ItemSubscriptionList<INetMessageHandler>::ReleaseFunction, &NetMessageRouter::releaseHandler, this ) };
    };
} // namespace sw
