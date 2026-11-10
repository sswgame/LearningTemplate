#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct KillcamComponentInternal
        {
            /** @brief `killcam.play` — 활성 씬의 킬캠마다 마지막으로 쓰러뜨린 발을 재생합니다(시나리오 · 손 확인). */
            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "killcam.play: no active scene";
                    return true;
                }
                uint32 playedCount  = 0;
                uint32 killcamCount = 0;
                pManager->forEachComponentOfType<KillcamComponent>( [&playedCount, &killcamCount]( KillcamComponent* pKillcam )
                {
                    ++killcamCount;
                    playedCount += pKillcam->playLastKill() ? 1u : 0u;
                } );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "killcam.play played %# of %# killcam(s)", playedCount, killcamCount );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( killcamPlaying, "Killcam.Playing", "1 while the first killcam replays a shot", KillcamComponent, &KillcamComponent::isPlaying );
    SW_AUTOMATION_GETTER_WATCH( killcamShotCount, "Killcam.ShotCount", "Shots the first killcam keeps", KillcamComponent, &KillcamComponent::getShotCount );
    SW_AUTOMATION_GETTER_WATCH( killcamLastHitCount, "Killcam.LastHitCount", "Hits of the most recent recorded shot (2 or more for a piercing shot)", KillcamComponent, &KillcamComponent::getLastHitCount );
    SW_AUTOMATION_GETTER_WATCH( killcamPhase, "Killcam.Phase", "Phase of the first killcam replay: 0 follow, 1 hit hold, 2 done", KillcamComponent, &KillcamComponent::getPhase );
    SW_DEV_COMMAND( KillcamPlay, "killcam.play", "killcam.play", "Replay the last killing shot on every killcam of the active scene", &KillcamComponentInternal::runPlay );
} // namespace sw

namespace sw
{
    KillcamComponent::KillcamComponent()
        : _settings{}
        , _shotCapacity{ 8 }
        , _listShot{}
        , _playing{}
        , _pose{}
        , _elapsed{ 0.0f }
        , _bPlaying{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        _pose._phase = KillcamPhase::Done;
    }

    void KillcamComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<KillcamComponent>( this );
    }

    void KillcamComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<KillcamComponent>( this );
        Component::onUnregister( manager );
    }

    void KillcamComponent::onEndPlay()
    {
        skip();
        Component::onEndPlay();
    }

    void KillcamComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bPlaying == SW_FALSE )
            return;
        _elapsed += MathUtil::max( context.getDeltaTime(), 0.0f );
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
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
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
        pManager->getCameraRegistry().setCutCamera( pCamera );
        SW_LOG_INFO_ID( "Killcam.Play", "Killcam replays a shot with %# hit(s) for %# s", _playing.getHits().size(), KillcamShot::computeDuration( _playing, _settings ) );
        return true;
    }

    void KillcamComponent::skip()
    {
        if ( _bPlaying == SW_FALSE )
            return;
        _bPlaying                   = SW_FALSE;
        _pose._phase                = KillcamPhase::Done;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            pManager->getCameraRegistry().setCutCamera( nullptr );
    }

    uint32 KillcamComponent::getLastHitCount() const
    {
        return _listShot.empty() ? 0u : static_cast<uint32>( _listShot.back().getHits().size() );
    }

    KillcamComponent* KillcamComponent::findInScene( const GameObjectManager& manager )
    {
        const ComponentRegistry::View<KillcamComponent> listKillcam = manager.getComponentRegistry().getAll<KillcamComponent>();
        return listKillcam.empty() ? nullptr : listKillcam[0];
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
