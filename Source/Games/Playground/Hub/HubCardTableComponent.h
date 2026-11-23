/**
 * @file HubCardTableComponent.h
 * @brief 접속한 허브의 카드 탁자입니다 — 두 사람이 셰딩 카드 게임 한 판을 턴 중계(`GF_NetTurnRelay`)로 둡니다.
 * @details 서버는 행동의 순서와 규칙만 본다: 방마다 받은 행동을 처음부터 다시 돌려(`HubCardTurnPolicy::replay` — 판은 씨앗이 정해 결정적이다) 다음 행동이 규칙에 맞는지 정하고, 맞는 것에만
 *          번호를 붙여 두 자리에 보낸다. 클라이언트는 같은 행동을 같은 씨앗으로 돌려 판을 보인다. 방이 찼으면 다음 방으로 간다.
 *          서버는 접속 티켓(계정)을 요구하는 세션에서만 선다(`NetLinkValue::_bTicketRequired`) — 허브 서버와 같은 문턱이다.
 *          shortcut: 손패를 숨기지 않는다(씨앗이 방 번호다 — 두 자리가 서로의 패를 계산할 수 있다). 숨긴 패의 게임은 서버가 나눠 자리별 메시지로 보낸다(`TurnRelay.h` 머리말).
 *          개발 명령 `hub.cards` 와 감시 값 `Hub.Cards*` 가 시나리오의 손이다(`automation/netpair/hub_walk_*`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetTurnRelay/TurnRelay.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/SheddingCard/SheddingCardGame.h"

namespace sw
{
    struct NetLinkValue;

    /** @class HubCardTurnPolicy @brief 카드 탁자의 차례 규칙입니다 — 방의 행동을 다시 돌려 판정하므로 상태가 없습니다. */
    class HubCardTurnPolicy final : public ITurnPolicy
    {
    public:
        static constexpr int32 kSeatCount = 2;

        /** @brief 방 @p roomID 의 행동 @p listAction 을 처음부터 돌려 @p outGame 에 판을 만듭니다. 규칙 밖 행동이 있으면 false 입니다. */
        [[nodiscard]] static bool replay( uint32 roomID, const vector<TurnAction>& listAction, SheddingCardGame& outGame );

        bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override;
        void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override;
    };

    /**
     * @class HubCardTableComponent
     * @brief 카드 탁자 하나의 중계(서버)와 자리(클라이언트)입니다.
     */
    REFLECT( Category = "Hub", DisplayName = "Hub Card Table", Tooltip = "Two connected players play a shedding card game; the server relays and checks their turns" )
    class HubCardTableComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kFirstRoomID       = 1;
        static constexpr int32  kMaxActionsPerRoom = 512; ///< 한 판의 행동 상한(판정이 행동을 다시 돌리는 비용의 상한이기도 하다)
        static constexpr uint32 kMaxRoomID         = 64;  ///< 찬 방을 건너 찾는 끝 — 넘으면 자리가 없다고 알린다

        HubCardTableComponent();

        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 빈 방에 앉습니다(클라이언트). 접속하지 않았거나 이미 앉았으면 false 입니다. */
        [[nodiscard]] bool requestSeat();
        /** @brief 내 손의 낼 수 있는 첫 패를 내거나, 없거나 @p bDrawOnly 면 뽑습니다. 내 차례가 아니어도 보낸다 — 거절은 서버가 한다. 판이 없으면 false 입니다. */
        [[nodiscard]] bool requestPlay( bool bDrawOnly );

        bool isClient() const { return _role == Role::Client; }
        /** @brief 앉은 자리입니다(0 · 1). 앉지 않았으면 -1 입니다. */
        int32 getSeat() const { return _role == Role::Client ? _client.getSeat() : constant::invalid_index::kInt32; }
        int32 getStartedValue() const { return _role == Role::Client && _client.isStarted() ? 1 : 0; }
        int32 getMyTurnValue() const;
        int32 getActionCount() const { return static_cast<int32>( _client.getActions().size() ); }
        int32 getRejectedCount() const { return _rejectedCount; }

    private:
        enum class Role : uint8
        {
            None = 0,
            Server,
            Client
        };

        void startRole( const NetLinkValue& link );
        void stopRole();
        void pollClientEvents();

    private:
        TurnRelayServer        _server;
        TurnRelayClient        _client;
        HubCardTurnPolicy      _policy;
        Subscription           _routerSubscription; ///< 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        vector<TurnRelayEvent> _listEventScratch;
        uint32                 _roomID;
        int32                  _rejectedCount;
        Role                   _role;
        uint8                  _bStartTried; ///< 걸린 링크로 세워 봤다(실패해도 다시 하지 않는다)
    };
} // namespace sw
