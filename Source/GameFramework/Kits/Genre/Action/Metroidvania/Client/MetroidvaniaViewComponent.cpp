#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Client/MetroidvaniaHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Component/MetroidvaniaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

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
    class MetroidvaniaViewComponent : public GameModeViewComponent
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

    namespace
    {
        /** @brief 액션 누름을 입력 깃발로 바꿉니다. */
        uint8 toMetroidvaniaInputFlag( bool bPressed )
        {
            return bPressed ? SW_TRUE : SW_FALSE;
        }
    } // namespace

    MetroidvaniaViewComponent::MetroidvaniaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MetroidvaniaViewComponent::updateView, &MetroidvaniaViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _jumpAction{}
        , _dashAction{}
        , _attackAction{}
        , _guardAction{}
        , _useAction{}
        , _flaskAction{}
        , _playerBody{}
        , _corpseBody{}
        , _listEnemyBody{}
        , _listPickupBody{}
        , _enemySize{ 0.9f, 1.0f, 0.9f }
        , _startupScale{ 1.4f }
        , _markerSize{ 0.5f, 0.5f, 0.5f }
        , _hudViewModel{}
    {
    }

    MetroidvaniaViewComponent::~MetroidvaniaViewComponent() = default;

    void MetroidvaniaViewComponent::updateView( GameObjectManager& manager, MetroidvaniaGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateScene( manager, director );
        updateHUD( director );
    }

    void MetroidvaniaViewComponent::submitInput( const GameModeInput& input, MetroidvaniaGameModeComponent& director ) const
    {
        MetroidvaniaPlayerCommand command;
        command._move           = input.getVector2D( _moveAction );
        command._bJumpHeld      = toMetroidvaniaInputFlag( input.isActionDown( _jumpAction ) );
        command._bJumpPressed   = toMetroidvaniaInputFlag( input.wasActionPressed( _jumpAction ) );
        command._bDashPressed   = toMetroidvaniaInputFlag( input.wasActionTriggered( _dashAction ) );
        command._bAttackPressed = toMetroidvaniaInputFlag( input.wasActionTriggered( _attackAction ) );
        command._bGuardHeld     = toMetroidvaniaInputFlag( input.isActionDown( _guardAction ) );
        command._bParryPressed  = toMetroidvaniaInputFlag( input.wasActionPressed( _guardAction ) );
        command._bUsePressed    = toMetroidvaniaInputFlag( input.wasActionTriggered( _useAction ) );
        command._bFlaskPressed  = toMetroidvaniaInputFlag( input.wasActionTriggered( _flaskAction ) );
        // 이겼으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( command );
    }

    void MetroidvaniaViewComponent::placeBody( GameObjectManager& manager, GameObjectHandle body, bool bShown, const float2& position, const float3& size )
    {
        GameObject* const    pObject = body.isValid() ? manager.resolveGameObject( body ) : nullptr;
        MeshComponent* const pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        if ( pMesh->isVisible() != bShown ) // 같은 값이어도 렌더 상태를 다시 짓는다
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalScale( size );
        pMesh->setWorldPosition( manager.getWorldOrigin().toLocal( MetroidvaniaGameModeComponent::toAbsolute( position ) ) );
    }

    void MetroidvaniaViewComponent::updateScene( GameObjectManager& manager, const MetroidvaniaGameModeComponent& director ) const
    {
        const float2 halfExtents = director.getMotor().getSettings()._halfExtents;
        const bool   bAlive      = director.getExplorePhase() != MetroidvaniaPhase::Dead;
        placeBody( manager, _playerBody, bAlive, director.getMotor().getPosition(), float3( halfExtents._x * 2.0f, halfExtents._y * 2.0f, halfExtents._x * 2.0f ) );
        const MetroidvaniaCorpse& corpse = director.getDeathRest().getCorpse();
        placeBody( manager, _corpseBody, corpse._bActive == SW_TRUE, corpse._position, _markerSize );
        const vector<MetroidvaniaEnemy>& listEnemy = director.getEnemies();
        for ( size_t index = 0; index < _listEnemyBody.size() && index < listEnemy.size(); ++index )
        {
            const float32 heightScale = listEnemy[index]._startup.isActive() ? _startupScale : 1.0f;
            placeBody( manager, _listEnemyBody[index], director.isEnemyAlive( index ), listEnemy[index]._position, float3( _enemySize._x, _enemySize._y * heightScale, _enemySize._z ) );
        }
        const vector<float2>& listPickupPosition = director.getPickupPositions();
        for ( size_t index = 0; index < _listPickupBody.size() && index < listPickupPosition.size(); ++index )
        {
            placeBody( manager, _listPickupBody[index], director.isPickupLeft( index ), listPickupPosition[index], _markerSize );
        }
    }

    void MetroidvaniaViewComponent::updateHUD( const MetroidvaniaGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MetroidvaniaHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const HealthStateMachine& health = director.getPlayer().getHealthMachine();
        _hudViewModel->setVitals( health.getHealth() / health.getSettings()._maxHealth, director.getPlayer().getStamina().getRatio(), director.getDeathRest().getFlaskCharges() );
        _hudViewModel->setProgress( director.getCurrency(), director.getMapState().computeCompletionPercent() );
        _hudViewModel->setResult( director.getExplorePhase() == MetroidvaniaPhase::Dead, director.getExplorePhase() == MetroidvaniaPhase::Won );
    }
} // namespace sw

#include "MetroidvaniaViewComponent.cpp.gen.inl"
