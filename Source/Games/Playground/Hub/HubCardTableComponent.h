/**
 * @file HubCardTableComponent.h
 * @brief 접속한 허브의 카드 탁자입니다 — 두 사람이 셰딩 카드 게임 한 판을 턴 중계(`GF_NetTurnRelay`)로 둡니다.
 * @details 서버는 행동의 순서와 규칙만 본다: 방마다 받은 행동을 처음부터 다시 돌려(`HubCardTurnPolicy::replay` — 판은 씨앗이 정해 결정적이다) 다음 행동이 규칙에 맞는지 정하고, 맞는 것에만
 *          번호를 붙여 두 자리에 보낸다. 방이 찼으면 다음 방으로 간다. 판이 끝나고 두 자리가 떠나면 중계가 방을 되돌려 다음 판이 쓴다 — 섞기는 방의 세대로 갈라 같은 방의 다음 판도 다른 판이다.
 *          서버는 접속 티켓(계정)을 요구하는 세션에서만 선다(`NetLinkValue::_bTicketRequired`) — 허브 서버와 같은 문턱이다.
 *          **패는 서버만 안다.** 서버가 운영체제 난수에서 만든 비밀 키로 섞고, 자리마다 자기 손패 · 낼 수 있는 패 · 상대 장수만 담은 모습(`HubCardSeatView`)을 보낸다. 클라이언트에는 키도
 *          씨앗도 남의 패도 없어 판을 다시 돌릴 수 없다. 낼 수 있는 패도 서버가 골라 보낸다(하스스톤의 선택지 — 규칙을 클라이언트에 두 벌 두지 않는다).
 *          개발 명령 `hub.cards` 와 감시 값 `Hub.Cards*`, HUD(`hub_cards.ui.xml` — 같은 오브젝트의 HUD 컨트롤러)가 시나리오의 손과 눈이다(`automation/netpair/hub_walk_*`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetTurnRelay/TurnRelay.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/SheddingCard/SheddingCardGame.h"

namespace sw
{
    struct NetLinkValue;

    class GameObjectManager;
    class HubCardHUDViewModel;

    /** @brief 자리 하나가 보는 판입니다(서버 → 그 자리, 고정 배치형). 자기 손패만 있고 상대는 장수뿐이다. */
    REFLECT()
    struct HubCardSeatView
    {
        REFLECT_BODY();

        PROPERTY( MaxCount = SheddingCardGame::kDeckCardCount )
        vector<uint16> _listHandCardID{};
        PROPERTY( MaxCount = SheddingCardGame::kDeckCardCount )
        vector<uint16> _listPlayableCardID{}; ///< 지금 낼 수 있는 패(내 차례가 아니면 비었다)
        PROPERTY()
        uint16 _topCardID{ 0 }; ///< 버린 더미의 맨 위
        PROPERTY( Min = 0, Max = 108 )
        int32 _opponentCardCount{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        int32 _currentSeat{ 0 };
        PROPERTY( Min = -1, Max = 1 )
        int32 _winner{ constant::invalid_index::kInt32 };
        PROPERTY()
        SheddingCardColor _color{ SheddingCardColor::Red }; ///< 지금 색(와일드가 정한 색 포함)
    };

    /** @class HubCardTurnPolicy @brief 카드 탁자의 차례 규칙입니다(서버) — 방의 행동을 다시 돌려 판정하므로 비밀 키 말고는 상태가 없습니다. */
    class HubCardTurnPolicy final : public ITurnPolicy
    {
    public:
        static constexpr int32 kSeatCount = 2;

        HubCardTurnPolicy();

        /** @brief 섞기의 비밀 키를 정합니다(서버가 세션을 세울 때마다 새 키). */
        void setShuffleKey( const SipHashKey& key ) { _shuffleKey = key; }

        /** @brief 방 @p room 의 행동을 처음부터 돌려 @p outGame 에 판을 만듭니다(섞기는 방의 세대로). 규칙 밖 행동이 있으면 false 입니다. */
        [[nodiscard]] bool replay( const TurnRoom& room, SheddingCardGame& outGame ) const;

        bool isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override;
        void applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const override;
        bool isRoomFinished( const TurnRoom& room ) const override;
        bool writeSeatView( const TurnRoom& room, int32 seat, vector<uint8>& outView ) const override;
        /** @brief 서버가 보낸 모습을 읽습니다. 깨졌거나 덱에 없는 카드가 있으면 false 이고 @p outView 는 그대로입니다. */
        [[nodiscard]] static bool readSeatView( const vector<uint8>& bytes, HubCardSeatView& outView );

    private:
        SipHashKey _shuffleKey;
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
        SW_REGISTERED_COMPONENT( HubCardTableComponent );

        static constexpr uint32 kFirstRoomID       = 1;
        static constexpr int32  kMaxActionsPerRoom = 512; ///< 한 판의 행동 상한(판정이 행동을 다시 돌리는 비용의 상한이기도 하다)
        static constexpr uint32 kMaxRoomID         = 64;  ///< 찬 방을 건너 찾는 끝 — 넘으면 자리가 없다고 알린다

        HubCardTableComponent();
        ~HubCardTableComponent() override;

        /** @brief @p manager 의 월드에서 접속한(클라이언트) 카드 탁자를 찾습니다. 없거나 여럿이면 nullptr 입니다. */
        [[nodiscard]] static HubCardTableComponent* findClientTable( GameObjectManager& manager );

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 빈 방에 앉습니다(클라이언트). 끝난 판에 앉아 있으면 떠나고 새 판에 앉습니다. 접속하지 않았거나 두는 판에 앉아 있으면 false 입니다. */
        [[nodiscard]] bool requestSeat();
        /** @brief 앉은 탁자를 떠납니다. 앉지 않았으면 false 입니다. */
        [[nodiscard]] bool requestLeave();
        /** @brief 내 손의 낼 수 있는 첫 패를 내거나, 없거나 @p bDrawOnly 면 뽑습니다. 내 차례가 아니어도 보낸다 — 거절은 서버가 한다. 판이 없으면 false 입니다. */
        [[nodiscard]] bool requestPlay( bool bDrawOnly );

        bool isClient() const { return _role == Role::Client; }
        /** @brief 앉은 자리입니다(0 · 1). 앉지 않았으면 -1 입니다. */
        int32 getSeat() const { return _role == Role::Client ? _client.getSeat() : constant::invalid_index::kInt32; }
        int32 getStartedValue() const { return _role == Role::Client && _client.isStarted() ? 1 : 0; }
        int32 getMyTurnValue() const;
        int32 getHandCount() const { return static_cast<int32>( _view._listHandCardID.size() ); }
        int32 getOpponentCardCount() const { return _view._opponentCardCount; }
        int32 getActionCount() const { return static_cast<int32>( _client.getActions().size() ); }
        /** @brief HUD 화면에 걸려 탁자 값이 보이면 1 입니다. */
        int32                  getHUDShownValue() const { return _bHUDShown != SW_FALSE ? 1 : 0; }
        const HubCardSeatView& getView() const { return _view; }
        int32                  getRejectedCount() const { return _rejectedCount; }

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
        /** @brief 탁자에서 일어났습니다 — 마지막 모습을 버리고 다음에는 첫 방부터 빈 자리를 찾습니다. */
        void clearTable();
        /** @brief 받은 모습을 HUD 뷰모델에 넣습니다. 전용 서버 · 헤드리스에는 HUD 창구가 없어 보일 곳이 없다. */
        void updateHUD();

    private:
        TurnRelayServer                 _server;
        TurnRelayClient                 _client;
        HubCardTurnPolicy               _policy;
        Subscription                    _routerSubscription; ///< 라우터에 단 `_server` 또는 `_client` 의 등록(처리기보다 뒤에 선언 — 먼저 풀린다)
        vector<TurnRelayEvent>          _listEventScratch;
        HubCardSeatView                 _view; ///< 클라이언트 — 서버가 마지막으로 보낸 내 자리의 모습
        unique_ptr<HubCardHUDViewModel> _hudViewModel;
        uint32                          _roomID;
        int32                           _rejectedCount;
        Role                            _role;
        uint8                           _bStartTried; ///< 걸린 링크로 세워 봤다(실패해도 다시 하지 않는다)
        uint8                           _bHUDShown;   ///< HUD 에 뷰모델이 걸렸고 탁자가 보인다
    };
} // namespace sw
