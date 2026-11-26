/**
 * @file ActionCombatAvatarComponent.h
 * @brief 액션 컴뱃 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`ActionCombatGameModeComponent`)의 입력 명령으로 넘기고, 룸 · 플레이어를 프리팹 인스턴스로 보이며 HP 바에 체력을 넣습니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_ActionCombat`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getRoom` · `getPlayerHP` 등)만 쓰고
 *          룸(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다.
 *          명령 · 그리기 · HP 바는 틱 뒤 게임 스레드에서 한다. 이 컴포넌트는 `PostUpdate` 라 디렉터 걸음 뒤에 돌아, 넘긴 입력은 다음 걸음이 쓰고
 *          그리기 · HP 바는 이번 걸음의 결과를 보인다(언리얼 PlayerController 입력 → 다음 폰 이동과 같은 한 프레임).
 *          플레이어 HP 는 씬의 뷰 오브젝트(`_playerView`)의 `RuleHealthViewComponent` 에 스냅숏으로 넣어 HP 바가 같은 체력 계약으로 읽는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoomLook.h"

namespace sw
{
    class ActionCombatGameModeComponent;
    class GameObjectManager;

    /**
     * @class ActionCombatAvatarComponent
     * @brief `Move` 축 · 공격 · 대시 액션을 프레임마다 디렉터 입력 명령으로 넘기고 판을 그립니다.
     */
    REFLECT( Category = "ActionCombat", DisplayName = "Action Combat Avatar",
             Tooltip = "Client side of an ActionCombat run: sends input commands to the Action Combat Game Mode on the same object, draws the room and player and feeds the HP bar" )
    class SW_KIT_CLIENT_API ActionCombatAvatarComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ActionCombatAvatarComponent();
        virtual ~ActionCombatAvatarComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 HP 바 · 그리기를 합니다. */
        void updateAvatar( GameObjectManager& manager, ActionCombatGameModeComponent& director );
        void submitInput( const GameModeInput& input, ActionCombatGameModeComponent& director );
        /** @brief 지금 플레이어 HP 를 뷰 오브젝트의 체력 뷰에 넣습니다. 칸이 비었으면 할 일이 없고, 가리킨 오브젝트에 체력 뷰가 없으면 한 번 오류를 남깁니다. */
        void pushPlayerHealthView( GameObjectManager& manager, const ActionCombatGameModeComponent& director );
        /** @brief 룸과 플레이어를 보입니다. 룸의 자리는 절대 자리다. */
        void showRoom( const ActionCombatGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that walks the player (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action that strikes (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action that dashes (required)" )
        hashed_string _dashAction;
        PROPERTY( Category = "View", DisplayName = "Player View", Tooltip = "Object whose RuleHealthViewComponent shows the player's rule HP (health bar); empty = no bar" )
        GameObjectHandle _playerView;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to the actors, projectiles and the player" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Room Look" )
        ActionRoomLook _roomLook;
        PROPERTY( Category = "View", DisplayName = "Player Radius", Min = 0.01, Units = m )
        float32 _playerRadius;
        PROPERTY( Category = "View", DisplayName = "Player Height", Tooltip = "Height of the player's center above the floor", Units = m )
        float32 _playerHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Invulnerable Color", Tooltip = "Player color while a dash makes it invulnerable" )
        float4 _invulnerableColor;

        SW_TRANSIENT PrefabViewBatch _sphereViews;
        SW_TRANSIENT uint8           _bViewReset         : 1; ///< 이 판에서 HP 바 스냅숏을 연출 없이 다시 두었다
        SW_TRANSIENT uint8           _bPlayerViewMissing : 1; ///< 가리킨 뷰 오브젝트에 체력 뷰가 없다고 이미 알렸다(매 프레임 되풀이하지 않는다)
        [[maybe_unused]] uint8       _reserved           : 6;
    };
} // namespace sw
