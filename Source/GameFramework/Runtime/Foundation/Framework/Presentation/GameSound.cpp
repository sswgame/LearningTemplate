#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"

namespace sw
{
    namespace
    {
        struct GameSoundInternal
        {
            /** @brief 등록된 소리 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IGameSoundPlayer*& slot()
            {
                static IGameSoundPlayer* s_pPlayer = nullptr;
                return s_pPlayer;
            }
        };
    } // namespace

    IGameSoundPlayer* IGameSoundPlayer::find()
    {
        return GameSoundInternal::slot();
    }

    IGameSoundPlayer::Registration::Registration( IGameSoundPlayer& player )
    {
        SW_ASSERT( GameSoundInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        GameSoundInternal::slot() = &player;
    }

    bool GameSound::play( const AssetID& assetID, const hashed_string& bus )
    {
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
        return pPlayer != nullptr && pPlayer->play( assetID, bus );
    }

    bool GameSound::playMusic( const AssetID& assetID )
    {
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
        return pPlayer != nullptr && pPlayer->playMusic( assetID );
    }

    bool GameSound::loadEvents( const AssetID& assetID )
    {
        // 창구가 없으면(전용 서버) 할 일이 없다 — 서버는 소리 파일을 읽지 않는다.
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
        return pPlayer == nullptr || pPlayer->loadEvents( assetID );
    }

    void GameSound::unloadEvents( string_view path )
    {
        if ( IGameSoundPlayer* pPlayer = IGameSoundPlayer::find() )
            pPlayer->unloadEvents( path );
    }

    GameSoundPlayingID GameSound::postEvent( const hashed_string& eventName )
    {
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
        return pPlayer != nullptr ? pPlayer->postEvent( eventName ) : 0;
    }

    GameSoundPlayingID GameSound::postEventAt( const hashed_string& eventName, const float3& position )
    {
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
        return pPlayer != nullptr ? pPlayer->postEventAt( eventName, position ) : 0;
    }

    void GameSound::setObjectParameter( const GameObject& object, const hashed_string& name, float32 value )
    {
        IGameSoundPlayer* pPlayer = IGameSoundPlayer::find();
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
