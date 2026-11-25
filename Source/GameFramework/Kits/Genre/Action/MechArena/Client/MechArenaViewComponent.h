/**
 * @file MechArenaViewComponent.h
 * @brief 기체 대전 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`MechArenaGameModeComponent`)의 입력 명령으로 넘기고, 기체 · 탄과 HUD 를 디렉터의 판대로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_MechArena`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput` · `requestNextMech`)과 읽기 API(`getWorld` · `getPilotPosition` 등)만 쓰고
 *          판(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 컨트롤러도 같은 오브젝트에 붙인다.
 *          기체는 씬에 놓인 오브젝트(원시 도형)이고 탄은 프리팹 풀에서 꺼낸다 — 세운 탄은 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
 *          명령 · 모습은 틱 뒤 게임 스레드에서, `PostUpdate` 라 디렉터 걸음 뒤에 돈다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"

namespace sw
{
    class GameObjectManager;
    class MechArenaGameModeComponent;
    class MechArenaHUDViewModel;

    /**
     * @class MechArenaViewComponent
     * @brief `Move` 축과 액션을 디렉터에 넘기고 기체 · 탄 · HUD 를 맞춥니다. 점프 액션을 누른 채면 부유합니다.
     */
    REFLECT( Category = "MechArena", DisplayName = "Mech Arena View",
             Tooltip = "Client side of a mech arena match: sends input commands to the Mech Arena Game Mode on the same object and shows mechs, projectiles and the HUD" )
    class SW_KIT_CLIENT_API MechArenaViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        MechArenaViewComponent();
        virtual ~MechArenaViewComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 탄을 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 모습 · HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, MechArenaGameModeComponent& director );
        void submitInput( const GameModeInput& input, MechArenaGameModeComponent& director ) const;
        void updatePilotBody( GameObjectManager& manager, const MechArenaGameModeComponent& director ) const;
        /** @brief 탄 수만큼 모습을 꺼내고(넘치면 돌려주고) 탄 자리에 둡니다. */
        void syncProjectiles( GameObjectManager& manager, const MechArenaGameModeComponent& director );
        void updateHUD( const MechArenaGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that moves the mech (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Fire Action", Tooltip = "InputMap action held to fire the main weapon (required)" )
        hashed_string _fireAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Melee Action", Tooltip = "InputMap action of the melee combo (required)" )
        hashed_string _meleeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Special Action", Tooltip = "InputMap action of the special weapon; while destroyed it picks the next mech of the deck (required)" )
        hashed_string _specialAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that boosts (required)" )
        hashed_string _dashAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action that jumps and, held, hovers (required)" )
        hashed_string _jumpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Lock Action", Tooltip = "InputMap action that locks on or picks the next target (required)" )
        hashed_string _lockAction;
        PROPERTY( Category = "Scene", DisplayName = "Pilots", Tooltip = "Objects shown as the mechs, in pilot order (player, allies, enemies)" )
        vector<GameObjectHandle> _listPilotBody;
        PROPERTY( Category = "Look", DisplayName = "Mech Height", Min = 0.1, Units = m )
        float32 _mechHeight;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Projectile Prefab", Tooltip = "View of one projectile" )
        AssetRef<PrefabAsset> _projectilePrefab;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Projectile Pool Size", Tooltip = "Projectile views kept switched off for reuse instead of being destroyed", Min = 1 )
        uint32 _projectilePoolSize;

        unique_ptr<MechArenaHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        PrefabPool                        _projectilePool;
        vector<GameObjectHandle>          _listProjectileView; ///< 꺼낸 탄 모습(판의 탄 목록 순서)
    };
} // namespace sw
