/**
 * @file BattleRoyaleViewComponent.h
 * @brief 배틀로얄 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`BattleRoyaleGameModeComponent`)의 입력 명령으로 넘기고, 사람 · 구역 · 바닥 무더기와 HUD 를 디렉터의 판대로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_BattleRoyale`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getMatch` · `getPersonPosition` 등)만 쓰고
 *          판(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 컨트롤러도 같은 오브젝트에 붙인다.
 *          사람 · 구역은 씬에 놓인 오브젝트(원시 도형)이고 무더기는 프리팹 풀에서 꺼낸다 — 세운 무더기는 상태를 싣기 전에 걷는다(`despawnViews` — .cpp 의 `StatefulComponentRegistrar`).
 *          명령 · 모습은 틱 뒤 게임 스레드에서, `PostUpdate` 라 디렉터 걸음 뒤에 돈다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"

namespace sw
{
    class BattleRoyaleGameModeComponent;
    class BattleRoyaleHUDViewModel;
    class GameObjectManager;

    /**
     * @class BattleRoyaleViewComponent
     * @brief `Move` 축 · 사격 · 쓰기 액션을 디렉터에 넘기고 사람 · 구역 원판 · 무더기 · HUD 를 맞춥니다.
     */
    REFLECT( Category = "BattleRoyale", DisplayName = "Battle Royale View",
             Tooltip = "Client side of a battle royale match: sends input commands to the Battle Royale Game Mode on the same object and shows people, zone, loot and the HUD" )
    class SW_KIT_CLIENT_API BattleRoyaleViewComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        BattleRoyaleViewComponent();
        virtual ~BattleRoyaleViewComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 무더기 풀을 정하고 틱 그룹을 뷰의 `PostUpdate` 로 둡니다. */
        void onBeginPlay() override;
        /** @brief 세운 무더기를 걷습니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 모습을 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

        /** @brief 세운 무더기 모습을 모두 지웁니다(상태를 싣기 전 · 플레이 끝). 다음 틱이 지금 판대로 다시 세웁니다. */
        void despawnViews();

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 모습 · HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager );
        void submitInput( BattleRoyaleGameModeComponent& director ) const;
        void updatePeople( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director ) const;
        void updateZone( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director ) const;
        /** @brief 무더기 목록이 바뀌었으면 모습을 다시 세웁니다. */
        void syncGroundItems( GameObjectManager& manager, const BattleRoyaleGameModeComponent& director );
        void updateHUD( const BattleRoyaleGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks and steers the fall (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Fire Action", Tooltip = "InputMap action held to fire (required)" )
        hashed_string _fireAction;
        PROPERTY( Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action that jumps off the plane, opens the parachute and, held, revives a squad mate (required)" )
        hashed_string _useAction;
        PROPERTY( Category = "Scene", DisplayName = "People", Tooltip = "Objects shown as the people, in person order" )
        vector<GameObjectHandle> _listPersonBody;
        PROPERTY( Category = "Scene", DisplayName = "Zone Disc", Tooltip = "Unit-diameter object scaled to the safe zone" )
        GameObjectHandle _zoneDisc;
        PROPERTY( Category = "Scene", DisplayName = "Next Zone Disc", Tooltip = "Unit-diameter object scaled to the next safe zone" )
        GameObjectHandle _nextZoneDisc;
        PROPERTY( Category = "Look", DisplayName = "Person Height", Min = 0.1, Units = m )
        float32 _personHeight;
        PROPERTY( Category = "Look", DisplayName = "Person Width", Min = 0.1, Units = m )
        float32 _personWidth;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Item Prefab", Tooltip = "View of one item pile on the ground" )
        AssetRef<PrefabAsset> _itemPrefab;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Item Pool Size", Tooltip = "Item views kept switched off for reuse instead of being destroyed", Min = 1 )
        uint32 _itemPoolSize;

        TComponentHandle<BattleRoyaleGameModeComponent> _director;
        unique_ptr<BattleRoyaleHUDViewModel>            _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        PrefabPool                                      _itemPool;
        uint32                                          _shownItemRevision; ///< 세운 무더기 목록의 번호(0 이면 세운 것이 없다 — 디렉터의 번호는 1 부터다)
        uint8                                           _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 넘기지 않는다)
        uint8                                           _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                          _reserved      : 6;
    };
} // namespace sw
