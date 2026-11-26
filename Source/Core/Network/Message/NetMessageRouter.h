/**
 * @file NetMessageRouter.h
 * @brief 엔드포인트가 받은 메시지를 종류(첫 바이트)마다 맡은 처리기 하나에게 나눠 주는 라우터를 정의합니다.
 * @details 네트워크 키트(`GF_Net*`)와 게임은 모두 "종류 바이트 + 비트 본문" 형식의 메시지를 주고받는다. 받은 메시지는 라우터가 종류마다 맡은 처리기 하나에게(영역과 종류 마스크로) 나눠 준다.
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
#include "Core/Network/Message/INetMessageHandler.h"
#include "Core/Network/NetChannelKind.h"
#include "Core/Network/NetConnectionState.h"
#include "Core/Network/NetDeliveryNotification.h"
#include "Core/Network/NetMessageKindStats.h"
#include "Core/Network/NetMessageRange.h"
#include "Core/Network/NetProtocol.h"
#include "Core/Network/Replication/NetClock.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/TimeSeconds.h"

namespace sw
{
    /** @brief 처리기가 없는 메시지입니다. */
    struct NetReceivedMessage
    {
        vector<uint8>  _buffer{};
        int32          _connectionID{ constant::invalid_index::kInt32 };
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
    };

    /**
     * @class NetMessageRouter
     * @brief 받은 메시지를 종류(첫 바이트)마다 맡은 처리기 하나에게 바로 전달합니다(종류 256개짜리 테이블). 연결 사건은 메시지보다 먼저 모든 처리기에 알립니다.
     *        영역마다 쓰는 메시지 표의 판을 상대와 맞춰 보고, 다르면 그 연결을 끊습니다(아래 "판 맞춤").
     * @details 판 맞춤: 키트 처리기는 접속한 뒤에 등록되므로(게임이 링크를 세우는 첫 틱) 접속 핸드셰이크 판(`NetEndpointConfig::_wireVersion`)에 섞을 수 없다.
     *          그래서 라우터가 맡는다 — 처리기의 판(`INetMessageHandler::getWireVersion`, 순수 가상)을 연결이 열릴 때와 등록이 바뀐 다음 `poll` 에서 상대 모두에게
     *          알리고(`NetMessageRange::kRouterWireVersionKind`), 상대가 알린 판은 연결마다 들고 있다가 양쪽이 같은 영역을 쓰면 견준다. 다르면 오류를 남기고 그 연결을 끊고,
     *          같은 `poll` 안의 그 연결 메시지는 처리기에 넘기지 않는다. 상대가 아직 등록하지 않은 영역은 견주지 않는다(뒤에 등록하면 그때 알려 온다).
     *          게임이 표 판을 손으로 섞던 방식(`NetWireVersion::combine`)은 빠뜨릴 수 있었다 — 섞는 게임이 하나도 없었다.
     * @details - 보류: 처리기가 아직 처리할 수 없는 메시지(대상이 서지 않았다 — `NotReady`)와, 처리기가 아직 없는 신뢰 메시지(씬을 읽는 중)는 버리지 않고 상한(연결마다
     *            개수 · 바이트, 종류마다 시간) 안에서 들고 있다가 `poll` 마다 온 순서대로 다시 준다. 시한 · 상한을 넘거나 월드가 바뀌면 버리고, 보류를 선언한 종류면 보낸 쪽에 알린다
     *            (`onMessageDeliveryFailed`). 유니티 NGO `DeferredMessageManager`(스폰 앞에 온 RPC, `SpawnTimeout`) · 언리얼 GUID 대기 큐 자리 — 시한 뒤 조용히 버리는 것만 다르다.
     *          - 처리기는 빌려 쓴다. 등록은 `registerHandler` 가 돌려주는 구독(`Subscription`)이 소유한다 — 처리기와 같은 수명의 객체(보통 처리기를 가진 쪽)가 들고,
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
        /** @brief 연결 하나가 보류 줄에 둘 수 있는 메시지 수 · 바이트입니다. 넘는 것은 전하지 못한 것이다(상대가 줄을 끝없이 늘리지 못한다). */
        static constexpr int32 kMaxHeldMessages = 64;
        static constexpr int32 kMaxHeldBytes    = 128 * 1024;
        /** @brief 연결 하나가 `kPresenceWindowMilliseconds` 안에 알릴 수 있는 월드 들고 남의 수입니다. 넘으면 끊는다 — 들어올 때마다 서버가 월드 상태를 처음부터 보낸다. */
        static constexpr int32 kMaxPresencePerWindow       = 16;
        static constexpr int64 kPresenceWindowMilliseconds = 10000;
        /** @brief 처리기가 아직 없는 신뢰 메시지를 기다리는 시간(밀리초)입니다 — 씬을 읽고 처리기를 다는 동안이다. */
        static constexpr int64 kMissingHandlerHoldMilliseconds = 10000;
        /** @brief 서버 시계를 알리는 간격(서버 틱)입니다 — 0.1 초. 언리얼 `ServerWorldTimeSecondsUpdateFrequency` 의 기본과 같다. */
        static constexpr uint32 kClockSendTicks = 6;

        /** @brief 보류 시한이 읽는 단조 시계(밀리초)입니다. */
        using ClockFunction = int64 ( * )();

        NetMessageRouter();

        /** @brief 보류 시한의 시계를 바꿉니다(기본은 `MonotonicClock`). 테스트 · 하니스가 실행 속도와 무관하게 돌린다. nullptr 은 받지 않는다(`SW_ASSERT`). */
        void setClock( ClockFunction pClock );
        /** @brief 보류 중인 메시지 수입니다. */
        int32 getHeldMessageCount() const { return static_cast<int32>( _listHeld.size() ); }

        NetMessageRouter( const NetMessageRouter& )            = delete;
        NetMessageRouter& operator=( const NetMessageRouter& ) = delete;

        /**
         * @brief 처리기를 등록하고 그 등록을 소유하는 구독을 반환합니다. 구독이 사라지면 처리기가 빠집니다.
         * @details 맡으려는 종류 중 하나라도 이미 맡은 처리기가 있거나(같은 처리기를 다시 달아도 자기 종류와 겹친다), @p pHandler 가 nullptr 이면 통째로 받지 않고
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
        void openWorld( INetPeers& peers, uint64 sceneID );
        /**
         * @brief 클라이언트가 씬 @p sceneID 를 세웠습니다(`SceneNetLink::bind`). 서버가 알린 월드의 씬이면 그 세대가 이쪽 월드의 세대가 되고 서버에 들어왔다고 알립니다.
         * @details 부르기 전에는 어느 월드에도 들지 않는다 — 서버의 월드가 문서 없는 월드(0)여도 같다.
         *          서버가 먼저 새 씬을 열면 그 메시지는 앞 씬의 오브젝트에 닿으면 안 된다 — 이쪽 씬이 다르면 세대는 0 이고, 서버는 들어온 연결에만 월드 범위 메시지를 보낸다.
         *          같은 씬을 다시 여는 것(재대결)은 씬 번호로 가를 수 없어 알림이 온 때 바뀐다.
         */
        void setLocalScene( INetPeers& peers, uint64 sceneID );
        /** @brief 월드를 올린 횟수입니다(`openWorld` · `setLocalScene`). 같은 라우터에 나중에 올라온 씬이 있는지 가른다. */
        uint32 getBindSerial() const { return _bindSerial; }
        /** @brief 이쪽 월드의 세대입니다. 서버는 자기가 연 것, 클라이언트는 서버에게 받았고 이쪽 씬이 그 씬인 것이다. 아직 없거나 씬이 다르면 0. */
        uint8 getWorldGeneration() const { return _worldGeneration; }
        /** @brief 세대 @p generation 이 이쪽 월드보다 뒤(아직 서지 않은 월드)면 true 입니다. 월드를 손으로 싣는 원격 호출만 묻는다(월드 범위 표의 메시지는 같은 월드에만 나간다). */
        bool isLaterWorld( uint8 generation ) const;
        /**
         * @brief 이쪽 월드의 서버 시계를 프레임에 한 번 흘립니다(`SceneNetLink::update`). 서버면 `kClockSendTicks` 마다 연결된 모두에게 알립니다.
         * @details 시계는 월드 세대와 수명이 같다 — 새 월드 · 새 연결이면 처음부터다.
         */
        void advanceWorldClock( INetPeers& peers, GameSeconds frameTime );
        /** @brief 이쪽 월드의 서버 시계입니다. 서버 틱을 실은 메시지를 받은 키트는 `observe` 로 알린다. */
        NetClock&       getWorldClock() { return _worldClock; }
        const NetClock& getWorldClock() const { return _worldClock; }
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
         * @brief 서버가 받은 메시지를 보낸 연결을 뺀 모두에게 받은 채널 그대로 다시 보냅니다. 클라이언트에서는 하지 않고 0을 반환합니다. 반환값은 보낸 수입니다.
         * @details 처리기 안에서 부른다(@p context 의 메시지 바이트는 처리기 호출 동안만 유효하다).
         */
        static int32      relayToOtherPeers( INetPeers& peers, const NetMessageContext& context );
        const DevCounter& getMalformedCount() const { return _malformedCount; }
        /** @brief 다른 월드의 메시지라 처리기에 넘기지 않은 수입니다(월드가 바뀌는 사이 날아오던 것). */
        const DevCounter& getOtherWorldCount() const { return _otherWorldCount; }
        /** @brief 서버 수신 상한(`NetLimitedMessageDesc`)을 넘어 버린 비신뢰 메시지 수입니다. */
        const DevCounter& getOverLimitCount() const { return _overLimitCount; }
        /** @brief 보류했다가 시한 · 상한 · 월드 바뀜으로 전하지 못한 메시지 수입니다. */
        const DevCounter& getDeliveryFailedCount() const { return _deliveryFailedCount; }
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
        /** @brief 메시지의 월드가 이쪽 월드의 앞 · 같음 · 뒤 가운데 어디인지입니다. */
        enum class WorldOrder : uint8
        {
            Same = 0,
            Earlier, ///< 앞 월드의 늦은 것
            Later    ///< 아직 이쪽에 서지 않은 월드의 것
        };

        /** @brief 보류 중인 메시지 하나입니다. */
        struct HeldMessage
        {
            vector<uint8>  _buffer{};
            uint64         _key{ 0 };
            int64          _deadline{ 0 };
            int32          _connectionID{ 0 };
            NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
            uint8          _bMissingHandler{ SW_FALSE }; ///< 처리기가 없어 기다린다(열쇠를 모른다 — 같은 영역의 뒤 메시지가 모두 뒤에 선다)
            uint8          _rangeBase{ 0 };
        };

        /** @brief 보류한 메시지를 온 순서대로 다시 줍니다. 앞의 같은 열쇠가 남아 있으면 건너뛰고, 시한이 지난 것은 버립니다. */
        void retryHeldMessages( INetPeers& peers, NetPeerRole role );
        /** @brief 메시지 하나를 처리기에 주거나 보류합니다. NotMine 이면 처리기도 보류도 아닙니다. */
        NetHandleResult deliverOrHold( INetPeers& peers, NetPeerRole role, const NetInboundMessage& message, const uint8* pData, bool bHoldMissingHandler );
        /** @brief 보류 줄에 세웁니다. 줄이 찼으면 전하지 못한 것으로 끝냅니다. */
        void holdMessage( INetPeers& peers, const NetInboundMessage& message, const uint8* pData, uint64 key, int64 holdMilliseconds, bool bMissingHandler );
        /** @brief 같은 연결 · 영역에 처리기를 기다리는 앞 메시지가 있으면 true 입니다. */
        bool isBlockedByMissingHandler( int32 connectionID, uint8 rangeBase ) const;
        bool isBlockedByHeld( int32 connectionID, uint8 rangeBase, uint64 key ) const;
        /** @brief 전하지 못한 메시지를 세고, 보류를 선언한 종류면 보낸 쪽에 알립니다. */
        void reportDeliveryFailed( INetPeers& peers, const HeldMessage& held, NetDeliveryFailure reason );
        /** @brief 상대가 알린 "전하지 못함" 을 그 영역의 처리기에 넘깁니다. 깨졌으면 연결을 끊습니다. */
        void receiveDeliveryFailed( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 알림과 이쪽 씬을 맞춰 이쪽 월드의 세대를 정합니다. */
        void updateClientWorld( INetPeers& peers );
        void setWorldGeneration( INetPeers& peers, uint8 generation );
        /** @brief @p generation 이 @p localGeneration 의 앞인지 뒤인지입니다. 1 ~ 255 를 돌고 0 은 "월드 없음" 이다. */
        static WorldOrder compareWorld( uint8 generation, uint8 localGeneration );
        /** @brief 월드 범위 메시지가 다른 월드의 것이면 true 입니다. 세대 바이트가 없으면(깨진 메시지 — 처리기 앞에서 거른다) 같은 것으로 봅니다. */
        bool isFromOtherWorld( const INetMessageHandler& handler, const uint8* pData, int32 size ) const;
        /** @brief 새로 단 처리기에 이미 월드에 들어와 있는 연결을 알립니다(서버). */
        void announceEnteredPeers( INetPeers& peers );
        /** @brief 연결 @p connectionID 가 월드를 나간 것으로 하고 처리기에 알립니다. 들어와 있지 않았으면 할 일이 없습니다. */
        void removeFromWorld( INetPeers& peers, int32 connectionID );
        /** @brief 클라이언트 — 이쪽 월드의 세대를 서버에 알립니다(0 이면 나갔다). */
        void sendPresence( INetPeers& peers );
        /** @brief 클라이언트가 알린 들고 남을 읽습니다. 서버의 지금 세대가 아니면 나간 것입니다. 깨졌거나 클라이언트가 받은 것이면 연결을 끊습니다. */
        void receivePresence( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 지금 월드 세대를 @p connectionID 에(-1 이면 상대 모두에게) 보냅니다. */
        void sendWorldGeneration( INetPeers& peers, int32 connectionID );
        /** @brief 서버가 보낸 월드 세대를 읽어 들고 처리기에 알립니다. 깨졌거나 서버가 받은 것이면 연결을 끊습니다. */
        void receiveWorldGeneration( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 서버가 보낸 서버 시계를 읽습니다. 다른 월드의 것이면 버리고, 깨졌거나 서버가 받은 것이면 연결을 끊습니다. */
        void receiveWorldClock( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size );
        /** @brief 깨진 라우터 메시지를 보낸 연결을 세고 끊습니다. */
        void refuseMalformed( INetPeers& peers, int32 connectionID, uint8 kind );
        /** @brief 종류 @p kind 를 역할 @p role 쪽이 받을 수 있으면 true 입니다(맡은 처리기의 방향 선언). */
        bool isInboundAllowed( uint8 kind, NetPeerRole role ) const;
        /** @brief 반대 방향 종류를 보낸 연결 @p connectionID 를 세고 끊습니다. 같은 `poll` 의 남은 메시지도 넘기지 않는다. */
        void refuseWrongDirection( INetPeers& peers, int32 connectionID, uint8 kind, NetPeerRole role );
        /** @brief 서버 — 이번 `poll` 에 연결 @p connectionID 가 보낸 종류 @p kind 를 하나 세고, 상한을 넘었으면 true 입니다. 상한이 없는 종류(처리기가 없다)는 세지 않습니다. */
        bool countOverLimit( int32 connectionID, uint8 kind );
        /** @brief 상한을 넘은 메시지를 버립니다 — 비신뢰는 세고, 신뢰는 연결을 끊습니다. */
        void refuseOverLimit( INetPeers& peers, int32 connectionID, uint8 kind, NetChannelKind channel );
        /** @brief 들고 있는 상대 판과 이 쪽 판을 견주어 다른 연결을 끊습니다. 다르면 true 입니다. */
        bool disconnectOnWireVersionMismatch( INetPeers& peers, int32 connectionID );

        /** @brief 상대가 알린 영역 하나의 표 판입니다. */
        struct PeerWireVersion
        {
            uint32 _wireVersion{ 0 };
            uint8  _rangeBase{ 0 };
        };

        /** @brief 연결 하나가 알린 들고 남의 셈입니다(서버). */
        struct PresenceCount
        {
            int64 _windowStart{ 0 };
            int32 _count{ 0 };
        };

        static constexpr uint8 kDirectionToServerBit = 1u << 0;
        static constexpr uint8 kDirectionToClientBit = 1u << 1;

    private:
        vector<INetMessageHandler*>                   _listHandler;    ///< 등록 순서대로 보관한다. 사건만 받는 처리기는 등록마다 하나씩 오르므로(중복 허용) `RegistrationList` 가 아니다.
        vector<INetMessageHandler*>                   _listNewHandler; ///< 지난 `poll` 뒤에 단 처리기 — 이미 들어와 있는 연결을 알려 줄 것
        INetMessageHandler*                           _arrKindHandler[kKindCount];
        INetMessageHandler*                           _arrNotificationHandler[kKindCount]; ///< 종류마다 그 종류를 전달 통지로 보내는 처리기(`getNotifiedKindMask`).
        uint8                                         _arrKindDirection[kKindCount];       ///< 종류마다 맡은 처리기가 적은 방향 비트(`kDirectionToServerBit` · `kDirectionToClientBit`).
        uint16                                        _arrKindMaxPerPoll[kKindCount];      ///< 종류마다 서버 수신 상한(0 이면 세지 않는다)
        uint8                                         _arrKindLimitIndex[kKindCount];      ///< 상한이 있는 종류의 셈 칸 번호
        vector<uint16>                                _listPollCount;                      ///< 이번 `poll` 의 셈([연결 × `_limitedKindCount` + 칸 번호]). `poll` 마다 0 으로 채운다
        int32                                         _limitedKindCount;                   ///< 상한이 있는 종류 수
        NetMessageKindStatsTable                      _kindStats;                          ///< `poll` 가 메시지마다 더한다(꺼내는 반복 안의 덧셈 둘 — 따로 훑지 않는다).
        NetInbound                                    _inbound;                            ///< `poll` 가 다시 쓰는 버퍼.
        DevCounter                                    _malformedCount;
        DevCounter                                    _overLimitCount;      ///< 수신 상한을 넘어 버린 비신뢰 메시지 수
        DevCounter                                    _otherWorldCount;     ///< 앞 월드의 것이라 버린 메시지 수
        DevCounter                                    _deliveryFailedCount; ///< 보류했다가 전하지 못한 메시지 수
        uint64                                        _unclaimedNotificationCount;
        unordered_map<int32, vector<PeerWireVersion>> _mapPeerWireVersion;    ///< 연결마다 상대가 알린 표 판. 연결이 닫히면 지운다
        unordered_map<int32, PresenceCount>           _mapPresenceCount;      ///< 연결마다 들고 남을 알린 수. 연결이 닫히면 지운다
        vector<int32>                                 _listRefusedConnection; ///< 이번 `poll` 에서 판이 달라 끊은 연결 — 남은 메시지를 넘기지 않는다
        vector<HeldMessage>                           _listHeld;              ///< 보류 중인 메시지(온 순서)
        ClockFunction                                 _pClock;                ///< 보류 시한의 시계
        uint32                                        _sessionSerial;         ///< 지금 세션의 번호(`closeSession` 이 올린다)
        NetClock                                      _worldClock;            ///< 이쪽 월드의 서버 시계(세대가 바뀌면 처음부터)
        uint32                                        _bindSerial;            ///< 월드를 올린 횟수(`getBindSerial`)
        uint32                                        _sentClock;             ///< 서버 — 마지막으로 알린 시계(`kClockSendTicks` 단위)
        uint64                                        _worldSceneID;          ///< 서버가 연 월드의 씬(서버는 연 것, 클라이언트는 알림)
        optional<uint64>                              _localSceneID;          ///< 클라이언트: 이쪽에 선 씬(`setLocalScene`). 아직 없으면 비어 있어 문서 없는 월드의 씬 번호(0)와 가른다
        uint8                                         _announcedGeneration;   ///< 서버가 연 월드의 세대(서버는 연 것, 클라이언트는 알림). 0 이면 아직 없다
        uint8                                         _worldGeneration;       ///< 이쪽 월드의 세대. 클라이언트의 씬이 다르면 0
        uint8                                         _bWireVersionsChanged;  ///< 등록이 바뀌었다 — 다음 `poll` 이 상대 모두에게 알리고 들고 있는 상대 판과 견준다
        /** @brief 이 객체가 내준 처리기 등록 구독입니다. 풀리면 `releaseHandler` 가 불린다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        ItemSubscriptionList<INetMessageHandler> _subscriptions{ SW_DELEGATE_METHOD( ItemSubscriptionList<INetMessageHandler>::ReleaseFunction, &NetMessageRouter::releaseHandler, this ) };
    };
} // namespace sw
