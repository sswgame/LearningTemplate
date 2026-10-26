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
        , _listSpawned{}
        , _sequenceEventMulticast{}
        , _cameraSignalMulticast{}
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
        // 컷이 덮던 중에 멈추면 시네마틱 끝을 알린다 — 듣는 쪽이 게임 카메라로 섞어 나온다. 멈추기 전의 프레임으로 본다.
        const bool                   bWasPlaying  = _player.isPlaying();
        const SequenceTrackItem*     pCoveringCut = bWasPlaying ? _player.getAsset().findCameraCut( _player.getCurrentFrame() ) : nullptr;
        vector<SequenceCameraSignal> listSignal;
        if ( pCoveringCut != nullptr && _cameraSignalMulticast.isBound() )
        {
            SequenceCameraSignal signal;
            signal._item         = *pCoveringCut;
            signal._blendSeconds = _player.getAsset()._cameraBlendOutSeconds;
            signal._kind         = SequenceCameraSignalKind::CinematicEnd;
            listSignal.push_back( signal );
        }
        _player.stop();
        GameObject* pSpawnOwner = getOwner();
        SequenceTimelineUtil::releaseSpawns( pSpawnOwner != nullptr ? pSpawnOwner->getManager() : nullptr, _listSpawned );
        // 카메라 컷은 컷 구간이 끝나야 해제되므로, 구간 안에서 멈추면 여기서 해제한다(게임 카메라가 원래 규칙으로 돌아간다).
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr )
            SequenceTimelineUtil::releaseCameraCut( pOwner->getManager(), _player.getAsset() );
        broadcastCameraSignals( listSignal );
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

    Subscription SequencePlayerComponent::registerSequenceEvent( const OnSequenceEventDelegate& delegate )
    {
        return _sequenceEventMulticast.add( delegate );
    }

    Subscription SequencePlayerComponent::registerCameraSignal( const OnSequenceCameraSignalDelegate& delegate )
    {
        return _cameraSignalMulticast.add( delegate );
    }

    void SequencePlayerComponent::broadcastCameraSignals( vector<SequenceCameraSignal>& inoutListSignal )
    {
        if ( inoutListSignal.empty() )
            return;
        const float32 secondsPerFrame = _framesPerSecond > 0.0f ? 1.0f / _framesPerSecond : 0.0f;
        for ( SequenceCameraSignal& signal : inoutListSignal )
        {
            signal._secondsUntil    = static_cast<float32>( signal._framesUntil ) * secondsPerFrame;
            signal._durationSeconds = static_cast<float32>( signal._item._end - signal._item._start ) * secondsPerFrame;
        }
        for ( const SequenceCameraSignal& signal : inoutListSignal )
        {
            _cameraSignalMulticast.broadcast( signal );
        }
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
        SequenceTimelineUtil::applySpawns( pManager, _player.getAsset(), _player.getCurrentFrame(), _listSpawned );

        // 카메라 신호는 듣는 쪽이 있을 때만 모은다(신호도 사본이라 핸들러가 시퀀스를 바꿔도 안전하다).
        if ( _cameraSignalMulticast.isBound() )
        {
            vector<SequenceCameraSignal> listSignal;
            SequenceTimelineUtil::collectPlaybackCameraSignals( _player, listSignal );
            broadcastCameraSignals( listSignal );
        }
        if ( listCrossed.empty() )
            return;

        // **사본으로 알린다.** 지난 항목은 플레이어가 가진 에셋의 원소라, 핸들러가 다른 시퀀스를 설정하면(`setSequence`) 남은 포인터가 무효가 된다.
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
