#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"

#include "Core/Concurrency/atomic.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/IAudioService.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameSoundInternal
        {
            /** @brief 한 번 쓰는 에미터 id 의 시작입니다 — 컴포넌트 id(작은 수부터 센다)와 겹치지 않는 높은 자리. */
            static constexpr AudioEmitterID kOneShotEmitterBase = AudioEmitterID{ 1 } << 62;

            /** @brief 오디오 엔진입니다. 서비스가 없거나 내려가 있으면 nullptr 입니다. */
            static AudioEngine* findEngine()
            {
                IAudioService* pAudio = game::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->isInitialized() ? &pAudio->getEngine() : nullptr;
            }

            inline static atomic<uint64> s_nextOneShotEmitter{ 0 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameSound::play( const ResourceID& resourceID, const hashed_string& bus )
    {
        IAudioService* pAudio = game::findSubsystem<IAudioService>();
        return pAudio != nullptr && pAudio->play( resourceID, bus );
    }

    bool GameSound::playMusic( const ResourceID& resourceID )
    {
        IAudioService* pAudio = game::findSubsystem<IAudioService>();
        return pAudio != nullptr && pAudio->playMusic( resourceID );
    }

    bool GameSound::loadEvents( const ResourceID& resourceID )
    {
        const string_view path    = resourceID.view();
        AudioEngine*      pEngine = GameSoundInternal::findEngine();
        if ( pEngine == nullptr )
            return true;
        AudioEventLibrary library;
        if ( library.loadFromResource( resourceID ) == false )
            return false;
        return pEngine->loadEventLibrary( hashed_string( path ), library );
    }

    void GameSound::unloadEvents( string_view path )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        if ( pEngine != nullptr )
            pEngine->unloadEventLibrary( hashed_string( path ) );
    }

    AudioPlayingID GameSound::postEvent( const hashed_string& eventName )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        return pEngine != nullptr ? pEngine->postEvent( eventName, 0 ) : 0;
    }

    AudioPlayingID GameSound::postEventAt( const hashed_string& eventName, const float3& position )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        if ( pEngine == nullptr )
            return 0;
        // 한 번 쓰는 에미터 — 자리를 두고 내고 바로 지운다. 엔진은 그 자리의 소리가 끝날 때까지 자리를 남긴다.
        const AudioEmitterID emitterID = GameSoundInternal::kOneShotEmitterBase + GameSoundInternal::s_nextOneShotEmitter.fetch_add( 1 );
        pEngine->setEmitter( emitterID, position, float3{} );
        const AudioPlayingID playingID = pEngine->postEvent( eventName, emitterID );
        pEngine->removeEmitter( emitterID );
        return playingID;
    }
} // namespace sw

namespace sw
{
    GameSoundQueue::GameSoundQueue()
        : _listEntry{}
    {
    }

    void GameSoundQueue::queueClip( const utf8* pPath )
    {
        Entry entry;
        entry._pName = pPath;
        entry._kind  = Kind::Clip;
        _listEntry.push_back( entry );
    }

    void GameSoundQueue::queueEvent( const utf8* pEventName )
    {
        Entry entry;
        entry._pName = pEventName;
        entry._kind  = Kind::Event;
        _listEntry.push_back( entry );
    }

    void GameSoundQueue::queueEventAt( const utf8* pEventName, const float3& position )
    {
        Entry entry;
        entry._position = position;
        entry._pName    = pEventName;
        entry._kind     = Kind::EventAt;
        _listEntry.push_back( entry );
    }

    void GameSoundQueue::playAll()
    {
        for ( const Entry& entry : _listEntry )
        {
            switch ( entry._kind )
            {
                case Kind::Clip:
                {
                    (void)GameSound::play( ResourceID{ entry._pName } );
                    break;
                }
                case Kind::Event:
                {
                    (void)GameSound::postEvent( hashed_string( entry._pName ) );
                    break;
                }
                case Kind::EventAt:
                {
                    (void)GameSound::postEventAt( hashed_string( entry._pName ), entry._position );
                    break;
                }
            }
        }
        _listEntry.clear();
    }
} // namespace sw
