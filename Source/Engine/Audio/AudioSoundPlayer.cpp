#include "pch.h"

#include "Engine/Audio/AudioSoundPlayer.h"

#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    AudioSoundPlayer::AudioSoundPlayer( IAudioService& audio )
        : _audio{ audio }
        , _nextOneShotEmitter{ 0 }
    {
    }

    bool AudioSoundPlayer::play( const AssetID& assetID, const hashed_string& bus )
    {
        return _audio.isInitialized() && _audio.play( assetID, bus );
    }

    bool AudioSoundPlayer::playMusic( const AssetID& assetID )
    {
        return _audio.isInitialized() && _audio.playMusic( assetID );
    }

    bool AudioSoundPlayer::loadEvents( const AssetID& assetID )
    {
        AudioEngine* pEngine = findEngine();
        if ( pEngine == nullptr )
            return true; // 서지 않은 서비스(헤드리스 하네스)는 낼 소리가 없다
        AudioEventLibrary library;
        if ( library.loadFromAsset( assetID ) == false )
            return false;
        return pEngine->loadEventLibrary( hashed_string( assetID.view() ), library );
    }

    void AudioSoundPlayer::unloadEvents( string_view path )
    {
        AudioEngine* pEngine = findEngine();
        if ( pEngine != nullptr )
            pEngine->unloadEventLibrary( hashed_string( path ) );
    }

    SoundPlayingID AudioSoundPlayer::postEvent( const hashed_string& eventName )
    {
        AudioEngine* pEngine = findEngine();
        return pEngine != nullptr ? pEngine->postEvent( eventName, 0 ) : 0;
    }

    SoundPlayingID AudioSoundPlayer::postEventAt( const hashed_string& eventName, const float3& position )
    {
        AudioEngine* pEngine = findEngine();
        if ( pEngine == nullptr )
            return 0;
        // 한 번 쓰는 에미터 — 자리를 두고 내고 바로 지운다. 엔진은 그 자리의 소리가 끝날 때까지 자리를 남긴다.
        const AudioEmitterID emitterID = kOneShotEmitterBase + _nextOneShotEmitter.fetch_add( 1 );
        pEngine->setEmitter( emitterID, position, float3{} );
        const AudioPlayingID playingID = pEngine->postEvent( eventName, emitterID );
        pEngine->removeEmitter( emitterID );
        return playingID;
    }

    SoundPlayingID AudioSoundPlayer::postEventOn( GameObject& target, const hashed_string& eventName )
    {
        AudioEmitterComponent* pEmitter = target.getComponent<AudioEmitterComponent>();
        if ( pEmitter == nullptr )
        {
            SW_LOG_ERROR_ID( "AudioSoundPlayer.NoEmitter", "Sound '%#' on '%#' - the object has no AudioEmitterComponent", eventName.c_str(), target.getName().c_str() );
            return 0;
        }
        return pEmitter->post( eventName );
    }

    void AudioSoundPlayer::setObjectParameter( const GameObject& object, const hashed_string& name, float32 value )
    {
        object.forEachComponentOfType<AudioEmitterComponent>( [&name, value]( AudioEmitterComponent* pEmitter )
        { pEmitter->setParameter( name, value ); } );
    }

    void AudioSoundPlayer::stop( SoundPlayingID playingID, float32 fadeSeconds )
    {
        AudioEngine* pEngine = findEngine();
        if ( pEngine != nullptr && playingID != 0 )
            pEngine->stop( playingID, fadeSeconds );
    }

    AudioEngine* AudioSoundPlayer::findEngine() const
    {
        return _audio.isInitialized() ? &_audio.getEngine() : nullptr;
    }
} // namespace sw
