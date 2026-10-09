#include "pch.h"

#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SequencePlayer" );

    SequencePlayerComponent::SequencePlayerComponent()
        : _sequencePath{}
        , _framesPerSecond{ 30.0f }
        , _bLoop{ SW_FALSE }
        , _bAutoPlay{ SW_TRUE }
        , _reserved{ 0 }
        , _player{}
        , _sequenceEventMulticast{}
    {
        setCanEverTick( true );
    }

    void SequencePlayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _player.setFramesPerSecond( _framesPerSecond );
        _player.setLoop( _bLoop == SW_TRUE );
        loadSequenceFromPath();
        if ( _bAutoPlay == SW_TRUE )
            play();
    }

    void SequencePlayerComponent::onPostLoad()
    {
        Component::onPostLoad();
        loadSequenceFromPath();
    }

    void SequencePlayerComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_sequencePathName( "_sequencePath" );
        if ( propertyName == s_sequencePathName )
            loadSequenceFromPath();
    }

    void SequencePlayerComponent::setSequencePath( string_view path )
    {
        _sequencePath = string( path );
        loadSequenceFromPath();
    }

    void SequencePlayerComponent::loadSequenceFromPath()
    {
        if ( _sequencePath.empty() )
            return;
        if ( _player.loadFromFile( _sequencePath ) == false )
            SW_LOG_WARNING( "Sequence '%#' could not be loaded - '%#' plays nothing", _sequencePath, getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
    }

    void SequencePlayerComponent::onEndPlay()
    {
        stop();
        Component::onEndPlay();
    }

    void SequencePlayerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _player.isPlaying() == false )
            return;
        _player.update( deltaTime );
        applyTimeline();
    }

    void SequencePlayerComponent::play()
    {
        _player.setFramesPerSecond( _framesPerSecond );
        _player.setLoop( _bLoop == SW_TRUE );
        _player.play();
        applyTimeline();
    }

    void SequencePlayerComponent::stop()
    {
        _player.stop();
    }

    void SequencePlayerComponent::pause()
    {
        _player.pause();
    }

    void SequencePlayerComponent::resume()
    {
        _player.resume();
    }

    void SequencePlayerComponent::setSequence( const SequenceAsset& asset )
    {
        _player.setAsset( asset );
    }

    DelegateHandle SequencePlayerComponent::registerSequenceEvent( const OnSequenceEventDelegate& delegate )
    {
        return _sequenceEventMulticast.add( delegate );
    }

    void SequencePlayerComponent::unregisterSequenceEvent( DelegateHandle handle )
    {
        _sequenceEventMulticast.remove( handle );
    }

    void SequencePlayerComponent::applyTimeline()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        GameObjectManager* pManager = pOwner->getManager();
        if ( pManager == nullptr )
            return;

        // 루프를 되감은 갱신이면 끝 구간 이벤트까지 본다(`applyPlayback`).
        vector<const SequenceTrackItem*> listCrossed;
        SequenceTimelineUtil::applyPlayback( pManager, _player, &listCrossed );
        if ( listCrossed.empty() )
            return;

        // **사본으로 알린다.** 지난 항목은 플레이어가 든 에셋의 원소라, 핸들러가 다른 시퀀스를 걸면(`setSequence`) 남은 포인터가 죽는다.
        vector<SequenceTrackItem> listEvent;
        listEvent.reserve( listCrossed.size() );
        for ( const SequenceTrackItem* pItem : listCrossed )
        {
            listEvent.push_back( *pItem );
        }
        for ( const SequenceTrackItem& event : listEvent )
        {
            _sequenceEventMulticast.broadcast( event );
        }
    }
} // namespace sw
