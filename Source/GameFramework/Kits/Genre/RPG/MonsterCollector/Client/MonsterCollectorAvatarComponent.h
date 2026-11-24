/**
 * @file MonsterCollectorAvatarComponent.h
 * @brief 몬스터 수집 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`MonsterCollectorGameModeComponent`)의 명령으로 넘기고, 풀밭 · 상대를 프리팹 인스턴스(`PrefabViewBatch`)로 그리며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_MonsterCollector`, 전용 서버는 짓지 않는다). 디렉터의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 는 같은 오브젝트의 HUD 컨트롤러(`HUDControllerComponent`)다.
 *          전투 중의 `Move` 축은 고른 기술을 바꾼다(한 번 기울일 때 하나). 명령 · 그리기 · HUD 는 틱 뒤 게임 스레드에서 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameInputView;
    class GameObjectManager;
    class MonsterCollectorGameModeComponent;
    class MonsterCollectorHUDViewModel;

    /**
     * @class MonsterCollectorAvatarComponent
     * @brief 걷는 동안은 `Move` · 쉬기, 전투 중에는 기술 고르기 · 기술 · 볼 · 교체 · 도망을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "MonsterCollector", DisplayName = "Monster Collector Avatar",
             Tooltip = "Client side of a MonsterCollector outing: sends commands to the Monster Collector Game Mode on the same object, draws the glade and the foe and fills the HUD" )
    class SW_KIT_CLIENT_API MonsterCollectorAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        MonsterCollectorAvatarComponent();
        virtual ~MonsterCollectorAvatarComponent() override;

        /** @brief 같은 오브젝트의 디렉터를 찾아 핸들로 듭니다. 없으면 오류를 남기고 아무것도 하지 않습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 액션 이름이 비었으면 오류를 남깁니다(폴백 없음). 틱 그룹은 뷰와 같은 `PostUpdate` 입니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;
        /** @brief 입력 · 명령 · 그리기를 틱 뒤 게임 스레드로 미룹니다. */
        void onTick( TickContext& context ) override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager );
        void submitExploreInput( MonsterCollectorGameModeComponent& director, const GameInputView& input );
        void submitBattleInput( MonsterCollectorGameModeComponent& director, const GameInputView& input );
        /** @brief 고른 기술을 나선 개체의 빈 슬롯이 아닌 것으로 맞추고, `Move` 축을 한 번 기울였으면 다음 기술로 옮깁니다. */
        void updateMoveSlot( const MonsterCollectorGameModeComponent& director, float32 axis );
        void updateHUD( const MonsterCollectorGameModeComponent& director );
        /** @brief 쉼터 · 플레이어 · 상대를 그립니다. */
        void draw( const MonsterCollectorGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player and cycles the battle move (required)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that uses the chosen move (required)" )
        hashed_string _attackAction;
        PROPERTY( Category = "Input", DisplayName = "Ball Action", Tooltip = "InputMap action that throws a ball (required)" )
        hashed_string _ballAction;
        PROPERTY( Category = "Input", DisplayName = "Switch Action", Tooltip = "InputMap action that brings in the next usable monster (required)" )
        hashed_string _switchAction;
        PROPERTY( Category = "Input", DisplayName = "Run Action", Tooltip = "InputMap action that runs from the battle (required)" )
        hashed_string _runAction;
        PROPERTY( Category = "Input", DisplayName = "Rest Action", Tooltip = "InputMap action that rests at the camp (required)" )
        hashed_string _restAction;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        TComponentHandle<MonsterCollectorGameModeComponent> _director;
        PrefabViewBatch                                     _bodies;
        PrefabViewBatch                                     _markers;
        unique_ptr<MonsterCollectorHUDViewModel>            _hudViewModel;      ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        int32                                               _moveSlot;          ///< 고른 기술 슬롯
        int32                                               _lastStep;          ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 기술을 고른다
        uint8                                               _bInputLive    : 1; ///< 이번 틱의 시간이 흘렀다(멈춤 중에는 입력을 읽지 않고 그리기만 한다)
        uint8                                               _bActionsValid : 1; ///< 액션 이름이 모두 있다(`onBeginPlay`)
        [[maybe_unused]] uint8                              _reserved      : 6;
    };
} // namespace sw
