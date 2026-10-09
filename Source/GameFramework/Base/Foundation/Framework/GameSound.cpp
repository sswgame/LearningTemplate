#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/GameSound.h"

#include "Core/Concurrency/atomic.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/IAudioSystem.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameSoundInternal
        {
            /** @brief 한 번 쓰는 에미터 id 의 시작입니다 — 컴포넌트 id(작은 수부터 센다)와 겹치지 않는 높은 자리. */
            static constexpr AudioEmitterId kOneShotEmitterBase = AudioEmitterId{ 1 } << 62;

            /** @brief 오디오 엔진입니다. 서비스가 없거나 내려가 있으면 nullptr 입니다. */
            static AudioEngine* findEngine()
            {
                IAudioSystem* pAudio = game::getService<IAudioSystem>();
                return pAudio != nullptr && pAudio->isInitialized() ? &pAudio->getEngine() : nullptr;
            }

            inline static atomic<uint64> s_nextOneShotEmitter{ 0 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool GameSound::play( string_view path, const hashed_string& bus )
    {
        IAudioSystem* pAudio = game::getService<IAudioSystem>();
        return pAudio != nullptr && pAudio->play( path, bus );
    }

    bool GameSound::playMusic( string_view path )
    {
        IAudioSystem* pAudio = game::getService<IAudioSystem>();
        return pAudio != nullptr && pAudio->playMusic( path );
    }

    bool GameSound::loadEvents( string_view path )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        if ( pEngine == nullptr )
            return true;
        AudioEventLibrary library;
        if ( library.loadFromResource( path ) == false )
            return false;
        return pEngine->loadEventLibrary( hashed_string( path ), library );
    }

    void GameSound::unloadEvents( string_view path )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        if ( pEngine != nullptr )
            pEngine->unloadEventLibrary( hashed_string( path ) );
    }

    AudioPlayingId GameSound::postEvent( const hashed_string& eventName )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        return pEngine != nullptr ? pEngine->postEvent( eventName, 0 ) : 0;
    }

    AudioPlayingId GameSound::postEventAt( const hashed_string& eventName, const float3& position )
    {
        AudioEngine* pEngine = GameSoundInternal::findEngine();
        if ( pEngine == nullptr )
            return 0;
        // 한 번 쓰는 에미터 — 자리를 두고 내고 바로 지운다. 엔진은 그 자리의 소리가 끝날 때까지 자리를 남긴다.
        const AudioEmitterId emitterId = GameSoundInternal::kOneShotEmitterBase + GameSoundInternal::s_nextOneShotEmitter.fetch_add( 1 );
        pEngine->setEmitter( emitterId, position, float3{} );
        const AudioPlayingId playingId = pEngine->postEvent( eventName, emitterId );
        pEngine->removeEmitter( emitterId );
        return playingId;
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
                    (void)GameSound::play( entry._pName );
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
