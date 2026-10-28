#include "pch.h"

#include "Engine/Audio/Component/AudioListenerComponent.h"

#include "Engine/Audio/Component/SceneAudio.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    AudioListenerComponent::AudioListenerComponent()
        : _lastState{}
        , _seenTransformVersion{ 0 }
        , _bHasLastState{ false }
        , _screenHalfWidth{ 10.0f }
        , _listenerIndex{ 0 }
        , _mode{ AudioSpatialMode::World3D }
        , _sceneAudioSubscription{}
    {
    }

    void AudioListenerComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _sceneAudioSubscription = manager.getSubsystem<sw::SceneAudio>().addListener( this );
    }

    void AudioListenerComponent::onUnregister( GameObjectManager& manager )
    {
        _sceneAudioSubscription.reset();
        SceneComponent::onUnregister( manager );
    }

    AudioListenerState AudioListenerComponent::makeListenerState( bool bPoseUnchanged ) const
    {
        AudioListenerState state = _lastState;
        if ( bPoseUnchanged == false )
        {
            const float4x4 world = getWorldMatrix();
            state._position      = world.getTranslation();
            state._forward       = float3::transformVector( float3( 0.0f, 0.0f, 1.0f ), world ).normalize();
            state._up            = float3::transformVector( float3( 0.0f, 1.0f, 0.0f ), world ).normalize();
        }
        state._velocity        = float3{};
        state._mode            = _mode;
        state._screenHalfWidth = _screenHalfWidth;
        state._bActive         = true;
        return state;
    }

    void AudioListenerComponent::setMode( AudioSpatialMode mode, float32 screenHalfWidth )
    {
        _mode            = mode;
        _screenHalfWidth = screenHalfWidth;
    }
} // namespace sw
