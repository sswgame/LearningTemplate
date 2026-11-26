/**
 * @file MetroidvaniaViewComponent.h
 * @brief 메트로배니아 키트의 클라이언트 쪽 — 입력 맵 액션을 디렉터(`MetroidvaniaGameModeComponent`)의 입력 명령으로 넘기고, 몸 · 적 · 줍는 것 · 시체와 HUD 를 디렉터의 판대로 보입니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Metroidvania`, 전용 서버는 짓지 않는다). 디렉터의 명령(`submitPlayerInput`)과 읽기 API(`getMotor` · `getEnemies` 등)만 쓰고
 *          판(규칙)을 직접 고치지 않는다. 디렉터는 같은 오브젝트에 먼저 붙인다 — 등록(`onRegister`)에서 찾아 핸들로 든다. HUD 컨트롤러도 같은 오브젝트에 붙인다.
 *          보이는 것은 모두 씬에 놓인 오브젝트(원시 도형)다 — 자리 · 크기 · 보임만 바꾼다. 명령 · 모습은 틱 뒤 게임 스레드에서, `PostUpdate` 라 디렉터 걸음 뒤에 돈다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class MetroidvaniaGameModeComponent;
    class MetroidvaniaHUDViewModel;

    /**
     * @class MetroidvaniaViewComponent
     * @brief `Move` 축과 액션을 디렉터에 넘기고 몸 · 적 · 줍는 것 · 시체 · HUD 를 맞춥니다.
     */
    REFLECT( Category = "Metroidvania", DisplayName = "Metroidvania View",
             Tooltip = "Client side of a metroidvania game: sends input commands to the Metroidvania Game Mode on the same object and shows the player, enemies, pickups, the corpse and the HUD" )
    class SW_KIT_CLIENT_API MetroidvaniaViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        MetroidvaniaViewComponent();
        virtual ~MetroidvaniaViewComponent() override;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 명령으로 넘기고 모습 · HUD 를 맞춥니다. */
        void updateView( GameObjectManager& manager, MetroidvaniaGameModeComponent& director );
        void submitInput( const GameModeInput& input, MetroidvaniaGameModeComponent& director ) const;
        void updateScene( GameObjectManager& manager, const MetroidvaniaGameModeComponent& director ) const;
        /** @brief 오브젝트 하나를 보이거나 숨기고, 보이면 평면 자리 @p position 을 가운데로 @p size 로 둡니다. */
        static void placeBody( GameObjectManager& manager, GameObjectHandle body, bool bShown, const float2& position, const float3& size );
        void        updateHUD( const MetroidvaniaGameModeComponent& director );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that runs and climbs (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Jump Action", Tooltip = "InputMap action of the jump, higher while held (required)" )
        hashed_string _jumpAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Dash Action", Tooltip = "InputMap action of the dash (required)" )
        hashed_string _dashAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Attack Action", Tooltip = "InputMap action of the attack (required)" )
        hashed_string _attackAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Guard Action", Tooltip = "InputMap action that parries when pressed and guards while held (required)" )
        hashed_string _guardAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Use Action", Tooltip = "InputMap action that uses a site (required)" )
        hashed_string _useAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Flask Action", Tooltip = "InputMap action that drinks a flask (required)" )
        hashed_string _flaskAction;
        PROPERTY( Category = "Scene", DisplayName = "Player Body", Tooltip = "Object shown as the player" )
        GameObjectHandle _playerBody;
        PROPERTY( Category = "Scene", DisplayName = "Corpse Body", Tooltip = "Object shown where the dropped currency lies" )
        GameObjectHandle _corpseBody;
        PROPERTY( Category = "Scene", DisplayName = "Enemy Bodies", Tooltip = "Objects shown as the enemies, in enemy order" )
        vector<GameObjectHandle> _listEnemyBody;
        PROPERTY( Category = "Scene", DisplayName = "Pickup Bodies", Tooltip = "Objects shown as the pickups, in pickup order" )
        vector<GameObjectHandle> _listPickupBody;
        PROPERTY( Category = "Look", DisplayName = "Enemy Size", Units = m )
        float3 _enemySize;
        PROPERTY( Category = "Look", DisplayName = "Startup Scale", Tooltip = "Enemy height multiplier while it winds up an attack (the parry cue)", Min = 0.1 )
        float32 _startupScale;
        PROPERTY( Category = "Look", DisplayName = "Marker Size", Tooltip = "Size of pickups and of the corpse", Units = m )
        float3 _markerSize;

        SW_TRANSIENT unique_ptr<MetroidvaniaHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
    };
} // namespace sw
