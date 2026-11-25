/**
 * @file CardBattleViewComponent.h
 * @brief 카드 게임 키트의 클라이언트 쪽 — 입력 맵 액션으로 손패를 골라 디렉터(`CardBattleGameModeComponent`)에 카드 내기 · 턴 끝 명령을 내고, 손패 · 상대를 프리팹으로 보이며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_CardGame`, 전용 서버는 짓지 않는다). 디렉터의 명령(`playCard` · `endTurn`)과 읽기 API(`getBattle` 등)만 쓰고 전투(규칙)를 직접 고치지 않는다.
 *          고른 자리는 화면 하나의 상태라 판 상태에 싣지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 모습은 틱 뒤 게임 스레드에서 낸다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CardBattleGameModeComponent;
    class CardBattleHUDViewModel;
    class GameObjectManager;

    /**
     * @class CardBattleViewComponent
     * @brief `Move` 축을 좌우로 한 번 기울일 때 고른 카드가 한 장 옮겨 가고, 확정 액션은 카드 내기, 턴 끝 액션은 턴 끝으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "CardGame", DisplayName = "Card Battle View", Tooltip = "Client side of a deck battle: picks hand cards from input, shows the hand and the enemy and fills the HUD" )
    class SW_KIT_CLIENT_API CardBattleViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        CardBattleViewComponent();
        virtual ~CardBattleViewComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

        /** @brief 고른 손패 자리입니다(자동화 감시 값). */
        int32 getSelectedCard() const { return _selectedCard; }

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, CardBattleGameModeComponent& director );
        /** @brief 고르기를 옮기고 확정 · 턴 끝을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, CardBattleGameModeComponent& director );
        /** @brief 손패 · 더미 · 상대를 보입니다. */
        void showBattle( GameObjectManager& manager, const CardBattleGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const CardBattleGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: left/right steps the picked card (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Confirm Action", Tooltip = "InputMap action that plays the picked card (required)" )
        hashed_string _confirmAction;
        PROPERTY( Required, Category = "Input", DisplayName = "End Turn Action", Tooltip = "InputMap action that ends the turn (required)" )
        hashed_string _endTurnAction;
        PROPERTY( Category = "View", DisplayName = "Card Prefab", Required, Tooltip = "Unit piece stretched to a card" )
        AssetRef<PrefabAsset> _cardPrefab;
        PROPERTY( Category = "View", DisplayName = "Enemy Prefab", Required )
        AssetRef<PrefabAsset> _enemyPrefab;
        PROPERTY( Category = "View", DisplayName = "Playable Color" )
        float4 _playableColor;
        PROPERTY( Category = "View", DisplayName = "Disabled Color" )
        float4 _disabledColor;
        PROPERTY( Category = "View", DisplayName = "Picked Color" )
        float4 _pickedColor;
        PROPERTY( Category = "View", DisplayName = "Pile Color" )
        float4 _pileColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;

        PrefabViewBatch                    _cardViews;
        PrefabViewBatch                    _enemyViews;
        unique_ptr<CardBattleHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        int32                              _selectedCard; ///< 고른 손패 자리
        int32                              _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 장으로 옮긴다
    };
} // namespace sw
