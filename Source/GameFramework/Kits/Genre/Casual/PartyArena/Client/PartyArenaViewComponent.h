/**
 * @file PartyArenaViewComponent.h
 * @brief 파티 아레나 키트의 클라이언트 쪽 — 입력 맵 액션을 조작 명령으로 바꿔 디렉터(`PartyGameModeComponent`)에 내고, 무대 · 사람 · 아이템을 프리팹으로 보이며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_PartyArena`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getArena` · `getSeries`)만 쓰고 무대(규칙)를 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 모습은 틱 뒤 게임 스레드에서 낸다(디렉터 규칙도 틱 뒤에 돈다).
 *          세운 모습은 상태에서 다시 세우는 파생 오브젝트라 저장하지 않는다(`PrefabViewBatch`). 자리는 절대 자리를 원점 기준으로 옮긴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class PartyGameModeComponent;
    class PartyHUDViewModel;

    /**
     * @class PartyArenaViewComponent
     * @brief `Move` 축은 공중 이동, 점프 · 공격 · 찍기 액션은 눌린 순간을 디렉터에 넘깁니다.
     */
    REFLECT( Category = "PartyArena", DisplayName = "Party Arena View", Tooltip = "Client side of a party arena: moves the local player from input, shows the arena, players and items and fills the HUD" )
    class SW_KIT_CLIENT_API PartyArenaViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        PartyArenaViewComponent();
        virtual ~PartyArenaViewComponent() override;

        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, PartyGameModeComponent& director );
        /** @brief 이번 프레임의 조작 입력을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, PartyGameModeComponent& director ) const;
        /** @brief 무대 · 사람 · 아이템을 지금 자리에 보입니다. */
        void showArena( GameObjectManager& manager, const PartyGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void updateHUD( const PartyGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that moves the player in the air (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action timed with the landing to bounce higher (required)" )
        hashed_string _jumpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that dashes into a rival (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Pound Action", Tooltip = "InputMap action that pounds the trampoline (required)" )
        hashed_string _poundAction;
        PROPERTY( Category = "View", DisplayName = "Floor Prefab", Required, Tooltip = "Unit disc stretched to the arena radius" )
        AssetRef<PrefabAsset> _floorPrefab;
        PROPERTY( Category = "View", DisplayName = "Player Prefab", Required )
        AssetRef<PrefabAsset> _playerPrefab;
        PROPERTY( Category = "View", DisplayName = "Item Prefab", Required )
        AssetRef<PrefabAsset> _itemPrefab;
        PROPERTY( Category = "View", DisplayName = "Floor Color" )
        float4 _floorColor;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Opponent Color" )
        float4 _opponentColor;
        PROPERTY( Category = "View", DisplayName = "Item Color" )
        float4 _itemColor;

        SW_TRANSIENT PrefabViewBatch _floorViews;
        SW_TRANSIENT PrefabViewBatch _playerViews;
        SW_TRANSIENT PrefabViewBatch _itemViews;
        SW_TRANSIENT unique_ptr<PartyHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };
} // namespace sw
