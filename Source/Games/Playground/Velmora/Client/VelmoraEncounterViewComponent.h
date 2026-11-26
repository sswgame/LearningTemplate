/**
 * @file VelmoraEncounterViewComponent.h
 * @brief 벨모라 지역 전투의 클라이언트 쪽 — 공격 · 대시 액션을 전투(`VelmoraEncounterGameModeComponent`)의 입력 명령으로 넘기고 룸(적 · 탄 · 무적)을 프리팹 인스턴스로 보입니다.
 * @details 클라이언트 전용 소스다(`Client/` — 전용 서버는 짓지 않는다). 전투 컴포넌트는 같은 오브젝트에 먼저 붙인다. 걷기 · 쓰기 입력과 지역 · 플레이어 그리기는 키트의 `AdventureAvatarComponent` 다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoomLook.h"

namespace sw
{
    class GameObjectManager;
    class VelmoraEncounterGameModeComponent;

    /**
     * @class VelmoraEncounterViewComponent
     * @brief 공격 · 대시 누름을 프레임마다 전투 입력 명령으로 넘기고 열린 룸을 그립니다.
     */
    REFLECT( Category = "Velmora", DisplayName = "Velmora Encounter View",
             Tooltip = "Client side of the Velmora region fights: sends attack and dash to the Velmora Encounter Game Mode on the same object and draws its room" )
    class VelmoraEncounterViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        VelmoraEncounterViewComponent();
        virtual ~VelmoraEncounterViewComponent() override;

        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 룸을 그립니다. */
        void updateView( GameObjectManager& manager, VelmoraEncounterGameModeComponent& director );
        void submitInput( const GameModeInput& input, VelmoraEncounterGameModeComponent& director );
        /** @brief 싸움이 열려 있으면 룸의 적 · 탄과 무적인 플레이어를 감싸는 공을 보입니다. 룸의 자리는 절대 자리다. */
        void showRoom( const VelmoraEncounterGameModeComponent& encounter, GameObjectManager& manager );

    private:
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that strikes (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that dashes (required)" )
        hashed_string _dashAction;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to the actors, projectiles and the invulnerable player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Room Look" )
        ActionRoomLook _roomLook;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Radius", Tooltip = "Sphere around the invulnerable player; a little larger than the player sphere of the kit", Min = 0.01, Units = m )
        float32 _invulnerableRadius;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Height", Tooltip = "Height of that sphere's center above the floor", Units = m )
        float32 _invulnerableHeight;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Color" )
        float4 _invulnerableColor;

        SW_TRANSIENT PrefabViewBatch _sphereViews;
    };
} // namespace sw
