#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    bool GameSound::play( const AssetID& assetID, const hashed_string& bus )
    {
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        return pPlayer != nullptr && pPlayer->play( assetID, bus );
    }

    bool GameSound::playMusic( const AssetID& assetID )
    {
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        return pPlayer != nullptr && pPlayer->playMusic( assetID );
    }

    bool GameSound::loadEvents( const AssetID& assetID )
    {
        // 창구가 없으면(전용 서버 — 오디오를 짓지 않는다) 할 일이 없다. 서버는 소리 파일을 읽지 않는다.
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        return pPlayer == nullptr || pPlayer->loadEvents( assetID );
    }

    void GameSound::unloadEvents( string_view path )
    {
        if ( ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>() )
            pPlayer->unloadEvents( path );
    }

    SoundPlayingID GameSound::postEvent( const hashed_string& eventName )
    {
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        return pPlayer != nullptr ? pPlayer->postEvent( eventName ) : 0;
    }

    SoundPlayingID GameSound::postEventAt( const hashed_string& eventName, const float3& position )
    {
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        return pPlayer != nullptr ? pPlayer->postEventAt( eventName, position ) : 0;
    }

    void GameSound::setObjectParameter( const GameObject& object, const hashed_string& name, float32 value )
    {
        ISoundPlayer* pPlayer = game::findSubsystem<ISoundPlayer>();
        if ( pPlayer != nullptr )
            pPlayer->setObjectParameter( object, name, value );
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
                    (void)GameSound::play( AssetID{ entry._pName } );
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
