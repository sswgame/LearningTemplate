#include "pch.h"

#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    namespace
    {
        struct SequencePlayerComponentInternal
        {
            static const SequencePlayerComponent* findTargetPlayer( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*              pTarget = query.findTargetObject();
                const SequencePlayerComponent* pPlayer = pTarget != nullptr ? pTarget->getComponent<SequencePlayerComponent>() : nullptr;
                if ( pPlayer == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "sequence player" ) );
                return pPlayer;
            }

            /** @brief 감시 값 `Sequence.Frame` 입니다 — 대상 플레이어의 지금 프레임입니다. */
            [[nodiscard]] static bool readFrame( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlayer->getCurrentFrame() ) );
                return true;
            }

            /** @brief 감시 값 `Sequence.History` 입니다 — 대상 플레이어의 기록에서 종류 key(`Event` · `CameraSignal` · `Finished` ...)의 줄 수입니다. */
            [[nodiscard]] static bool readHistory( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                for ( uint8 kindIndex = 0; kindIndex <= static_cast<uint8>( SequenceHistoryKind::Span ); ++kindIndex )
                {
                    const SequenceHistoryKind kind = static_cast<SequenceHistoryKind>( kindIndex );
                    if ( query._key == getEnumName( kind ) )
                    {
                        outValue.setNumber( static_cast<float64>( pPlayer->getHistory().countKind( kind ) ) );
                        return true;
                    }
                }
                outValue.setText( "key must be Started, Finished, Stopped, Event, CameraSignal, UnboundBinding or Span" );
                return false;
            }

            /** @brief 감시 값 `Sequence.BoundObject` 입니다 — 바인딩 id key 가 묶인 오브젝트의 이름이고, 묶이지 않았으면 `none` 입니다. */
            [[nodiscard]] static bool readBoundObject( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                int32 bindingID = 0;
                if ( StringUtil::parseInt( query._key, bindingID ) == false || bindingID <= 0 )
                {
                    outValue.setText( "key must be a binding id (1 or more)" );
                    return false;
                }
                const GameObject* pObject = pPlayer->findBoundObject( static_cast<uint32>( bindingID ) );
                outValue.setText( pObject != nullptr ? pObject->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( sequenceFrame, "Sequence.Frame", "Current frame of the target's sequence player", &SequencePlayerComponentInternal::readFrame, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceHistory, "Sequence.History", "Lines of one kind (key: Started, Finished, Stopped, Event, CameraSignal, UnboundBinding, Span) in the target's recent sequence history",
                               &SequencePlayerComponentInternal::readHistory, true, true );
    SW_AUTOMATION_QUERY_WATCH( sequenceBoundObject, "Sequence.BoundObject", "Name of the object the target's sequence player binds to binding id key; none when unbound",
                               &SequencePlayerComponentInternal::readBoundObject, true, true );
} // namespace sw

namespace sw
{
    SequencePlayerComponent::SequencePlayerComponent()
        : _sequencePath{}
        , _listBinding{}
        , _framesPerSecond{ 30.0f }
        , _bLoop{ SW_FALSE }
        , _bAutoPlay{ SW_TRUE }
        , _reserved{ 0 }
        , _player{}
        , _state{}
        , _history{}
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
        // 쥔 것(스폰 · 구간)은 지금 시퀀스의 항목 번호로 기억했다 — 시퀀스를 바꾸기 전에 거둔다.
        endPlayback( SequenceHistoryKind::Stopped );
        if ( _player.loadFromAsset( AssetID{ _sequencePath } ) == false )
            SW_LOG_WARNING_ID( "SequencePlayer.SequenceCouldNotLoaded", "Sequence '%#' could not be loaded - '%#' plays nothing", _sequencePath.c_str(), getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
    }

    void SequencePlayerComponent::onEndPlay()
    {
        stop();
        Component::onEndPlay();
    }

    void SequencePlayerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _player.isPlaying() == false )
            return;
        _player.update( context.getDeltaTime() );
        applyTimeline();
        // 루프가 아니면 끝 프레임을 적용한 갱신에서 플레이어가 멈춘다 — 그때 끝낸다.
        if ( _player.isPlaying() == false )
            endPlayback( SequenceHistoryKind::Finished );
    }

    void SequencePlayerComponent::play()
    {
        // 다시 틀기는 지금 재생을 끝내고 시작한다 — 되돌릴 값은 이번 재생의 시작 상태여야 한다.
        endPlayback( SequenceHistoryKind::Stopped );
        if ( checkBindings() == false )
            return;
        _player.setFramesPerSecond( _framesPerSecond );
        _player.setLoop( _bLoop == SW_TRUE );
        GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && _player.getAsset()._whenFinished == SequenceFinishPolicy::RestoreState )
        {
            SequenceTimelineUtil::captureRestoreState( pOwner->getManager(), _player.getAsset(), makeBindingView(), _state._listRestore );
            _state._bRestoreCaptured = SW_TRUE;
        }
        _player.play();
        _history.record( SequenceHistoryKind::Started, _player.getCurrentFrame(), kSequenceNoBinding, _sequencePath.path() );
        applyTimeline();
    }

    void SequencePlayerComponent::stop()
    {
        endPlayback( SequenceHistoryKind::Stopped );
    }

    bool SequencePlayerComponent::checkBindings()
    {
        GameObject*    pOwner = getOwner();
        vector<uint32> listUnbound;
        SequenceTimelineUtil::collectUnboundBindings( pOwner != nullptr ? pOwner->getManager() : nullptr, _player.getAsset(), makeBindingView(), listUnbound );
        for ( const uint32 bindingID : listUnbound )
        {
            const SequenceBinding* pBinding = _player.getAsset().findBinding( bindingID );
            const utf8*            pName    = pBinding != nullptr ? pBinding->_name.c_str() : "?";
            SW_LOG_ERROR_ID( "SequencePlayer.BindingUnbound", "Sequence '%#' on '%#': binding %# ('%#') is not bound to a live object - the sequence does not play", _sequencePath.c_str(),
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", bindingID, pName );
            _history.record( SequenceHistoryKind::UnboundBinding, _player.getCurrentFrame(), bindingID, pName );
        }
        return listUnbound.empty();
    }

    void SequencePlayerComponent::endPlayback( SequenceHistoryKind kind )
    {
        const bool bWasPlaying = _player.isPlaying() || kind == SequenceHistoryKind::Finished;
        const bool bHoldsState = _state._listSpawned.empty() == false || _state._listSpan.empty() == false || _state._bRestoreCaptured == SW_TRUE;
        if ( bWasPlaying == false && bHoldsState == false )
            return;
        GameObject*               pOwner   = getOwner();
        GameObjectManager*        pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SequenceAsset&      asset    = _player.getAsset();
        const SequenceBindingView view     = makeBindingView();
        const int32               frame    = _player.getCurrentFrame();

        // 컷이 덮던 중에 끝나면 시네마틱 끝을 알린다 — 듣는 쪽이 게임 카메라로 섞어 나온다. 끝내기 전의 프레임으로 본다.
        const SequenceTrackItem*     pCoveringCut = bWasPlaying ? asset.findCameraCut( frame ) : nullptr;
        vector<SequenceCameraSignal> listSignal;
        if ( pCoveringCut != nullptr )
        {
            SequenceCameraSignal signal;
            signal._item         = *pCoveringCut;
            signal._blendSeconds = asset._cameraBlendOutSeconds;
            signal._kind         = SequenceCameraSignalKind::CinematicEnd;
            listSignal.push_back( signal );
        }
        broadcastCameraSignals( listSignal ); // 컷 카메라가 아직 살아 있을 때 알린다(듣는 쪽이 그 화면에서 섞는다)
        _player.stop();
        SequenceTimelineUtil::releaseSpans( pManager, asset, view, _framesPerSecond, _state._listSpan );
        SequenceTimelineUtil::releaseSpawns( pManager, _state._listSpawned );
        // 카메라 컷은 컷 구간이 끝나야 해제되므로, 구간 안에서 끝나면 여기서 해제한다(게임 카메라가 원래 규칙으로 돌아간다).
        SequenceTimelineUtil::releaseCameraCut( pManager, asset );
        if ( _state._bRestoreCaptured == SW_TRUE )
            SequenceTimelineUtil::applyRestoreState( pManager, asset, view, _state._listRestore );
        _state._listRestore.clear();
        _state._bRestoreCaptured = SW_FALSE;
        if ( bWasPlaying )
            _history.record( kind, frame, kSequenceNoBinding, _sequencePath.path() );
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
        endPlayback( SequenceHistoryKind::Stopped );
        _player.setAsset( asset );
    }

    void SequencePlayerComponent::setBinding( uint32 bindingID, GameObjectHandle object )
    {
        for ( auto it = _listBinding.begin(); it != _listBinding.end(); ++it )
        {
            if ( it->_binding != bindingID )
                continue;
            if ( object.isValid() )
                it->_object = object;
            else
                _listBinding.erase( it );
            return;
        }
        if ( object.isValid() == false )
            return;
        SequenceBindingSlot slot;
        slot._binding = bindingID;
        slot._object  = object;
        _listBinding.push_back( slot );
    }

    SequenceBindingView SequencePlayerComponent::makeBindingView() const
    {
        SequenceBindingView view;
        view._pAsset       = &_player.getAsset();
        view._pListSlot    = &_listBinding;
        view._pListSpawned = &_state._listSpawned;
        return view;
    }

    GameObject* SequencePlayerComponent::findBoundObject( uint32 bindingID ) const
    {
        const GameObject* pOwner = getOwner();
        return SequenceTimelineUtil::findTarget( pOwner != nullptr ? pOwner->getManager() : nullptr, makeBindingView(), bindingID );
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
        const float32             secondsPerFrame = _framesPerSecond > 0.0f ? 1.0f / _framesPerSecond : 0.0f;
        const SequenceBindingView view            = makeBindingView();
        const int32               frame           = _player.getCurrentFrame();
        for ( SequenceCameraSignal& signal : inoutListSignal )
        {
            signal._secondsUntil    = static_cast<float32>( signal._framesUntil ) * secondsPerFrame;
            signal._durationSeconds = static_cast<float32>( signal._item._end - signal._item._start ) * secondsPerFrame;
            signal._target          = view.findHandle( signal._item._binding );
            _history.record( SequenceHistoryKind::CameraSignal, frame, signal._item._binding, signal._item._name, static_cast<uint8>( signal._kind ) );
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
        const SequenceBindingView        view = makeBindingView();
        vector<const SequenceTrackItem*> listCrossed;
        SequenceTimelineUtil::applyPlayback( pManager, _player, view, &listCrossed );
        SequenceTimelineUtil::applySpawns( pManager, _player.getAsset(), _player.getCurrentFrame(), _state._listSpawned );
        vector<const SequenceTrackItem*> listEnteredSpan;
        SequenceTimelineUtil::applySpans( pManager, _player.getAsset(), view, _player.getCurrentFrame(), _framesPerSecond, _state._listSpan, &listEnteredSpan );
        for ( const SequenceTrackItem* pItem : listEnteredSpan )
            _history.record( SequenceHistoryKind::Span, _player.getCurrentFrame(), pItem->_binding, pItem->_name );

        // 카메라 신호는 듣는 쪽이 있거나 기록할 때만 모은다(신호도 사본이라 핸들러가 시퀀스를 바꿔도 안전하다).
        if ( _cameraSignalMulticast.isBound() || build::kShipping == false )
        {
            vector<SequenceCameraSignal> listSignal;
            SequenceTimelineUtil::collectPlaybackCameraSignals( _player, listSignal );
            broadcastCameraSignals( listSignal );
        }
        if ( listCrossed.empty() )
            return;

        // **사본으로 알린다.** 지난 항목은 플레이어가 가진 에셋의 원소라, 핸들러가 다른 시퀀스를 설정하면(`setSequence`) 남은 포인터가 무효가 된다.
        vector<SequenceFiredEvent> listEvent;
        listEvent.reserve( listCrossed.size() );
        for ( const SequenceTrackItem* pItem : listCrossed )
        {
            SequenceFiredEvent event;
            event._item   = *pItem;
            event._target = view.findHandle( pItem->_binding );
            listEvent.push_back( std::move( event ) );
            _history.record( SequenceHistoryKind::Event, _player.getCurrentFrame(), pItem->_binding, pItem->_name );
        }
        for ( const SequenceFiredEvent& event : listEvent )
        {
            // 시나리오 항목은 엔진이 바로 재생기로 넘긴다(`ScenarioPlayback` — 재생기가 없거나 무엇이 돌고 있으면 그쪽이 까닭을 남긴다). 듣는 쪽에도 같은 이벤트로 알린다.
            const SequenceItemKindInfo* pKindInfo = SequenceAsset::findItemKindInfo( event._item._kind );
            if ( pKindInfo != nullptr && pKindInfo->_bPlaysScenario )
                (void)ScenarioPlayback::play( AssetID{ event._item._scenario } );
            _sequenceEventMulticast.broadcast( event );
        }
    }
} // namespace sw
