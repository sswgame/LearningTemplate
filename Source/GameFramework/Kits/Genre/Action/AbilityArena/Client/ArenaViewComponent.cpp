#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaViewComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaProjectileComponent.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 상태를 싣기 전에 세운 투사체 모습을 걷습니다 — 씬 섹션에 실리면 복원한 뒤 다시 세운 것과 겹친다. */
        const StatefulComponentRegistrar<ArenaViewComponent> s_viewRegistration{ "ArenaViewComponent" };
    } // namespace

    ArenaViewComponent::ArenaViewComponent()
        : _projectilePrefab{}
        , _projectilePoolSize{ 0 }
        , _playerTint{ 0.55f, 0.75f, 1.0f, 1.0f }
        , _gruntTint{ 1.0f, 0.5f, 0.45f, 1.0f }
        , _casterTint{ 0.8f, 0.55f, 1.0f, 1.0f }
        , _projectileTint{ 1.0f, 0.6f, 0.1f, 1.0f }
        , _director{}
        , _tintCache{}
        , _projectilePool{ string{}, 0 }
        , _nextProjectileID{ 0 }
    {
        setCanEverTick( true );
    }

    ArenaViewComponent::~ArenaViewComponent() = default;

    void ArenaViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        const GameObject* const             pOwner    = getOwner();
        const ArenaGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ArenaGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "ArenaView.NoGameMode", "Arena view on '%#': no ArenaGameModeComponent in front of it on the same object - no projectile is shown",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ArenaGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ArenaGameModeComponent>( pDirector );
    }

    void ArenaViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _projectilePool = PrefabPool( _projectilePrefab.path(), _projectilePoolSize );
    }

    void ArenaViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void ArenaViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        context.deferPostTickOnComponent( *this, []( ArenaViewComponent& view, GameObjectManager& manager )
        { view.updateViews( manager ); } );
    }

    void ArenaViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _projectilePool.destroyAll( *pManager );
        _nextProjectileID = 0;
    }

    void ArenaViewComponent::updateViews( GameObjectManager& manager )
    {
        const ArenaGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        tintUnits( manager, *pDirector );
        syncProjectiles( manager, *pDirector );
    }

    void ArenaViewComponent::tintUnits( GameObjectManager& manager, const ArenaGameModeComponent& director )
    {
        for ( const ArenaUnit& unit : director.getUnits() )
        {
            GameObject* const    pBody = manager.resolveGameObject( unit._object );
            MeshComponent* const pMesh = pBody != nullptr ? pBody->getComponent<MeshComponent>() : nullptr;
            if ( pMesh != nullptr )
                (void)_tintCache.apply( *pMesh, findTint( unit._kind ) ); // 아직 머티리얼이 없으면 다음 프레임에 다시 건다
        }
    }

    void ArenaViewComponent::syncProjectiles( GameObjectManager& manager, const ArenaGameModeComponent& director )
    {
        // 다 쓴 모습은 틱에서 스스로 숨는다(`ArenaProjectileComponent::isFinished`). 끄기는 구조 변경이라 여기(틱 밖)서 풀에 돌려주고, 새 탄이 다시 쓴다.
        _projectilePool.releaseBusyIf( manager, []( const GameObject& object )
        {
            const ArenaProjectileComponent* pView = object.getComponent<ArenaProjectileComponent>();
            return pView == nullptr || pView->isFinished();
        } );
        uint32 nextProjectileID = _nextProjectileID;
        for ( const ProjectileState& projectile : director.getProjectiles() )
        {
            if ( projectile._id < _nextProjectileID )
                continue;
            nextProjectileID = MathUtil::max( nextProjectileID, projectile._id + 1 );
            showProjectile( manager, director, projectile );
        }
        _nextProjectileID = nextProjectileID;
    }

    void ArenaViewComponent::showProjectile( GameObjectManager& manager, const ArenaGameModeComponent& director, const ProjectileState& projectile )
    {
        GameObject* const               pObject = _projectilePool.acquire( manager, "ArenaProjectile" );
        ArenaProjectileComponent* const pView   = pObject != nullptr ? pObject->getComponent<ArenaProjectileComponent>() : nullptr;
        if ( pView == nullptr )
        {
            if ( pObject != nullptr )
            {
                SW_LOG_ERROR_ID( "ArenaView.ProjectilePrefabNoView", "[Arena] projectile prefab '%#' has no ArenaProjectileComponent - the shot flies unseen", _projectilePrefab.c_str() );
                _projectilePool.destroyAll( manager );
            }
            return;
        }
        MeshComponent* const pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
            if ( _tintCache.apply( *pMesh, _projectileTint ) == false )
                SW_LOG_ERROR_ONCE_ID( "ArenaView.ProjectileWithoutMaterial", "[Arena]: prefab '%#' has a mesh without a material - it keeps the prefab colour", _projectilePrefab.c_str() );
        pView->bind( director.getOwner()->getHandle(), projectile._id, projectile._position );
    }

    const float4& ArenaViewComponent::findTint( ArenaUnitKind kind ) const
    {
        switch ( kind )
        {
            case ArenaUnitKind::Player:
                return _playerTint;
            case ArenaUnitKind::Grunt:
                return _gruntTint;
            case ArenaUnitKind::Caster:
                return _casterTint;
        }
        return _playerTint; // 열거자는 셋뿐이다(covered-switch) — 닿지 않는다
    }
} // namespace sw
