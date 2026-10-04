#include "pch.h"

#include "Engine/Object/Component/Audio/AudioListenerComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneAudio.h"

namespace sw
{
    AudioListenerComponent::AudioListenerComponent()
        : _lastPosition{}
        , _bHasLastPosition{ false }
        , _screenHalfWidth{ 10.0f }
        , _listenerIndex{ 0 }
        , _mode{ AudioSpatialMode::World3D }
    {
    }

    void AudioListenerComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getSceneAudio().addListener( this );
    }

    void AudioListenerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSceneAudio().removeListener( this );
        SceneComponent::onUnregister( manager );
    }

    AudioListenerState AudioListenerComponent::makeListenerState() const
    {
        const float4x4     world = getWorldMatrix();
        AudioListenerState state;
        state._position        = world.getTranslation();
        state._forward         = float3::transformVector( float3( 0.0f, 0.0f, 1.0f ), world ).normalize();
        state._up              = float3::transformVector( float3( 0.0f, 1.0f, 0.0f ), world ).normalize();
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
