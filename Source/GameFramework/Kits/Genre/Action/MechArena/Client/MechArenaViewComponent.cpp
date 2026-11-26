#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/MechArena/Client/MechArenaHUDViewModel.h"
#include "GameFramework/Kits/Genre/Action/MechArena/Component/MechArenaGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

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
    class MechArenaViewComponent : public GameModeViewComponent
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

        SW_TRANSIENT unique_ptr<MechArenaHUDViewModel> _hudViewModel; ///< HUD 가 보는 값(이 컴포넌트가 소유하고 HUD 는 빌려 쓴다 — 먼저 지워지면 HUD 가 스스로 뗀다)
        SW_TRANSIENT PrefabPool                        _projectilePool;
        SW_TRANSIENT vector<GameObjectHandle> _listProjectileView; ///< 꺼낸 탄 모습(판의 탄 목록 순서)
    };

    namespace
    {

        /** @brief 다운된 기체의 높이 비율입니다. */
        constexpr float32 kMechArenaDownHeightRatio = 0.4f;

        /** @brief 액션 누름을 입력 깃발로 바꿉니다. */
        uint8 toMechInputFlag( bool bPressed )
        {
            return bPressed ? SW_TRUE : SW_FALSE;
        }
    } // namespace

    MechArenaViewComponent::MechArenaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MechArenaViewComponent::updateView, &MechArenaViewComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _fireAction{}
        , _meleeAction{}
        , _specialAction{}
        , _dashAction{}
        , _jumpAction{}
        , _lockAction{}
        , _listPilotBody{}
        , _mechHeight{ 2.4f }
        , _projectilePrefab{}
        , _projectilePoolSize{ 0 }
        , _hudViewModel{}
        , _projectilePool{ string{}, 0, SpawnLifetime::Derived }
        , _listProjectileView{}
    {
    }

    MechArenaViewComponent::~MechArenaViewComponent() = default;

    void MechArenaViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _projectilePool = PrefabPool( _projectilePrefab.path(), _projectilePoolSize, SpawnLifetime::Derived );
    }

    void MechArenaViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void MechArenaViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _projectilePool.destroyAll( *pManager );
        _listProjectileView.clear();
    }

    void MechArenaViewComponent::updateView( GameObjectManager& manager, MechArenaGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updatePilotBody( manager, director );
        syncProjectiles( manager, director );
        updateHUD( director );
    }

    void MechArenaViewComponent::submitInput( const GameModeInput& view, MechArenaGameModeComponent& director ) const
    {
        const float2 move = view.getVector2D( _moveAction );
        MechInput    input;
        input._moveX    = move._x;
        input._moveZ    = move._y;
        input._bFire    = toMechInputFlag( view.isActionDown( _fireAction ) );
        input._bMelee   = toMechInputFlag( view.wasActionTriggered( _meleeAction ) );
        input._bSpecial = toMechInputFlag( view.wasActionTriggered( _specialAction ) );
        input._bDash    = toMechInputFlag( view.wasActionTriggered( _dashAction ) );
        input._bJump    = toMechInputFlag( view.wasActionTriggered( _jumpAction ) );
        input._bHover   = toMechInputFlag( view.isActionDown( _jumpAction ) );
        input._bLockOn  = toMechInputFlag( view.wasActionTriggered( _lockAction ) );
        // 판이 끝났거나 덱이 하나면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        const bool bDestroyed = director.getPlayerState() == static_cast<int32>( MechPilotState::Destroyed );
        if ( bDestroyed && input._bSpecial == SW_TRUE )
            (void)director.requestNextMech();
        (void)director.submitPlayerInput( input );
    }

    void MechArenaViewComponent::updatePilotBody( GameObjectManager& manager, const MechArenaGameModeComponent& director ) const
    {
        for ( size_t index = 0; index < _listPilotBody.size(); ++index )
        {
            const int32            pilot   = static_cast<int32>( index );
            GameObject* const      pObject = _listPilotBody[index].isValid() ? manager.resolveGameObject( _listPilotBody[index] ) : nullptr;
            MeshComponent* const   pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            const MechPilot* const pPilot  = director.getWorld().findPilot( pilot );
            if ( pMesh == nullptr )
                continue;
            const bool bShown = pPilot != nullptr && pPilot->getMech() != nullptr && pPilot->_state != MechPilotState::Destroyed && pPilot->_state != MechPilotState::Waiting;
            if ( pMesh->isVisible() != bShown ) // 같은 값이어도 렌더 상태를 다시 짓는다
                pMesh->setVisible( bShown );
            if ( bShown == false )
                continue;
            const float32 height = pPilot->_state == MechPilotState::Down ? _mechHeight * kMechArenaDownHeightRatio : _mechHeight;
            const float32 width  = pPilot->getMech()->_radius * 2.0f;
            pMesh->setLocalScale( float3( width, height, width ) );
            pMesh->setLocalRotation( float3( 0.0f, MathUtil::atan2( pPilot->_forward._x, pPilot->_forward._z ), 0.0f ) );
            pMesh->setWorldPosition( manager.getWorldOrigin().toLocal( director.getPilotPosition( pilot ) + float3( 0.0f, height * 0.5f, 0.0f ) ) );
        }
    }

    void MechArenaViewComponent::syncProjectiles( GameObjectManager& manager, const MechArenaGameModeComponent& director )
    {
        const vector<ProjectileState>& listProjectile = director.getWorld().getProjectiles();
        while ( _listProjectileView.size() > listProjectile.size() )
        {
            _projectilePool.release( manager, _listProjectileView.back() );
            _listProjectileView.pop_back();
        }
        while ( _listProjectileView.size() < listProjectile.size() )
        {
            GameObject* const pObject = _projectilePool.acquire( manager, "MechArenaProjectile" );
            if ( pObject == nullptr )
                return; // 프리팹을 읽지 못했다(오류는 풀이 남긴다) — 탄은 보이지 않아도 난다
            _listProjectileView.push_back( pObject->getHandle() );
        }
        for ( size_t index = 0; index < listProjectile.size(); ++index )
        {
            GameObject* const     pObject = manager.resolveGameObject( _listProjectileView[index] );
            SceneComponent* const pRoot   = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pRoot != nullptr )
                pRoot->setWorldPosition( manager.getWorldOrigin().toLocal( AbsolutePosition{ listProjectile[index]._position } ) );
        }
    }

    void MechArenaViewComponent::updateHUD( const MechArenaGameModeComponent& director )
    {
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MechArenaHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        GameObject* const          pOwner  = getOwner();
        if ( pBinder != nullptr && pOwner != nullptr )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const MechPilot* const pPilot = director.getWorld().findPilot( MechArenaGameModeComponent::kPlayer );
        if ( pPilot == nullptr )
            return;
        const bool bEnded = director.getWorld().isEnded();
        _hudViewModel->setPilot( director.computeHealthRatio( MechArenaGameModeComponent::kPlayer ), 1.0f - pPilot->_boost.getRatio(), director.getPlayerAmmo() ); // 부스트는 열이 차는 과열형이다
        _hudViewModel->setTeamGauge( director.getPlayerGauge(), director.getEnemyGauge() );
        _hudViewModel->setHints( pPilot->_boost.isOverheated(), pPilot->_state == MechPilotState::Destroyed );
        _hudViewModel->setResult( director.hasWon(), bEnded && director.hasWon() == false );
    }
} // namespace sw

#include "MechArenaViewComponent.cpp.gen.inl"
