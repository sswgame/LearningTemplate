/**
 * @file INetMessageHandler.h
 * @brief 메시지 영역 하나를 맡는 처리기 인터페이스와 그 계약(처리 결과 · 방향 · 받은 메시지의 컨텍스트)을 정의합니다.
 * @details 연결 사건(열림, 닫힘)은 라우터(`NetMessageRouter`)가 메시지보다 먼저 처리기에 알린다. 그래서 키트는 수신 가드나 연결 수명을 직접 배선하지 않고 자기 종류만 읽으면 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/NetChannelKind.h"
#include "Core/Network/NetConnectionState.h"
#include "Core/Network/NetDeliveryNotification.h"
#include "Core/Network/NetMessageRange.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    /** @brief 처리기가 메시지 하나를 어떻게 처리했는지 나타냅니다. */
    enum class NetHandleResult : uint8
    {
        Handled = 0, ///< 받아서 처리했다(낡아서 버린 것도 포함한다. 형식은 맞았다).
        Malformed,   ///< 내 종류인데 본문이 깨졌다. 라우터가 세고 버리며, 다른 처리기나 남은 목록으로 넘기지 않는다.
        NotMine,     ///< 맡은 종류가 아니다(`handleMessage` 로 직접 전달할 때만 반환한다. 라우터는 마스크로 미리 거른다).
        NotReady     ///< 아직 처리할 수 없다(대상이 서지 않았다). 서술자가 보류를 선언한 종류(`NetHeldMessageDesc`)만 낼 수 있고, 라우터가 보류했다가 순서대로 다시 준다.
    };

    /** @brief 보류한 메시지를 끝내 전하지 못한 까닭입니다. */
    enum class NetDeliveryFailure : uint8
    {
        TimedOut = 0, ///< 조건이 차지 않은 채 선언한 시간이 지났다
        Overflow,     ///< 그 연결의 보류 줄이 찼다
        WorldChanged  ///< 기다리는 사이 월드가 바뀌었다 — 대상은 앞 월드의 것이다
    };

    /** @brief 메시지가 가는 방향입니다. 메시지 서술자(`NetMessageDesc`)마다 하나를 적고, 라우터가 받는 쪽 역할과 견준다. 와이어 버전에 섞인다. */
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

    /** @brief 받은 메시지 하나의 컨텍스트입니다. 보낸 연결(처리기에 닿는 것은 늘 유효하다 — 클라이언트에서는 서버가 0), 채널, 종류(첫 바이트), 메시지 전체(종류 포함, 그대로 중계할 때 씁니다)를 담습니다. */
    struct NetMessageContext
    {
        const uint8*   _pMessage{ nullptr }; ///< 처리기 호출 동안만 유효하다.
        int32          _messageSize{ 0 };
        int32          _connectionID{ constant::invalid_index::kInt32 };
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
        uint8          _kind{ 0 };
    };

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
         * @brief 맡는 종류마다 갈 수 있는 방향입니다(`NetMessageDirectionMask`). 표 기반 처리기(`NetTableHandler`)는 표의 `NetMessageDesc` 에서 저절로 낸다.
         * @details 순수 가상이라 빠뜨릴 수 없다. 맡는 종류 중 방향이 없는 것이 있으면 라우터가 등록을 거절한다. 사건만 받는 처리기는 빈 값을 낸다.
         */
        virtual NetMessageDirectionMask getMessageDirectionMask() const = 0;
        /**
         * @brief 종류 @p kind 를 서버가 한 연결에게서 `poll` 한 번에 받는 상한입니다. 표 기반 처리기는 서술자(`NetLimitedMessageDesc`)에서 저절로 낸다.
         * @details 서버로 갈 수 있는 종류의 상한이 0 이면 라우터가 등록을 거절한다(상한 없는 서버 처리기는 없다).
         */
        virtual uint32 getMaxPerPoll( uint8 kind ) const
        {
            (void)kind;
            return 0;
        }
        /** @brief 연결이 열렸습니다(서버에서는 클라이언트, 클라이언트에서는 서버 = 0). 이 연결 ID의 상태를 새로 시작합니다. */
        virtual void onConnectionOpened( int32 connectionID ) { (void)connectionID; }
        /** @brief 연결이 닫혔습니다. 이 연결 ID의 상태를 버립니다(같은 ID는 다음 연결이 다시 씁니다). */
        virtual void onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
        {
            (void)connectionID;
            (void)reason;
        }
        /** @brief 이 처리기의 메시지가 월드 세대를 싣는가입니다(`NetWorldMessageTable`). 표 기반 처리기는 표에서 저절로 낸다. */
        virtual bool isWorldScoped() const { return false; }
        /**
         * @brief 클라이언트의 월드가 서버의 새 월드 세대가 됐습니다 — 알림이 오고 이쪽 씬이 그 씬일 때다. 같은 `poll` 의 뒤따르는 메시지보다 먼저 부른다.
         * @details 서버의 새 월드는 번호 · 틱을 처음부터 센다 — 월드의 상태를 든 처리기는 앞 월드의 것을 여기서 버린다(같은 씬을 다시 열면 이쪽 씬은 그대로일 수 있다).
         *          세대를 세션 번호로 쓰는 처리기(락스텝 판)가 갈아 끼우는 자리이기도 하다. 서버 쪽에는 부르지 않는다(스스로 열었다).
         */
        virtual void onWorldOpened( uint32 generation ) { (void)generation; }
        /**
         * @brief 서버 — 연결 @p connectionID 의 클라이언트가 이쪽 월드에 들어왔습니다(씬을 세웠다). 그 연결의 월드 상태를 처음부터 보냅니다.
         * @details 들어온 뒤에 단 처리기에도 다음 `poll` 의 처음에 알린다 — 단 차례와 무관하게 연결마다 한 번이다. 언리얼 `ServerNotifyLoadedWorld` 뒤의 첫 복제 자리다.
         */
        virtual void onPeerEnteredWorld( int32 connectionID ) { (void)connectionID; }
        /** @brief 서버 — 연결 @p connectionID 의 클라이언트가 이쪽 월드를 나갔습니다(다른 씬 · 끊김 · 서버가 새 월드를 열었다). 그 연결의 월드 상태를 버립니다. */
        virtual void onPeerLeftWorld( int32 connectionID ) { (void)connectionID; }
        /**
         * @brief 종류 @p kind 를 처리기가 `NotReady` 로 미룰 때 보류하는 시간(밀리초)입니다. 0 이면 보류하지 않습니다. 표 기반 처리기는 서술자(`NetHeldMessageDesc`)에서 저절로 낸다.
         */
        virtual uint32 getHoldLimit( uint8 kind ) const
        {
            (void)kind;
            return 0;
        }
        /** @brief 보류하는 종류의 열쇠(대상 · 세션 번호)입니다. 같은 연결 · 영역 · 열쇠의 메시지는 앞의 것이 전해질 때까지 순서대로 기다립니다. @p body 는 종류 바이트 다음부터인 사본입니다. */
        virtual uint64 readMessageHoldKey( uint8 kind, BitReader body ) const
        {
            (void)kind;
            (void)body;
            return 0;
        }
        /** @brief 이쪽이 보낸 영역 안의 종류 @p kind 메시지를 상대가 보류했다가 끝내 전하지 못했습니다(보류를 선언한 종류만). */
        virtual void onMessageDeliveryFailed( int32 connectionID, uint8 kind, uint64 key, NetDeliveryFailure reason )
        {
            (void)connectionID;
            (void)kind;
            (void)key;
            (void)reason;
        }
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

    protected:
        friend class NetMessageRouter;

        /**
         * @brief 맡은 종류의 메시지 하나를 처리합니다. @p body 는 종류 바이트 다음부터입니다.
         * @details 부르는 쪽은 라우터와 `handleMessage` 둘이고 둘 다 연결 번호가 없는 메시지를 거른다 — 처리기는 `context._connectionID` 가 유효한지 보지 않는다.
         * @return 본문을 읽지 못했으면 Malformed 를 반환하며, 라우터가 세고 버립니다. 낡은 것을 버린 경우는 Handled 입니다.
         */
        virtual NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) = 0;
    };
} // namespace sw
