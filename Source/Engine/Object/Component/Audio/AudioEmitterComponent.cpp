#include "pch.h"

#include "Engine/Object/Component/Audio/AudioEmitterComponent.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneAudio.h"

namespace sw
{
    AudioEmitterComponent::AudioEmitterComponent()
        : _lastAudioPosition{}
        , _bHasLastAudioPosition{ false }
        , _event{}
        , _bPlayOnBeginPlay{ true }
        , _bStopOnEndPlay{ true }
        , _bOcclusion{ true }
    {
    }

    void AudioEmitterComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getSceneAudio().addEmitter( this );
    }

    void AudioEmitterComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSceneAudio().removeEmitter( this );
        SceneComponent::onUnregister( manager );
    }

    void AudioEmitterComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        if ( _bPlayOnBeginPlay && _event.empty() == false )
            (void)post( _event );
    }

    void AudioEmitterComponent::onEndPlay()
    {
        if ( _bStopOnEndPlay )
            stopAll( -1.0f );
        SceneComponent::onEndPlay();
    }

    AudioEngine* AudioEmitterComponent::findAudioEngine() const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getManager() == nullptr )
            return nullptr;
        return pOwner->getManager()->getSceneAudio().getAudioEngine();
    }

    AudioPlayingId AudioEmitterComponent::post( const hashed_string& eventName )
    {
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine == nullptr )
            return 0;
        // 자리를 먼저 넣는다 — 씬 오디오가 이번 프레임의 자리를 넣기 전(틱 안)에 낸 소리도 첫 블록부터 제자리에서 난다.
        pEngine->setEmitter( getEmitterId(), computeAudioPosition( getWorldPosition() ), float3{} );
        return pEngine->postEvent( eventName, getEmitterId() );
    }

    void AudioEmitterComponent::stopAll( float32 fadeSeconds )
    {
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine != nullptr )
            pEngine->stopEmitter( getEmitterId(), fadeSeconds );
    }

    void AudioEmitterComponent::setParameter( const hashed_string& name, float32 value )
    {
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine != nullptr )
            pEngine->setEmitterParameter( getEmitterId(), name, value );
    }

    float3 AudioEmitterComponent::computeAudioPosition( const float3& listenerPosition ) const
    {
        (void)listenerPosition;
        return getWorldPosition();
    }
} // namespace sw
