#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    KillcamComponent::KillcamComponent()
        : _settings{}
        , _shotCapacity{ 8 }
        , _listShot{}
        , _playing{}
        , _pose{}
        , _elapsed{ 0.0f }
        , _cameraCut{}
        , _bPlaying{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        _pose._phase = KillcamPhase::Done;
    }

    void KillcamComponent::onEndPlay()
    {
        skip();
        Component::onEndPlay();
    }

    void KillcamComponent::onWorldOriginShifted( const float3& delta )
    {
        for ( KillcamShot& shot : _listShot )
        {
            shot.applyWorldOffset( delta );
        }
        _playing.applyWorldOffset( delta );
        _pose._position += delta;
        _pose._lookAt += delta;
        _pose._bullet += delta;
    }

    void KillcamComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bPlaying == SW_FALSE )
            return;
        _elapsed += MathUtil::max( context.getDeltaTime().count(), 0.0f );
        _pose = KillcamShot::sample( _playing, _settings, _elapsed );
        applyPose();
        if ( _pose._phase == KillcamPhase::Done )
            skip();
    }

    void KillcamComponent::recordShot( const KillcamShot& shot )
    {
        if ( _listShot.size() >= MathUtil::max( _shotCapacity, 1u ) )
            _listShot.erase( _listShot.begin() ); // 고정 용량 — 가장 오래된 발을 버린다(용량은 작다)
        _listShot.push_back( shot );
    }

    bool KillcamComponent::playLastKill()
    {
        for ( auto it = _listShot.rbegin(); it != _listShot.rend(); ++it )
        {
            if ( it->hasKill() )
                return play( *it );
        }
        return false;
    }

    bool KillcamComponent::play( const KillcamShot& shot )
    {
        GameObject*        pOwner   = getOwner();
        CameraComponent*   pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pCamera == nullptr || pManager == nullptr )
        {
            SW_LOG_ERROR_ID( "Killcam.NoCamera", "Killcam on '%#' has no CameraComponent next to it - it cannot replay", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return false;
        }
        _playing  = shot;
        _elapsed  = 0.0f;
        _bPlaying = SW_TRUE;
        _pose     = KillcamShot::sample( _playing, _settings, 0.0f );
        applyPose();
        pManager->getCameraRegistry().setCutCamera( _cameraCut, pCamera );
        SW_LOG_INFO_ID( "Killcam.Play", "Killcam replays a shot with %# hit(s) for %# s", _playing.getHits().size(), KillcamShot::computeDuration( _playing, _settings ) );
        return true;
    }

    void KillcamComponent::skip()
    {
        if ( _bPlaying == SW_FALSE )
            return;
        _bPlaying                         = SW_FALSE;
        _pose._phase                      = KillcamPhase::Done;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        // 틱(재생 끝)에서도 부른다 — 씬의 컷 자리는 명령 버퍼로 틱 뒤에 놓는다.
        if ( pManager != nullptr )
            pManager->executeOrDeferPostTickOnComponent( *this, []( KillcamComponent& self )
            { self._cameraCut.reset(); } );
    }

    uint32 KillcamComponent::getLastHitCount() const
    {
        return _listShot.empty() ? 0u : static_cast<uint32>( _listShot.back().getHits().size() );
    }

    KillcamComponent* KillcamComponent::findInScene( const GameObjectManager& manager )
    {
        return manager.getComponentRegistry().findUnique<KillcamComponent>(); // 씬에 하나 — 둘이면 오류로 기록하지 않는다(등록 순서로 고르지 않는다)
    }

    void KillcamComponent::applyPose()
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
            return;
        const float3 forward = ( _pose._lookAt - _pose._position ).normalizeOr( _playing.getDirection(), 1e-5f );
        pCamera->setWorldTransform( float4x4::makeWorld( _pose._position, forward, float3::Up ) );
    }
} // namespace sw
