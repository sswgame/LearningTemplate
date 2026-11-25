/**
 * @file ConquestViewComponent.h
 * @brief 횡스크롤 정복 키트의 클라이언트 쪽 — 입력 맵 액션을 지휘 명령으로 바꿔 디렉터(`ConquestGameModeComponent`)에 내고, 거점 · 병사 · 지휘관을 프리팹으로 보이며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_SideScrollConquest`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitCommanderMove` · `submitOrder` · `submitTrain`)과 읽기 API(`getWorld`)만 쓰고 판(규칙)을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. 명령 · 모습은 틱 뒤 게임 스레드에서 낸다(디렉터 규칙도 틱 뒤에 돈다).
 *          세운 모습은 상태에서 다시 세우는 파생 오브젝트라 저장하지 않는다(`PrefabViewBatch`). 자리는 절대 자리를 원점 기준으로 옮긴다.
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
#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"

namespace sw
{
    class ConquestGameModeComponent;
    class ConquestHUDViewModel;
    class GameObjectManager;

    /**
     * @class ConquestViewComponent
     * @brief `Move` 축의 좌우는 지휘관 이동, 따르기 · 대기 · 돌격 액션은 부대 명령, 훈련 액션은 병사 훈련으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "SideScrollConquest", DisplayName = "Conquest View", Tooltip = "Client side of a side scroll conquest: commands from input, shows sites, soldiers and the commander and fills the HUD" )
    class SW_KIT_CLIENT_API ConquestViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ConquestViewComponent();
        virtual ~ConquestViewComponent() override;

        /** @brief 세운 모습을 지우고 HUD 에서 뷰모델을 뗍니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 내고 모습과 HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, ConquestGameModeComponent& director );
        /** @brief 이번 프레임의 지휘 명령을 디렉터에 냅니다. */
        void submitInput( const GameModeInput& input, ConquestGameModeComponent& director ) const;
        /** @brief 거점 · 병사 · 지휘관을 지금 자리에 보입니다. */
        void showWorld( GameObjectManager& manager, const ConquestGameModeComponent& director );
        /** @brief HUD 뷰모델을 채웁니다. HUD 가 없는 씬이면 값만 듭니다. */
        void          updateHUD( const ConquestGameModeComponent& director );
        const float4& findTeamColor( ConquestTeam team ) const;

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action: left/right moves the commander (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Follow Action", Tooltip = "InputMap action that orders the squad to follow (required)" )
        hashed_string _followAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Hold Action", Tooltip = "InputMap action that orders the squad to hold (required)" )
        hashed_string _holdAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Charge Action", Tooltip = "InputMap action that orders the squad to charge (required)" )
        hashed_string _chargeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Train Action", Tooltip = "InputMap action that trains one soldier (required)" )
        hashed_string _trainAction;
        PROPERTY( Category = "View", DisplayName = "Site Prefab", Required, Tooltip = "Unit piece used for sites, gates and walls" )
        AssetRef<PrefabAsset> _sitePrefab;
        PROPERTY( Category = "View", DisplayName = "Soldier Prefab", Required )
        AssetRef<PrefabAsset> _soldierPrefab;
        PROPERTY( Category = "View", DisplayName = "Commander Prefab", Required )
        AssetRef<PrefabAsset> _commanderPrefab;
        PROPERTY( Category = "View", DisplayName = "Shown Resource", Tooltip = "Resource id the HUD counts" )
        hashed_string _shownResource;
        PROPERTY( Category = "View", DisplayName = "Neutral Color" )
        float4 _neutralColor;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;

        SW_TRANSIENT PrefabViewBatch     _siteViews;
        SW_TRANSIENT PrefabViewBatch     _soldierViews;
        SW_TRANSIENT PrefabViewBatch     _commanderViews;
        unique_ptr<ConquestHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
    };
} // namespace sw
