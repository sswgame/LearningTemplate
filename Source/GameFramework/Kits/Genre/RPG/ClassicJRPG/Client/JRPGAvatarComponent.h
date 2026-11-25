/**
 * @file JRPGAvatarComponent.h
 * @brief 클래식 JRPG 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`JRPGGameModeComponent`)의 명령으로 넘기고, 길 · 적을 프리팹 인스턴스(`PrefabViewBatch`)로 그리며 HUD 뷰모델을 채웁니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ClassicJRPG`, 전용 서버는 짓지 않는다). 디렉터의 명령과 읽기 API 만 쓰고 규칙을 직접 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 는 같은 오브젝트의 HUD 컨트롤러(`HUDControllerComponent`)다.
 *          전투 중의 `Move` 축은 고른 적을 바꾸고(한 번 기울일 때 하나), 타이밍 단계에서는 공격 액션이 누름이다. 명령 · 그리기 · HUD 는 틱 뒤 게임 스레드에서 한다.
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
    class GameInputView;
    class GameObjectManager;
    class JRPGGameModeComponent;
    class JRPGBattleHUDViewModel;

    /**
     * @class JRPGAvatarComponent
     * @brief 걷는 동안은 `Move` · 쉬기, 전투 중에는 적 고르기 · 공격 · 주문 · 방어 · 도망을 디렉터 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ClassicJRPG", DisplayName = "JRPG Avatar",
             Tooltip = "Client side of a ClassicJRPG trail: sends commands to the JRPG Game Mode on the same object, draws the trail and the enemies and fills the HUD" )
    class SW_KIT_CLIENT_API JRPGAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        JRPGAvatarComponent();
        virtual ~JRPGAvatarComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HUD · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, JRPGGameModeComponent& director );
        void submitInput( const GameModeInput& input, JRPGGameModeComponent& director );
        void submitExploreInput( JRPGGameModeComponent& director, const GameInputView& input );
        void submitBattleInput( JRPGGameModeComponent& director, const GameInputView& input );
        /** @brief 고른 적을 살아 있는 적으로 맞추고, `Move` 축을 한 번 기울였으면 다음 산 적으로 옮깁니다. */
        void updateTarget( const JRPGGameModeComponent& director, float32 axis );
        /** @brief 명령 멤버가 지금 쓸 수 있는 첫 주문을 명령으로 냅니다(피해는 고른 적, 회복은 HP 가 가장 낮은 멤버, 부활은 쓰러진 멤버). 쓸 주문이 없으면 아무 일도 없습니다. */
        void castFirstSpell( JRPGGameModeComponent& director, int32 memberIndex ) const;
        void updateHUD( const JRPGGameModeComponent& director );
        /** @brief 길 · 파티 · 적을 그립니다. */
        void draw( const JRPGGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the party and cycles the battle target (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that attacks the target (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Spell Action", Tooltip = "InputMap action that casts the first usable spell (required)" )
        hashed_string _spellAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Defend Action", Tooltip = "InputMap action that defends this round (required)" )
        hashed_string _defendAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Flee Action", Tooltip = "InputMap action that flees the battle (required)" )
        hashed_string _fleeAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Rest Action", Tooltip = "InputMap action that rests at the inn (required)" )
        hashed_string _restAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Equip Action", Tooltip = "InputMap action that wears the next gear from the bag (required)" )
        hashed_string _equipAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Class Change Action", Tooltip = "InputMap action that changes the class of a member at the inn (required)" )
        hashed_string _classChangeAction;
        PROPERTY( Category = "Input", DisplayName = "Class Change Member", Tooltip = "Party member whose class the action changes", Min = 0 )
        int32 _classChangeMember;

        PROPERTY( Required, Category = "View", DisplayName = "Body Prefab", Tooltip = "Prefab with one mesh, feet at its origin and one metre tall, spawned for every body the view shows" )
        AssetRef<PrefabAsset> _bodyPrefab;
        PROPERTY( Required, Category = "View", DisplayName = "Marker Prefab", Tooltip = "One-metre cube prefab, spawned for every marker the view shows" )
        AssetRef<PrefabAsset> _markerPrefab;

        SW_TRANSIENT PrefabViewBatch       _bodies;
        SW_TRANSIENT PrefabViewBatch       _markers;
        unique_ptr<JRPGBattleHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다)
        SW_TRANSIENT int32                 _target;       ///< 고른 적
        int32                              _lastStep;     ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 적을 고른다
    };
} // namespace sw
