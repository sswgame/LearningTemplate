#include "pch.h"

#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Presentation/IUILayerVisibility.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    namespace
    {
        struct SequencePlayerComponentInternal
        {
            /** @brief 컷씬 입력 맵(`EngineDefaultAssets::_cutsceneInputMap`)의 레이어 · 액션 이름입니다. 맵 파일과 이 표가 계약이다(포토 모드 맵과 같은 꼴). */
            static constexpr const utf8* kCutsceneLayer     = "Cutscene";
            static constexpr const utf8* kSkipAction        = "Cutscene.Skip";
            static constexpr const utf8* kFastForwardAction = "Cutscene.FastForward";

            static const SequencePlayerComponent* findTargetPlayer( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<SequencePlayerComponent>( "sequence player", outValue );
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

            /** @brief 감시 값 `Sequence.PlayRate` 입니다 — 지금 걸린 재생 속도(빨리감기 배율 포함, 음수 = 역방향 속도 값)입니다. */
            [[nodiscard]] static bool readPlayRate( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlayer->getEffectivePlayRate() ) );
                return true;
            }

            /** @brief 감시 값 `Sequence.Reversed` 입니다 — 지금 거꾸로 진행하면 1(속도 부호와 왕복이 뒤집은 방향)입니다. */
            [[nodiscard]] static bool readReversed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( pPlayer->isReversed() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Sequence.InputBlocked` 입니다 — 대상 플레이어가 컷씬 레이어로 플레이어 입력(이동 · 시선 · 게임 동작)을 막고 있으면 1 입니다. */
            [[nodiscard]] static bool readInputBlocked( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SequencePlayerComponent* pPlayer = findTargetPlayer( query, outValue );
                if ( pPlayer == nullptr )
                    return false;
                outValue.setNumber( pPlayer->isBlockingPlayerInput() ? 1.0 : 0.0 );
                return true;
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
    SW_AUTOMATION_QUERY_WATCH( sequencePlayRate, "Sequence.PlayRate", "Play rate applied to the target's sequence player now (fast forward included; negative plays backwards)",
                               &SequencePlayerComponentInternal::readPlayRate, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceReversed, "Sequence.Reversed", "1 while the target's sequence plays backwards (negative rate or a ping-pong turn)", &SequencePlayerComponentInternal::readReversed,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceInputBlocked, "Sequence.InputBlocked", "1 while the target's sequence player blocks player movement, look and game actions with the Cutscene input layer",
                               &SequencePlayerComponentInternal::readInputBlocked, true, false );
    SW_AUTOMATION_QUERY_WATCH( sequenceBoundObject, "Sequence.BoundObject", "Name of the object the target's sequence player binds to binding id key; none when unbound",
                               &SequencePlayerComponentInternal::readBoundObject, true, true );
} // namespace sw

namespace sw
{
    SequencePlayerComponent::SequencePlayerComponent()
        : _sequencePath{}
        , _listBinding{}
        , _framesPerSecond{ 30.0f }
        , _playRate{ 1.0f }
        , _loopMode{ SequenceLoopMode::None }
        , _playerMovement{ SequencePlayerMovement::Free }
        , _confineTarget{}
        , _confineCenter{}
        , _confineRadius{ 0.0f }
        , _bAutoPlay{ SW_TRUE }
        , _bHideHUD{ SW_FALSE }
        , _bSkippable{ SW_FALSE }
        , _bAllowFastForward{ SW_FALSE }
        , _bInputLayerPushed{ SW_FALSE }
        , _bInputLayerStacked{ SW_FALSE }
        , _bFastForwardHeld{ SW_FALSE }
        , _bTimelineOnly{ SW_FALSE }
        , _reserved{ 0 }
        , _hudHide{}
        , _player{}
        , _pendingStep{}
        , _state{}
        , _history{}
        , _sequenceEventMulticast{}
        , _cameraSignalMulticast{}
        , _finishedMulticast{}
    {
        setCanEverTick( true );
    }

    void SequencePlayerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
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
        // 건너뛰기는 남의 오브젝트에 끝 상태를 쓰므로 틱 뒤에 한다. 빨리감기 배율은 이번 갱신 전에 건다.
        if ( readCutsceneInput() )
        {
            _pendingStep = SequencePlaybackStep::makeEmpty( _player.getCurrentFrame() );
            context.deferPostTickOnComponent( *this, []( SequencePlayerComponent& self )
            { (void)self.skipToEnd(); } );
            return;
        }
        _pendingStep = _player.update( context.getDeltaTime() );
        // 묶인 오브젝트(남의 오브젝트)에 값 · 스폰 · 구간을 적용하는 것은 틱 뒤(같은 프레임)에 한다 — 틱은 시간만 민다.
        context.deferPostTickOnComponent( *this, []( SequencePlayerComponent& self )
        { self.applyPendingStep(); } );
    }

    void SequencePlayerComponent::applyPendingStep()
    {
        applyTimeline( _pendingStep );
        applyConfine();
        // 루프가 아니면 끝 프레임을 적용한 갱신에서 플레이어가 멈춘다 — 그때 끝낸다.
        if ( _player.isPlaying() == false )
            endPlayback( SequenceHistoryKind::Finished );
    }

    void SequencePlayerComponent::advance( float32 seconds )
    {
        if ( _player.isPlaying() == false || seconds <= 0.0f )
            return;
        _pendingStep = _player.update( seconds );
        applyPendingStep();
    }

    void SequencePlayerComponent::play()
    {
        startPlayback( nullopt );
    }

    void SequencePlayerComponent::playFromFrame( int32 frame )
    {
        startPlayback( frame );
    }

    void SequencePlayerComponent::startPlayback( optional<int32> startFrame )
    {
        // 다시 틀기는 지금 재생을 끝내고 시작한다 — 되돌릴 값은 이번 재생의 시작 상태여야 한다.
        endPlayback( SequenceHistoryKind::Stopped );
        if ( _bTimelineOnly == SW_FALSE && validateBindings() == false )
            return;
        // 속도 0 은 데이터 결함이다 — 오류를 남기고 틀지 않는다(1 배속으로 돌아가는 길은 없다).
        if ( _player.setPlayRate( _playRate ) == false || validateConfine() == false )
            return;
        _player.setFramesPerSecond( _framesPerSecond );
        _player.setLoopMode( _loopMode );
        if ( _bTimelineOnly == SW_FALSE )
            acquireCutsceneOptions();
        GameObject* pOwner = getOwner();
        if ( _bTimelineOnly == SW_FALSE && pOwner != nullptr && _player.getAsset()._whenFinished == SequenceFinishPolicy::RestoreState )
        {
            SequenceTimelineUtil::captureRestoreState( pOwner->getMutableManager(), _player.getAsset(), makeBindingView(), _state._listRestore );
            _state._bRestoreCaptured = SW_TRUE;
        }
        if ( startFrame.has_value() )
            _player.playFromFrame( *startFrame );
        else
            _player.play();
        _history.record( SequenceHistoryKind::Started, _player.getCurrentFrame(), kSequenceNoBinding, _sequencePath.path() );
        applyTimeline( _player.getStartStep() );
    }

    void SequencePlayerComponent::stop()
    {
        endPlayback( SequenceHistoryKind::Stopped );
    }

    void SequencePlayerComponent::playReverse()
    {
        _playRate = -MathUtil::abs( _playRate );
        play();
    }

    bool SequencePlayerComponent::skipToEnd()
    {
        if ( _player.isPlaying() == false )
            return false;
        // 끝 프레임으로 점프해 그 상태를 적용하고(이벤트는 쏘지 않는다) 정상 끝을 지난다 — 덮던 컷이 있으면 끝내기가 시네마틱 끝을 낸다.
        applyTimeline( _player.skipToEnd() );
        endPlayback( SequenceHistoryKind::Finished );
        return true;
    }

    bool SequencePlayerComponent::setPlayRate( float32 playRate )
    {
        // 빨리감기 중이면 배율을 곱한 값을 건다 — 놓으면 이 값으로 돌아온다.
        const float32 scale = _bFastForwardHeld == SW_TRUE ? _player.getPlayRate() / _playRate : 1.0f;
        if ( _player.setPlayRate( playRate * scale ) == false )
            return false;
        _playRate = playRate;
        return true;
    }

    bool SequencePlayerComponent::validateConfine() const
    {
        if ( _playerMovement != SequencePlayerMovement::Confined )
            return true;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool               bTarget  = pManager != nullptr && pManager->resolveGameObject( _confineTarget ) != nullptr;
        const bool               bCenter  = pManager != nullptr && pManager->resolveGameObject( _confineCenter ) != nullptr;
        if ( bTarget && bCenter && _confineRadius > 0.0f )
            return true;
        const utf8* pMissing = bTarget == false ? "a live _confineTarget" : ( bCenter == false ? "a live _confineCenter" : "a _confineRadius above 0" );
        SW_LOG_ERROR_ID( "SequencePlayer.ConfineIncomplete", "Sequence '%#' on '%#' confines the player but lacks %# - the sequence does not play", _sequencePath.c_str(),
                         pOwner != nullptr ? pOwner->getName().c_str() : "?", pMissing );
        return false;
    }

    InputMap* SequencePlayerComponent::findPlayerInputMap()
    {
        InputManager* pInput = engine::findSubsystem<InputManager>();
        return pInput != nullptr ? &pInput->getInputMap() : nullptr;
    }

    void SequencePlayerComponent::acquireCutsceneOptions()
    {
        // HUD 는 이 컴포넌트가 모른다 — 숨기는 일은 레이어 숨김 창구(구현은 UI 시스템)가 한다. 창구가 없으면(전용 서버) 가릴 화면도 없다.
        IUILayerVisibility* pLayerVisibility = engine::findSubsystem<IUILayerVisibility>();
        if ( _bHideHUD == SW_TRUE && pLayerVisibility != nullptr )
            _hudHide = pLayerVisibility->addLayerHideRequest( UILayerMask::HUD );
        const bool bNeedsInput = _playerMovement == SequencePlayerMovement::Blocked || _bSkippable == SW_TRUE || _bAllowFastForward == SW_TRUE;
        InputMap*  pInputMap   = bNeedsInput ? findPlayerInputMap() : nullptr;
        if ( pInputMap == nullptr )
            return; // 쓸 입력이 없거나 입력이 없는 구성이다(전용 서버 · 하네스)
        // 컷씬 레이어는 엔진 맵이다(포토 모드 맵과 같은 꼴 — 게임마다 액션을 더하지 않는다). 처음 쓸 때 플레이어 맵에 더한다.
        const hashed_string layerName( SequencePlayerComponentInternal::kCutsceneLayer );
        if ( pInputMap->hasLayer( layerName ) == false )
        {
            const EngineDefaultAssets* pDefaults = engine::findSubsystem<EngineDefaultAssets>();
            const bool                 bAppended = pDefaults != nullptr && pInputMap->appendFromAsset( AssetID{ pDefaults->_cutsceneInputMap.get() } );
            if ( bAppended == false )
            {
                SW_LOG_ERROR_ID( "SequencePlayer.CutsceneInputMap", "Cutscene input map could not be added to the player's map - '%#' plays without blocking, skip or fast forward",
                                 _sequencePath.c_str() );
                return;
            }
        }
        // 컷씬 레이어는 늘 켜지는 덧층(alwaysOn — cutscene.input.xml)이라 켜기만 하면 건너뛰기 · 빨리감기가 듣는다. 레이어 스택은 문맥 바꾸기라(스택에 무엇이 있으면 스택 밖 레이어가 꺼진다)
        // 막지 않는 재생(Free · Confined)이 스택에 올리면 게임 이동 레이어까지 꺼져 플레이어가 서 버렸다. 막기(Blocked)만 스택에 올려 아래 레이어(이동 · 시선 · 게임 동작)를 막는다.
        // 일시정지 · 메뉴는 UI 맵이라 막히지 않는다. 언리얼 Enhanced Input 의 매핑 문맥 더하기(우선순위 덧층)와 같은 자리다.
        pInputMap->setLayerEnabled( layerName, true );
        _bInputLayerPushed = SW_TRUE;
        if ( _playerMovement == SequencePlayerMovement::Blocked )
        {
            pInputMap->pushLayer( layerName, true );
            _bInputLayerStacked = SW_TRUE;
        }
    }

    void SequencePlayerComponent::releaseCutsceneOptions()
    {
        _hudHide.reset();
        if ( _bInputLayerPushed == SW_TRUE )
        {
            InputMap*           pInputMap = findPlayerInputMap();
            const hashed_string layerName( SequencePlayerComponentInternal::kCutsceneLayer );
            if ( pInputMap != nullptr && _bInputLayerStacked == SW_TRUE )
                pInputMap->popLayer( layerName );
            if ( pInputMap != nullptr )
                pInputMap->setLayerEnabled( layerName, false );
            _bInputLayerPushed  = SW_FALSE;
            _bInputLayerStacked = SW_FALSE;
        }
        if ( _bFastForwardHeld == SW_TRUE )
        {
            (void)_player.setPlayRate( _playRate );
            _bFastForwardHeld = SW_FALSE;
        }
    }

    bool SequencePlayerComponent::readCutsceneInput()
    {
        if ( _bInputLayerPushed == SW_FALSE )
            return false;
        // 게임이 멈춘 동안(일시정지 메뉴 — 게임 시간과 같은 원천)은 컷씬 입력도 쉰다. 재생도 멈춘 시간으로 서 있다가 재개하면 그 자리부터 이어 간다.
        const GameTimeScale* pTimeScale = engine::findSubsystem<GameTimeScale>();
        const InputMap*      pInputMap  = findPlayerInputMap();
        if ( pInputMap == nullptr || ( pTimeScale != nullptr && pTimeScale->get() <= 0.0f ) )
            return false;
        if ( _bSkippable == SW_TRUE && pInputMap->wasActionTriggered( hashed_string( SequencePlayerComponentInternal::kSkipAction ) ) )
            return true;
        const bool bHeld = _bAllowFastForward == SW_TRUE && pInputMap->isActionDown( hashed_string( SequencePlayerComponentInternal::kFastForwardAction ) );
        if ( bHeld == ( _bFastForwardHeld == SW_TRUE ) )
            return false;
        const EngineConfig* pConfig = engine::findSubsystem<EngineConfig>();
        if ( bHeld && pConfig == nullptr )
        {
            SW_LOG_ERROR_ID( "SequencePlayer.FastForwardNoConfig", "Fast forward needs EngineConfig _sequencer._fastForwardRate - '%#' keeps its rate", _sequencePath.c_str() );
            return false;
        }
        // 배율을 곱한 속도를 걸고, 놓으면 컴포넌트의 속도로 돌아온다(방향 · 왕복 표지는 플레이어가 그대로 든다).
        const float32 rate = bHeld ? _playRate * pConfig->_sequencer._fastForwardRate : _playRate;
        if ( _player.setPlayRate( rate ) )
            _bFastForwardHeld = bHeld ? SW_TRUE : SW_FALSE;
        return false;
    }

    void SequencePlayerComponent::applyConfine()
    {
        if ( _playerMovement != SequencePlayerMovement::Confined || _player.isPlaying() == false )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        GameObject*        pTarget  = pManager != nullptr ? pManager->resolveGameObject( _confineTarget ) : nullptr;
        const GameObject*  pCenter  = pManager != nullptr ? pManager->resolveGameObject( _confineCenter ) : nullptr;
        SceneComponent*    pScene   = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr || pCenter == nullptr || pCenter->getPrimarySceneComponent() == nullptr )
            return;
        // 바닥 평면의 원이다(반지름 칸) — 다른 모양으로 가두려면 그 씬에 막는 콜라이더를 둔다(언리얼 BlockingVolume 자리).
        const float3  center   = pCenter->getPrimarySceneComponent()->getWorldPosition();
        const float3  position = pScene->getWorldPosition();
        const float32 dx       = position._x - center._x;
        const float32 dz       = position._z - center._z;
        const float32 distance = MathUtil::sqrt( dx * dx + dz * dz );
        if ( distance <= _confineRadius )
            return;
        // 경계에서 멈춘다 — 이어진 움직임으로 옮겨(`setWorldPosition`, 순간이동이 아니다) 물리 · 이동 기록이 따라온다.
        const float32 scale = _confineRadius / distance;
        pScene->setWorldPosition( float3{ center._x + dx * scale, position._y, center._z + dz * scale } );
    }

    void SequencePlayerComponent::setLoopMode( SequenceLoopMode loopMode )
    {
        _loopMode = loopMode;
        _player.setLoopMode( loopMode );
    }

    bool SequencePlayerComponent::validateBindings()
    {
        GameObject*    pOwner = getOwner();
        vector<uint32> listUnbound;
        SequenceTimelineUtil::collectUnboundBindings( pOwner != nullptr ? pOwner->getMutableManager() : nullptr, _player.getAsset(), makeBindingView(), listUnbound );
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
        const bool bHoldsState = _state._listSpawned.empty() == false || _state._listSpan.empty() == false || _state._bRestoreCaptured == SW_TRUE || _bInputLayerPushed == SW_TRUE ||
                                 _hudHide.isActive();
        if ( bWasPlaying == false && bHoldsState == false )
            return;
        if ( _bTimelineOnly == SW_TRUE )
        {
            // 아무것도 적용하지 않았다 — 되돌리거나 풀 것이 없다. 씬의 컷은 적용하는 재생의 것이라 풀지 않는다.
            const int32 endFrame = _player.getCurrentFrame();
            if ( kind != SequenceHistoryKind::Finished )
                _player.stop();
            _history.record( kind, endFrame, kSequenceNoBinding, _sequencePath.path() );
            if ( kind == SequenceHistoryKind::Finished )
                _finishedMulticast.broadcast();
            return;
        }
        GameObject*               pOwner   = getOwner();
        GameObjectManager*        pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
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
        releaseCutsceneOptions();
        // 정상 끝(끝 프레임 · 건너뛰기)은 위치를 끝에 둔다(언리얼 OnFinished · GoToEndAndStop) — 멈춤(Stop)만 처음으로 되감는다.
        if ( kind != SequenceHistoryKind::Finished )
            _player.stop();
        SequenceTimelineUtil::releaseSpans( pManager, asset, view, _framesPerSecond, _state._listSpan );
        SequenceTimelineUtil::releaseSpawns( pManager, _state._listSpawned );
        // 카메라 컷은 컷 구간이 끝나야 해제되므로, 구간 안에서 끝나면 여기서 해제한다(게임 카메라가 원래 규칙으로 돌아간다).
        SequenceTimelineUtil::releaseCameraCut( pManager, asset );
        if ( _state._bRestoreCaptured == SW_TRUE )
            SequenceTimelineUtil::applyRestoreState( pManager, asset, view, _state._listRestore );
        _state._listRestore.clear();
        _state._bRestoreCaptured = SW_FALSE;
        if ( bWasPlaying == false )
            return;
        _history.record( kind, frame, kSequenceNoBinding, _sequencePath.path() );
        if ( kind == SequenceHistoryKind::Finished )
            _finishedMulticast.broadcast(); // 마지막 — 듣는 쪽이 다른 시퀀스를 틀어도 이 끝내기는 이미 마쳤다
    }

    void SequencePlayerComponent::pause()
    {
        _player.pause();
    }

    void SequencePlayerComponent::resume()
    {
        _player.resume();
    }

    void SequencePlayerComponent::copyTimelineFrom( const SequencePlayerComponent& source )
    {
        setSequence( source._player.getAsset() ); // 파일 경로가 아니라 열린 시퀀스를 옮긴다 — 경로를 옮기면 시작(onBeginPlay)이 다시 열며 재생을 끝낸다
        _listBinding.clear();
        _framesPerSecond = source._framesPerSecond;
        _playRate        = source._playRate;
        _loopMode        = source._loopMode;
        _playerMovement  = SequencePlayerMovement::Free;
        _bAutoPlay       = SW_FALSE;
        _bTimelineOnly   = SW_TRUE;
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
        return SequenceTimelineUtil::findTarget( pOwner != nullptr ? pOwner->getMutableManager() : nullptr, makeBindingView(), bindingID );
    }

    Subscription SequencePlayerComponent::registerSequenceEvent( const OnSequenceEventDelegate& delegate )
    {
        return _sequenceEventMulticast.add( delegate );
    }

    Subscription SequencePlayerComponent::registerCameraSignal( const OnSequenceCameraSignalDelegate& delegate )
    {
        return _cameraSignalMulticast.add( delegate );
    }

    Subscription SequencePlayerComponent::registerFinished( const OnSequenceFinishedDelegate& delegate )
    {
        return _finishedMulticast.add( delegate );
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
            // 컷 사이 섞임은 벽시계 초다 — 2 배속이면 반 시간에 섞는다.
            if ( signal._kind == SequenceCameraSignalKind::CutBegin )
                signal._blendSeconds = static_cast<float32>( signal._blendFrames ) * secondsPerFrame / MathUtil::abs( _player.getPlayRate() );
            signal._target = view.findHandle( signal._item._binding );
            _history.record( SequenceHistoryKind::CameraSignal, frame, signal._item._binding, signal._item._name, static_cast<uint8>( signal._kind ) );
        }
        for ( const SequenceCameraSignal& signal : inoutListSignal )
        {
            _cameraSignalMulticast.broadcast( signal );
        }
    }

    void SequencePlayerComponent::applyTimeline( const SequencePlaybackStep& step )
    {
        if ( _bTimelineOnly == SW_TRUE )
            return;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        GameObjectManager* pManager = pOwner->getMutableManager();
        if ( pManager == nullptr )
            return;

        // 지난 구간마다(반복 되감기 · 왕복 꺾임이면 둘) 문턱을 넘은 이벤트를 본다(`applyPlayback`).
        const SequenceBindingView        view = makeBindingView();
        vector<const SequenceTrackItem*> listCrossed;
        SequenceTimelineUtil::applyPlayback( pManager, _player.getAsset(), step, view, &listCrossed );
        SequenceTimelineUtil::applySpawns( pManager, _player.getAsset(), _player.getCurrentFrame(), _state._listSpawned );
        vector<const SequenceTrackItem*> listEnteredSpan;
        SequenceTimelineUtil::applySpans( pManager, _player.getAsset(), view, _player.getCurrentFrame(), _framesPerSecond, _player.getPlayRate(), _state._listSpan, &listEnteredSpan );
        for ( const SequenceTrackItem* pItem : listEnteredSpan )
            _history.record( SequenceHistoryKind::Span, _player.getCurrentFrame(), pItem->_binding, pItem->_name );

        // 카메라 신호는 듣는 쪽이 있거나 기록할 때만 모은다(신호도 사본이라 핸들러가 시퀀스를 바꿔도 안전하다).
        if ( _cameraSignalMulticast.isBound() || build::kShipping == false )
        {
            vector<SequenceCameraSignal> listSignal;
            SequenceTimelineUtil::collectPlaybackCameraSignals( _player.getAsset(), step, listSignal );
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
