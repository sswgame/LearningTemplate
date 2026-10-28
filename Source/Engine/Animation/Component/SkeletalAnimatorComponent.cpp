#include "pch.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Notify/AnimationNotifyListener.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "SkeletalAnimator" );

    SkeletalAnimatorBinding::SkeletalAnimatorBinding( SkeletalAnimatorComponent& owner )
        : _owner{ owner }
    {
    }

    bool SkeletalAnimatorBinding::isAnimationActive() const
    {
        return _owner.isAnimationActive();
    }

    bool SkeletalAnimatorBinding::pollAnimationAssetsReady()
    {
        return _owner.finishClipLoadsIfComplete();
    }

    void SkeletalAnimatorBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner.advanceTime( context );
        else if ( phase == AnimationPhase::BasePose )
            _owner.populateBasePose( unit );
    }

    AnimationPlayer* SkeletalAnimatorBinding::findSyncPlayer( hashed_string& outGroupName, float32& outWeight )
    {
        if ( _owner._syncGroup.empty() )
            return nullptr;
        outGroupName = hashed_string( _owner._syncGroup );
        outWeight    = _owner._syncWeight;
        return &_owner._graphPlayer.getPlayer();
    }

    void SkeletalAnimatorBinding::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        // 알림이 먼저다 — 받는 쪽(모션 워핑 창)이 이번 프레임의 루트 모션을 바꿀 수 있다.
        _owner.dispatchNotifies();
        _owner.applyRootMotion( unit );
    }

    void SkeletalAnimatorBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    bool SkeletalAnimatorBinding::describeSharedPose( AnimationSharedPoseRequest& outRequest ) const
    {
        return _owner.describeSharedPose( outRequest );
    }

    shared_ptr<const AnimationClip> SkeletalAnimatorBinding::findSharedPoseClip( const AnimationClip* pClip ) const
    {
        for ( const auto& [name, clip] : _owner._mapClip )
        {
            if ( clip.get() == pClip )
                return clip;
        }
        return nullptr;
    }

    void SkeletalAnimatorBinding::collectDebugState( AnimationDebugState& inoutState ) const
    {
        inoutState._stateName = _owner._graphPlayer.getCurrentStateName();
        inoutState._stateTime = _owner._stateTime;
        for ( const AnimationFiredNotify& notify : _owner._listFiredNotify )
        {
            inoutState._listNotify.push_back( notify._name );
        }
        inoutState._listCurveName.insert( inoutState._listCurveName.end(), _owner._curves.getNames().begin(), _owner._curves.getNames().end() );
        inoutState._listCurveValue.insert( inoutState._listCurveValue.end(), _owner._curves.getValues().begin(), _owner._curves.getValues().end() );
        inoutState._rootMotionTranslation = _owner._rootMotionDelta._translation;
        inoutState._rootMotionRotation    = _owner._rootMotionDelta._rotation;
    }

    const IAnimationPlayable* SkeletalAnimatorBinding::findPlayable( const hashed_string& name ) const
    {
        // 블렌드 스페이스 노드는 그 상태를, 보통 노드(또는 그래프 밖 이름)는 같은 이름의 클립을 낸다.
        for ( const unique_ptr<SkeletalBlendSpaceState>& state : _owner._listBlendSpace )
        {
            if ( state->getStateName() == name )
                return state.get();
        }
        return _owner.findClip( name );
    }

    SkeletalBlendSpaceState::SkeletalBlendSpaceState( const hashed_string& stateName, const AnimationGraphBlendSpaceDef& def, vector<const AnimationClip*>&& listClip )
        : _stateName{ stateName }
        , _pDef{ &def }
        , _listClip{ std::move( listClip ) }
        , _listLength{}
        , _listWeight{}
        , _playLength{ 0.0f }
    {
        _listLength.reserve( _listClip.size() );
        for ( const AnimationClip* pClip : _listClip )
        {
            _listLength.push_back( pClip->getPlayLength() );
        }
        _listWeight.assign( _listClip.size(), 0.0f );
    }

    void SkeletalBlendSpaceState::updateWeights( const AnimationParameterSet& parameters )
    {
        const float2 parameter{ parameters.getFloat( _pDef->_parameterX ), _pDef->_parameterY.empty() ? 0.0f : parameters.getFloat( _pDef->_parameterY ) };
        _pDef->_space.computeSampleWeights( parameter, _listWeight );
        _playLength = BlendSpace::computeBlendedLength( _listLength, _listWeight );
    }

    float32 SkeletalBlendSpaceState::computeSampleTime( uint32 sampleIndex, float32 time ) const
    {
        if ( _playLength <= 0.0f )
            return 0.0f;
        return time / _playLength * _listLength[sampleIndex];
    }

    bool SkeletalBlendSpaceState::isNotifySample( uint32 sampleIndex ) const
    {
        // 가중치 0 인 표본은 포즈에 없으므로 알림도 내지 않는다(언리얼의 알림 가중치 임계값과 같다).
        const float32 weight = _listWeight[sampleIndex];
        switch ( _pDef->_notifyMode )
        {
            case AnimationBlendSpaceNotifyMode::All:
                return weight > 0.0f;
            case AnimationBlendSpaceNotifyMode::HighestWeight:
                return weight > 0.0f && std::max_element( _listWeight.begin(), _listWeight.end() ) - _listWeight.begin() == static_cast<ptrdiff_t>( sampleIndex );
            case AnimationBlendSpaceNotifyMode::None:
                return false;
        }
        return false;
    }

    void SkeletalBlendSpaceState::collectFiredNotifies( const AnimationTimeStep& step, float32 weight, vector<AnimationFiredNotify>& outListFired ) const
    {
        // 표본 클립은 같은 위상으로 돈다 — 걸음의 양 끝을 그 클립의 시각으로 옮기면 감긴 횟수는 그대로다.
        for ( uint32 sampleIndex = 0; sampleIndex < getSampleCount(); ++sampleIndex )
        {
            if ( isNotifySample( sampleIndex ) == false )
                continue;
            AnimationTimeStep sampleStep = step;
            sampleStep._previousTime     = computeSampleTime( sampleIndex, step._previousTime );
            sampleStep._currentTime      = computeSampleTime( sampleIndex, step._currentTime );
            _listClip[sampleIndex]->collectFiredNotifies( sampleStep, weight * _listWeight[sampleIndex], outListFired );
        }
    }

    void SkeletalBlendSpaceState::appendNotifySources( vector<const IAnimationPlayable*>& inoutListPlayable ) const
    {
        for ( uint32 sampleIndex = 0; sampleIndex < getSampleCount(); ++sampleIndex )
        {
            if ( isNotifySample( sampleIndex ) )
                inoutListPlayable.push_back( _listClip[sampleIndex] );
        }
    }

    SkeletalAnimatorComponent::SkeletalAnimatorComponent()
        : _animationGraphPath{}
        , _clipFolder{}
        , _initialState{}
        , _currentState{}
        , _stateTime{ 0.0f }
        , _playRate{ 1.0f }
        , _blendSeconds{ 0.2f }
        , _syncGroup{}
        , _syncWeight{ 1.0f }
        , _binding{ *this }
        , _graph{}
        , _graphPlayer{}
        , _parameters{}
        , _mapClip{}
        , _listClipLoad{}
        , _deferredPlayState{}
        , _deferredPlayBlend{ -1.0f }
        , _mapTrackToBone{}
        , _pTrackMapSkeleton{ nullptr }
        , _listLayer{}
        , _listGraphLayer{}
        , _listBlendSpace{}
        , _listScratchSamplePose{}
        , _listScratchSamplePosePointer{}
        , _listScratchSampleWeight{}
        , _listFiredNotify{}
        , _listActivePlayable{}
        , _listRootMotionModifier{}
        , _rootMotionModifierSource{ _listRootMotionModifier }
        , _curves{}
        , _scratchPose{}
        , _scratchTrackPose{}
        , _scratchLayerPose{}
        , _listScratchTrackMask{}
        , _baseHistory{}
        , _carryOverPose{}
        , _inertializer{}
        , _sequencerClip{}
        , _rootMotionDelta{}
        , _pUnit{ nullptr }
        , _pNotifyListener{ nullptr }
        , _lastDeltaSeconds{ 0.0f }
        , _sequencerTime{ 0.0f }
        , _sequencerWeight{ 0.0f }
        , _initialTime{ 0.0f }
        , _carryOverElapsed{ 0.0f }
        , _carryOverDuration{ 0.0f }
        , _seenInterruptCount{ 0 }
        , _seenInertializationCount{ 0 }
        , _lastTimeFrameIndex{ 0 }
        , _bExtractRootMotion{ SW_FALSE }
        , _bPlayOnBegin{ SW_TRUE }
        , _bRootMotionThroughController{ SW_TRUE }
        , _bPoseShownThisFrame{ SW_FALSE }
        , _bCarryingOver{ SW_FALSE }
        , _bInertializationPending{ SW_FALSE }
        , _bClipsPending{ SW_FALSE }
        , _bStartPending{ SW_FALSE }
        , _bPlayDeferred{ SW_FALSE }
        , _bDeferredPlayLoop{ SW_TRUE }
        , _reserved{ 0 }
    {
        _graphPlayer.setPlayableSource( &_binding );
    }

    SkeletalAnimatorComponent::~SkeletalAnimatorComponent()
    {
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
    }

    void SkeletalAnimatorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _graphPlayer.setDefaultBlendSeconds( _blendSeconds );
        bindToUnit();
        // 스폰이 디스크를 기다리지 않게 클립은 워커가 읽는다. 다 읽힐 때까지 유닛은 그리지 않고(캐릭터는 애니메이션이 준비된 뒤 나타난다),
        // 씬 프레임의 도착 단계가 유닛을 통해 마무리를 묻는다(`pollAnimationAssetsReady`). 묻는 쪽(유닛)이 없으면 그 자리에서 읽는다.
        if ( _pUnit != nullptr )
            loadGraphForBeginPlay();
        else
            loadGraph();
        if ( _bClipsPending == SW_TRUE )
        {
            _bStartPending = _bPlayOnBegin;
            _pUnit->holdUntilAnimationAssetsReady();
            return;
        }
        if ( _bPlayOnBegin == SW_TRUE )
            startInitialState();
    }

    void SkeletalAnimatorComponent::startInitialState()
    {
        // 핫 리로드로 되살아났으면 그 상태 · 시각에서 잇는다. 아니면 처음 상태부터.
        const float32 resumeTime = _stateTime;
        if ( _currentState.empty() == false && play( hashed_string( _currentState ), true, 0.0f ) )
            _graphPlayer.getPlayer().setCurrentTime( resumeTime );
        else if ( play( hashed_string( _initialState ), true, 0.0f ) && _initialTime > 0.0f )
            _graphPlayer.getPlayer().setCurrentTime( _initialTime );
    }

    void SkeletalAnimatorComponent::loadGraphForBeginPlay()
    {
        _graph = AnimationGraphAsset{};
        if ( _animationGraphPath.empty() == false && _graph.loadFromFile( AssetID{ _animationGraphPath } ) == false )
            SW_LOG_ERROR( "Animation graph '%#' could not be loaded", _animationGraphPath.c_str() );
        requestClipLoads();
        if ( _listClipLoad.empty() )
        {
            applyGraphPlayables();
            return;
        }
        _bClipsPending = SW_TRUE;
        (void)finishClipLoadsIfComplete(); // 모두 이미 읽혀 있었으면(같은 클립을 쓰는 캐릭터가 먼저 읽었다) 바로 끝난다
    }

    void SkeletalAnimatorComponent::requestClipLoad( const hashed_string& clipName )
    {
        if ( clipName.empty() || _mapClip.find( clipName ) != _mapClip.end() )
            return;
        for ( const ClipLoad& existing : _listClipLoad )
        {
            if ( existing._clipName == clipName )
                return;
        }
        ClipLoad& added = _listClipLoad.emplace_back();
        added._clipName = clipName;
        added._load     = SharedAssetCache<AnimationClip>::request( makeClipPath( clipName ) );
    }

    void SkeletalAnimatorComponent::requestClipLoads()
    {
        _listClipLoad.clear();
        // 그래프를 짓는 것(`rebuildGraphPlayables`)과 같은 이름을 모은다 — 노드(블렌드 스페이스면 표본), 레이어. 그래프가 없으면 시작 상태가 곧 클립이다.
        for ( const AnimationGraphNode& node : _graph._listNode )
        {
            if ( node.hasPoseNode() )
                continue;
            if ( node.hasBlendSpace() == false )
            {
                requestClipLoad( hashed_string( node._name ) );
                continue;
            }
            for ( const BlendSpaceSample& sample : node._blendSpace._space.getSamples() )
            {
                requestClipLoad( sample._clipName );
            }
        }
        for ( const AnimationGraphLayerDef& def : _graph._listLayer )
        {
            requestClipLoad( def._clipName );
        }
        if ( _graph._listNode.empty() )
        {
            requestClipLoad( hashed_string( _currentState ) );
            requestClipLoad( hashed_string( _initialState ) );
        }
    }

    bool SkeletalAnimatorComponent::finishClipLoadsIfComplete()
    {
        if ( _bClipsPending == SW_FALSE )
            return true;
        for ( const ClipLoad& load : _listClipLoad )
        {
            if ( load._load.isLoading() )
                return false;
        }
        // 다 끝났다. 읽은 것만 표에 넣는다 — 못 읽은 이름은 그래프를 지을 때(`rebuildGraphPlayables`) · 재생할 때 오류가 드러난다.
        for ( const ClipLoad& load : _listClipLoad )
        {
            shared_ptr<const AnimationClip> clip = load._load.get();
            if ( clip != nullptr )
                _mapClip.emplace( load._clipName, std::move( clip ) );
        }
        _listClipLoad.clear();
        _bClipsPending = SW_FALSE;
        applyGraphPlayables();
        runDeferredPlayback();
        return true;
    }

    void SkeletalAnimatorComponent::runDeferredPlayback()
    {
        if ( _bPlayDeferred == SW_TRUE )
        {
            _bPlayDeferred = SW_FALSE;
            _bStartPending = SW_FALSE; // 기다리는 동안 코드가 다른 상태를 재생하라고 했다 — 그것이 이긴다
            (void)play( _deferredPlayState, _bDeferredPlayLoop == SW_TRUE, _deferredPlayBlend );
        }
        else if ( _bStartPending == SW_TRUE )
        {
            _bStartPending = SW_FALSE;
            startInitialState();
        }
    }

    void SkeletalAnimatorComponent::waitForAnimationAssets()
    {
        for ( const ClipLoad& load : _listClipLoad )
        {
            (void)load._load.wait();
        }
        (void)finishClipLoadsIfComplete(); // 위에서 모든 읽기를 기다렸다 — 끝났는지 다시 묻지 않는다
    }

    void SkeletalAnimatorComponent::onEndPlay()
    {
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
        _pUnit = nullptr;
        Component::onEndPlay();
    }

    void SkeletalAnimatorComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_graphPathName( "_animationGraphPath" );
        static const hashed_string s_blendName( "_blendSeconds" );
        if ( propertyName == s_graphPathName )
        {
            const hashed_string state = _graphPlayer.getCurrentStateName();
            loadGraph();
            (void)play( state.empty() ? hashed_string( _initialState ) : state, true, 0.0f );
        }
        else if ( propertyName == s_blendName )
        {
            _graphPlayer.setDefaultBlendSeconds( _blendSeconds );
        }
    }

    void SkeletalAnimatorComponent::bindToUnit()
    {
        GameObject*            pOwner = getOwner();
        SkeletalMeshComponent* pUnit  = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit == _pUnit )
            return;
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _bindingRegistration = _pUnit->registerAnimationPhaseTask( &_binding );
        else
            SW_LOG_WARNING( "'%#': skeletal animator has no SkeletalMeshComponent on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
    }

    void SkeletalAnimatorComponent::loadGraph()
    {
        // 코드가 그래프를 바꿨다 — 시작 때 요청해 둔 로드는 버리고 그 자리에서 다시 읽는다(동기 API). 미룬 시작 재생은 아래 뒤에 한다.
        const bool bWasPending = _bClipsPending == SW_TRUE;
        _listClipLoad.clear();
        _bClipsPending = SW_FALSE;
        _graph         = AnimationGraphAsset{};
        if ( _animationGraphPath.empty() == false && _graph.loadFromFile( AssetID{ _animationGraphPath } ) == false )
            SW_LOG_ERROR( "Animation graph '%#' could not be loaded", _animationGraphPath.c_str() );
        applyGraphPlayables();
        if ( bWasPending )
            runDeferredPlayback();
    }

    void SkeletalAnimatorComponent::applyGraphPlayables()
    {
        // 플레이어가 빌린 상태를 먼저 놓는다(`setGraph` 는 재생을 멈춘다).
        _graphPlayer.setGraph( nullptr );
        if ( rebuildGraphPlayables() )
        {
            _graphPlayer.setGraph( _graph._listNode.empty() ? nullptr : &_graph );
            return;
        }
        // 빠진 것만 빼고 돌지 않는다 — 파일을 못 읽은 그래프와 같이 플레이어에 그래프를 걸지 않는다. 데이터는 남겨 클립 폴더를 바꾸면 다시 맞춰 본다.
        SW_LOG_ERROR( "Animation graph '%#' is not used - it names what SkeletalAnimatorComponent cannot play (errors above)", _animationGraphPath.c_str() );
        _listBlendSpace.clear();
        _listGraphLayer.clear();
    }

    bool SkeletalAnimatorComponent::rebuildGraphPlayables()
    {
        // 플레이어(`setGraph`, `stop`)가 빌린 상태를 놓은 뒤에 부른다. 워커의 전이는 읽어 둔 클립만 찾으므로 클립은 지금(게임 스레드) 읽는다.
        _listBlendSpace.clear();
        bool bPlayable = true;
        for ( const AnimationGraphNode& node : _graph._listNode )
        {
            // 포즈 노드(`AnimationPoseNodeSet`)는 아직 이 애니메이터가 평가하지 않는다 — 클립으로 잘못 재생하지 않고 그래프를 쓰지 않는다.
            if ( node.hasPoseNode() )
            {
                SW_LOG_ERROR( "Animation graph '%#' node '%#' is a pose node, which SkeletalAnimatorComponent does not evaluate yet", _animationGraphPath.c_str(), node._name.c_str() );
                bPlayable = false;
                continue;
            }
            if ( node.hasBlendSpace() == false )
            {
                (void)preloadClip( hashed_string( node._name ) ); // 노드 이름이 클립이 아닌 상태(이름만 옮기는 상태)도 있다. 재생할 때 못 찾으면 play 가 오류를 남긴다
                continue;
            }
            vector<const AnimationClip*> listClip;
            for ( const BlendSpaceSample& sample : node._blendSpace._space.getSamples() )
            {
                const bool bLoaded = preloadClip( sample._clipName );
                if ( bLoaded == false )
                    SW_LOG_ERROR( "Blend space state '%#' sample clip '%#' was not found in '%#'", node._name.c_str(), sample._clipName.c_str(), _clipFolder.c_str() );
                listClip.push_back( bLoaded ? findClip( sample._clipName ) : nullptr );
            }
            // 표본 클립이 하나라도 없으면 그래프를 쓰지 않는다 — 없는 표본을 빼고 섞으면 데이터가 말한 것과 다른 포즈가 된다.
            if ( VectorUtil::contains( listClip, nullptr ) )
            {
                bPlayable = false;
                continue;
            }
            unique_ptr<SkeletalBlendSpaceState> state = sw::make_unique<SkeletalBlendSpaceState>( hashed_string( node._name ), node._blendSpace, std::move( listClip ) );
            state->updateWeights( _parameters );
            _listBlendSpace.push_back( std::move( state ) );
        }
        return rebuildGraphLayers() && bPlayable;
    }

    bool SkeletalAnimatorComponent::rebuildGraphLayers()
    {
        _listGraphLayer.clear();
        bool bPlayable = true;
        for ( const AnimationGraphLayerDef& def : _graph._listLayer )
        {
            if ( preloadClip( def._clipName ) == false )
            {
                SW_LOG_ERROR( "Animation graph layer '%#' clip '%#' was not found in '%#'", def._name.c_str(), def._clipName.c_str(), _clipFolder.c_str() );
                bPlayable = false;
                continue;
            }
            LayerState layer{};
            layer._desc._clipName        = def._clipName;
            layer._desc._weight          = def._weight;
            layer._desc._blend           = def._blend;
            layer._desc._bLoop           = def._bLoop;
            layer._desc._halfLifeSeconds = def._halfLifeSeconds;
            layer._pGraphLayer           = &def;
            layer._pBoneMask             = def._maskName.empty() ? nullptr : _graph.findBoneMask( def._maskName );
            layer._clip                  = _mapClip[def._clipName];
            layer._player.play( layer._clip.get(), def._bLoop == SW_TRUE );
            _listGraphLayer.push_back( std::move( layer ) );
        }
        return bPlayable;
    }

    void SkeletalAnimatorComponent::setClipFolder( string_view folder )
    {
        // 플레이어가 빌린 클립 포인터를 먼저 놓고 표를 비운다.
        _graphPlayer.stop();
        _clipFolder = string{ folder };
        _mapClip.clear();
        _mapTrackToBone.clear();
        // 시작 때 요청해 둔 클립은 옛 폴더의 것이다 — 버리고 새 폴더에서 그 자리에서 읽는다(동기 API).
        const bool bWasPending = _bClipsPending == SW_TRUE;
        _listClipLoad.clear();
        _bClipsPending = SW_FALSE;
        applyGraphPlayables();
        if ( bWasPending )
            runDeferredPlayback();
    }

    void SkeletalAnimatorComponent::setAnimationGraphPath( string_view path )
    {
        _animationGraphPath = string{ path };
        loadGraph();
    }

    string SkeletalAnimatorComponent::makeClipPath( const hashed_string& clipName ) const
    {
        const string fileName = StringUtil::toLower( clipName.c_str() ) + string( AnimationClip::kExtension );
        return _clipFolder.empty() ? fileName : FileUtil::joinPath( _clipFolder, fileName );
    }

    bool SkeletalAnimatorComponent::preloadClip( const hashed_string& clipName )
    {
        if ( clipName.empty() )
            return false;
        if ( _mapClip.find( clipName ) != _mapClip.end() )
            return true;
        shared_ptr<const AnimationClip> clip = SharedAssetCache<AnimationClip>::acquire( makeClipPath( clipName ) );
        if ( clip == nullptr )
            return false;
        _mapClip.emplace( clipName, std::move( clip ) );
        return true;
    }

    const AnimationClip* SkeletalAnimatorComponent::findClip( const hashed_string& name ) const
    {
        const auto it = _mapClip.find( name );
        return ( it != _mapClip.end() ) ? it->second.get() : nullptr;
    }

    bool SkeletalAnimatorComponent::play( const hashed_string& stateName, bool bLoop, float32 blendSeconds )
    {
        // 시작 때 요청한 클립을 읽는 중이다 — 그래프가 아직 없으니 재생을 미루고, 다 읽히면 한다(`runDeferredPlayback`). 없는 클립은 그때 오류가 난다.
        if ( _bClipsPending == SW_TRUE )
        {
            _deferredPlayState = stateName;
            _deferredPlayBlend = blendSeconds;
            _bDeferredPlayLoop = bLoop ? SW_TRUE : SW_FALSE;
            _bPlayDeferred     = SW_TRUE;
            return true;
        }
        const AnimationGraphNode* pNode = stateName.empty() ? _graph.findEntryNode() : _graph.findNodeByName( stateName.c_str() );
        // 블렌드 스페이스 노드의 표본은 그래프를 읽을 때 읽었다. 가중치와 길이는 지금 파라미터로 맞춰 둔다(길이 0 이면 재생되지 않는다).
        if ( pNode != nullptr && pNode->hasBlendSpace() )
            updateBlendSpaceWeights();
        else
            (void)preloadClip( pNode != nullptr ? hashed_string( pNode->_name ) : stateName ); // 못 찾으면 아래 play 가 오류를 남긴다
        const bool bFound = _graphPlayer.play( stateName, bLoop, blendSeconds < 0.0f ? _blendSeconds : blendSeconds );
        _currentState     = _graphPlayer.getCurrentStateName().c_str();
        _stateTime        = 0.0f;
        if ( bFound == false && stateName.empty() == false )
            SW_LOG_ERROR( "Animation clip '%#' was not found in '%#'", stateName.c_str(), _clipFolder.c_str() );
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
        return bFound;
    }

    bool SkeletalAnimatorComponent::seekCurrentTime( float32 seconds )
    {
        const IAnimationPlayable* pPlaying = _graphPlayer.getPlayer().getCurrentPlayable();
        if ( pPlaying == nullptr )
        {
            SW_LOG_ERROR( "Cannot seek to %# s - nothing is playing", seconds );
            return false;
        }
        const float32 playLength = pPlaying->getPlayLength();
        const bool    bFinite    = MathUtil::isNan( seconds ) == false && MathUtil::isInfinite( seconds ) == false;
        const bool    bInRange   = seconds >= 0.0f && seconds <= playLength;
        if ( bFinite == false || bInRange == false )
        {
            SW_LOG_ERROR( "Cannot seek to %# s - the time must be within 0..%# s", seconds, playLength );
            return false;
        }
        _graphPlayer.getPlayer().setCurrentTime( seconds );
        _stateTime = seconds;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
        return true;
    }

    void SkeletalAnimatorComponent::stop()
    {
        _graphPlayer.stop();
        _currentState.clear();
        _stateTime = 0.0f;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    int32 SkeletalAnimatorComponent::addLayer( const AnimationLayerDesc& desc )
    {
        if ( preloadClip( desc._clipName ) == false )
        {
            SW_LOG_ERROR( "Animation layer clip '%#' was not found in '%#'", desc._clipName.c_str(), _clipFolder.c_str() );
            return -1;
        }
        LayerState layer{};
        layer._desc = desc;
        layer._clip = _mapClip[desc._clipName];
        layer._player.play( layer._clip.get(), desc._bLoop == SW_TRUE );
        _listLayer.push_back( std::move( layer ) );
        return static_cast<int32>( _listLayer.size() - 1 );
    }

    void SkeletalAnimatorComponent::setLayerWeight( uint32 layerIndex, float32 weight )
    {
        if ( layerIndex < _listLayer.size() )
            _listLayer[layerIndex]._desc._weight = MathUtil::clamp( weight, 0.0f, 1.0f );
    }

    void SkeletalAnimatorComponent::restartLayer( uint32 layerIndex )
    {
        if ( layerIndex >= _listLayer.size() )
            return;
        LayerState& layer = _listLayer[layerIndex];
        layer._player.play( layer._clip.get(), layer._desc._bLoop == SW_TRUE );
        // 반감기가 있으면 다음 평가가 끊긴 레이어 포즈에서 새 포즈로 이어 간다(얹은 적이 없으면 이어 갈 포즈가 없다).
        layer._bRestartPending = ( layer._desc._halfLifeSeconds > 0.0f && layer._history.findLast() != nullptr ) ? SW_TRUE : SW_FALSE;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    void SkeletalAnimatorComponent::clearLayers()
    {
        _listLayer.clear();
    }

    bool SkeletalAnimatorComponent::setGraphLayerWeight( const hashed_string& layerName, float32 weight )
    {
        for ( LayerState& layer : _listGraphLayer )
        {
            if ( layer._pGraphLayer->_name != layerName )
                continue;
            if ( layer._pGraphLayer->_weightParameter.empty() == false )
            {
                SW_LOG_ERROR( "Animation graph layer '%#' takes its weight from parameter '%#'; set the parameter instead", layerName.c_str(),
                              layer._pGraphLayer->_weightParameter.c_str() );
                return false;
            }
            layer._desc._weight = MathUtil::clamp( weight, 0.0f, 1.0f );
            return true;
        }
        SW_LOG_ERROR( "Animation graph layer '%#' was not found in '%#'", layerName.c_str(), _animationGraphPath.c_str() );
        return false;
    }

    void SkeletalAnimatorComponent::setSequencerOverride( shared_ptr<const AnimationClip> clip, float32 time, float32 weight )
    {
        _sequencerClip   = std::move( clip );
        _sequencerTime   = time;
        _sequencerWeight = ( _sequencerClip != nullptr ) ? MathUtil::clamp( weight, 0.0f, 1.0f ) : 0.0f;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    void SkeletalAnimatorComponent::setSyncGroup( const hashed_string& groupName, float32 weight )
    {
        _syncGroup  = groupName.c_str();
        _syncWeight = weight;
    }

    float32 SkeletalAnimatorComponent::getCurveValue( const hashed_string& curveName ) const
    {
        return _curves.getValue( curveName );
    }

    bool SkeletalAnimatorComponent::describeSharedPose( AnimationSharedPoseRequest& outRequest ) const
    {
        const AnimationPlayer&    player   = _graphPlayer.getPlayer();
        const IAnimationPlayable* pPlaying = player.getCurrentPlayable();
        // 끊긴 페이드를 이어 섞는 중 · inertialization · 레이어 · 블렌드 스페이스(파라미터가 캐릭터마다 다르다) · 시퀀서 덮어쓰기는 캐릭터마다 다르다.
        // 반복하지 않는 클립은 시작 시각이 캐릭터마다 달라 칸으로 묶지 않는다. 반복 클립 둘 사이의 페이드는 섞기 묶음으로 나눈다(`AnimationCrowd::joinBlendBucket`).
        const IAnimationPlayable* pNext        = player.getNextPlayable();
        const bool                bLayered     = _listLayer.empty() == false || _listGraphLayer.empty() == false;
        const bool                bNotClip     = pPlaying == nullptr || findBlendSpaceState( pPlaying ) != nullptr;
        const bool                bNextNotClip = pNext != nullptr && ( findBlendSpaceState( pNext ) != nullptr || player.isNextLooping() == false );
        // inertialization 중이면 남은 차이가 유닛마다 달라 묶음의 포즈와 같지 않다.
        const bool bInertializing = _inertializer.isActive() || _bInertializationPending == SW_TRUE;
        const bool bNotShareable  = bNotClip || bNextNotClip || bInertializing || _bCarryingOver == SW_TRUE || player.isCurrentLooping() == false || bLayered ||
                                   _sequencerWeight > 0.0f;
        if ( bNotShareable )
            return false;
        outRequest._pClip             = static_cast<const AnimationClip*>( pPlaying );
        outRequest._time              = player.getCurrentTime();
        outRequest._playRate          = _playRate * player.getSpeed();
        outRequest._bAnchorRootMotion = _bExtractRootMotion;
        outRequest._pNextClip         = static_cast<const AnimationClip*>( pNext );
        outRequest._nextTime          = pNext != nullptr ? player.getNextTime() : 0.0f;
        outRequest._blendAlpha        = pNext != nullptr ? player.getBlendAlpha() : 0.0f;
        outRequest._fadeSeconds       = pNext != nullptr ? player.getFadeDuration() : 0.0f;
        return true;
    }

    bool SkeletalAnimatorComponent::isAnimationActive() const
    {
        const bool bPlaying = _graphPlayer.getPlayer().getCurrentPlayable() != nullptr;
        const bool bLayered = _listLayer.empty() == false || _listGraphLayer.empty() == false;
        return bPlaying || bLayered || _sequencerWeight > 0.0f;
    }

    void SkeletalAnimatorComponent::collectActivePlayables( vector<const IAnimationPlayable*>& outListPlayable ) const
    {
        outListPlayable.clear();
        const AnimationPlayer& player = _graphPlayer.getPlayer();
        for ( const IAnimationPlayable* pPlayable : { player.getCurrentPlayable(), player.getNextPlayable() } )
        {
            if ( pPlayable == nullptr )
                continue;
            outListPlayable.push_back( pPlayable );
            // 블렌드 스페이스 상태의 알림은 표본 클립이 출처다 — 그 클립이 빠지면 받는 쪽이 열린 구간 알림을 닫는다.
            const SkeletalBlendSpaceState* pState = findBlendSpaceState( pPlayable );
            if ( pState != nullptr )
                pState->appendNotifySources( outListPlayable );
        }
        for ( const LayerState& layer : _listGraphLayer )
        {
            if ( layer._clip != nullptr )
                outListPlayable.push_back( layer._clip.get() );
        }
        for ( const LayerState& layer : _listLayer )
        {
            if ( layer._clip != nullptr )
                outListPlayable.push_back( layer._clip.get() );
        }
    }

    void SkeletalAnimatorComponent::dispatchNotifies()
    {
        if ( _pNotifyListener == nullptr )
            return;
        collectActivePlayables( _listActivePlayable );
        AnimationNotifyFrame frame{};
        frame._listFired          = vector_reference<const AnimationFiredNotify>{ _listFiredNotify.data(), _listFiredNotify.size() };
        frame._listActivePlayable = vector_reference<const IAnimationPlayable* const>{ _listActivePlayable.data(), _listActivePlayable.size() };
        frame._deltaSeconds       = _lastDeltaSeconds;
        frame._bFromTick          = SW_FALSE;
        _pNotifyListener->onAnimationNotifiesFired( frame );
    }

    Subscription SkeletalAnimatorComponent::registerRootMotionModifier( IRootMotionModifier* pModifier )
    {
        // 수정자는 캐릭터 하나에 붙은 컴포넌트가 하나씩 올린다(수 개 이하). 호출 순서가 등록 순서라 vector 를 둔다. 구독 하나가 등록 하나다.
        if ( pModifier == nullptr )
            return {};
        _listRootMotionModifier.push_back( pModifier );
        return _rootMotionModifierSource.issue( pModifier );
    }

    void SkeletalAnimatorComponent::applyRootMotion( SkeletalMeshComponent& unit )
    {
        // 루트 모션은 오브젝트 트랜스폼에 쓴다 — 게임 스레드에서, 모든 단계가 끝난 뒤다(아래 트랜스폼 플러시가 반영한다).
        if ( _bExtractRootMotion == SW_FALSE || unit.getOwner() == nullptr )
            return;
        SceneComponent* pRoot = unit.getOwner()->getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return;
        // 움직임은 캐릭터 공간(루트 본의 부모 = 모델 공간)의 값이다 — 오브젝트의 월드 회전 · 배율로 돌려 월드 이동으로 바꾼다.
        const AnimationPlayer&   player = _graphPlayer.getPlayer();
        const AnimationTimeStep& step   = player.getCurrentStep();
        RootMotionFrame          frame{};
        frame._worldTranslation = float3::transformVector( _rootMotionDelta._translation, pRoot->getWorldMatrix() );
        frame._rotation         = _rootMotionDelta._rotation;
        frame._pClip            = player.getCurrentPlayable();
        frame._previousClipTime = step._previousTime;
        frame._clipTime         = step._currentTime;
        frame._deltaSeconds     = _lastDeltaSeconds;
        frame._bClipWrapped     = step._wrapCount > 0 ? SW_TRUE : SW_FALSE;
        for ( IRootMotionModifier* pModifier : _listRootMotionModifier )
        {
            pModifier->modifyRootMotion( *this, frame );
        }

        const bool bMoves = frame._worldTranslation.getLengthSquared() > 0.0f;
        if ( bMoves )
        {
            CharacterControllerComponent* pController =
                _bRootMotionThroughController == SW_TRUE ? unit.getOwner()->getComponent<CharacterControllerComponent>() : nullptr;
            if ( pController != nullptr && pController->hasBegunPlay() )
                pController->addRootMotionDisplacement( frame._worldTranslation );
            else
                pRoot->setWorldPosition( pRoot->getWorldPosition() + frame._worldTranslation );
        }
        if ( frame._rotation != quaternion::Identity )
        {
            const quaternion turned = ( quaternion::makeFromYawPitchRoll( pRoot->getLocalRotation() ) * frame._rotation ).normalize();
            pRoot->setLocalRotation( turned.getEulerAngles() );
        }
    }

    void SkeletalAnimatorComponent::advanceTime( const AnimationFrameContext& context )
    {
        const float32 deltaSeconds = context._deltaSeconds * _playRate;
        _lastDeltaSeconds          = deltaSeconds;
        // 지난 프레임에 포즈가 화면에 보이지 않았거나(화면 밖 · 숨김 · 군중 묶음이 대신 냈다) 쉬어서 시간 단계가 끊겼으면, 기록한 포즈는 아무도 보지 않은 옛 포즈다.
        // 다음 전이가 거기서 출발하면 다시 보일 때 옛 포즈로 튄다 — 무효로 표시해 새 포즈에서 바로 시작한다.
        const bool bHistoryBroken = _bPoseShownThisFrame == SW_FALSE || context._frameIndex != _lastTimeFrameIndex + 1;
        _lastTimeFrameIndex       = context._frameIndex;
        // 포즈를 만들지 않아도 화면 안이면 지난 포즈(또는 그 보간)가 그대로 보인다(LOD 주기). 만들면 `populateBasePose` 가 참으로 둔다.
        _bPoseShownThisFrame = context._bPoseNeeded == SW_FALSE && context._bOnScreen == SW_TRUE ? SW_TRUE : SW_FALSE;
        _baseHistory.advance( deltaSeconds );
        if ( bHistoryBroken )
            _baseHistory.invalidate();
        for ( vector<LayerState>* pListLayer : { &_listGraphLayer, &_listLayer } )
        {
            for ( LayerState& layer : *pListLayer )
            {
                layer._history.advance( deltaSeconds );
                if ( bHistoryBroken )
                    layer._history.invalidate();
            }
        }
        _listFiredNotify.clear();
        updateBlendSpaceWeights();
        _graphPlayer.update( deltaSeconds, &_parameters, &_listFiredNotify );
        for ( LayerState& layer : _listGraphLayer )
        {
            if ( layer._pGraphLayer->_weightParameter.empty() == false )
                layer._desc._weight = MathUtil::saturate( _parameters.getFloat( layer._pGraphLayer->_weightParameter ) );
            layer._player.update( deltaSeconds, &_listFiredNotify );
        }
        for ( LayerState& layer : _listLayer )
        {
            layer._player.update( deltaSeconds, &_listFiredNotify );
        }

        // 페이드가 다른 페이드로 끊기면 플레이어는 섞이던 한 칸을 버린다(최대 절반이 한 프레임에 사라져 튄다). 끊긴 순간의 포즈(지난 기본 포즈)를
        // 새 페이드 길이 동안 지금 포즈 위에서 사라지게 해 이어 붙인다 — 상태가 짧게 오가도 포즈는 이어진다.
        const AnimationPlayer& player = _graphPlayer.getPlayer();
        if ( player.getInterruptCount() != _seenInterruptCount )
        {
            _seenInterruptCount    = player.getInterruptCount();
            const Pose* pShownPose = _baseHistory.findLast();
            if ( pShownPose != nullptr )
            {
                _carryOverPose     = *pShownPose;
                _carryOverElapsed  = 0.0f;
                _carryOverDuration = MathUtil::max( player.getFadeDuration(), 1.0e-3f );
                _bCarryingOver     = SW_TRUE;
            }
        }
        else if ( _bCarryingOver == SW_TRUE )
        {
            _carryOverElapsed += deltaSeconds;
            if ( _carryOverElapsed >= _carryOverDuration )
                _bCarryingOver = SW_FALSE;
        }

        // inertialization 전이 — 새 포즈를 샘플한 뒤(`populateBasePose`) 지난 포즈와의 차이를 잡는다. 그 차이가 이어 주므로 이어 섞기는 그만둔다.
        if ( player.getInertializationCount() != _seenInertializationCount )
        {
            _seenInertializationCount = player.getInertializationCount();
            _bInertializationPending  = _baseHistory.findLast() != nullptr ? SW_TRUE : SW_FALSE;
            _bCarryingOver            = SW_FALSE;
        }
        else
        {
            _inertializer.update( deltaSeconds );
        }

        const IAnimationPlayable* pCurrent = player.getCurrentPlayable();
        const IAnimationPlayable* pNext    = player.getNextPlayable();
        const float32             alpha    = player.getBlendAlpha();

        // 루트 모션 — 두 칸의 움직임을 섞임 가중치로 섞는다.
        _rootMotionDelta = BoneTransform{};
        if ( _bExtractRootMotion == SW_TRUE && pCurrent != nullptr )
        {
            _rootMotionDelta = computePlayableRootMotion( *pCurrent, player.getCurrentStep() );
            if ( pNext != nullptr )
                _rootMotionDelta = BoneTransform::blend( _rootMotionDelta, computePlayableRootMotion( *pNext, player.getNextStep() ), alpha );
        }

        // 커브 — 두 칸의 값을 섞는다. 한쪽에만 있는 커브는 그쪽 가중치만큼입니다.
        _curves.clear();
        const IAnimationPlayable* arrPlayable[2] = { pCurrent, pNext };
        const float32             arrWeight[2]   = { 1.0f - alpha, alpha };
        const float32             arrTime[2]     = { player.getCurrentTime(), player.getNextTime() };
        for ( uint32 slotIndex = 0; slotIndex < 2; ++slotIndex )
        {
            if ( arrPlayable[slotIndex] != nullptr )
                accumulatePlayableCurves( *arrPlayable[slotIndex], arrTime[slotIndex], arrWeight[slotIndex] );
        }

        // 핫 리로드가 복원할 상태 · 시각을 PROPERTY 에 비춘다(게임 스레드가 아니어도 이 컴포넌트만의 칸이다).
        _stateTime = player.getCurrentTime();
    }

    const vector<int32>& SkeletalAnimatorComponent::getTrackMap( const AnimationClip& clip, const Skeleton& skeleton )
    {
        if ( _pTrackMapSkeleton != &skeleton )
        {
            _mapTrackToBone.clear();
            _pTrackMapSkeleton = &skeleton;
            for ( LayerState& layer : _listGraphLayer )
            {
                layer._pMaskSkeleton = nullptr;
            }
            for ( LayerState& layer : _listLayer )
            {
                layer._pMaskSkeleton = nullptr;
            }
        }
        vector<int32>& listBone = _mapTrackToBone[&clip];
        if ( listBone.size() != clip.getTrackCount() )
            clip.makeTrackToBoneMap( skeleton, listBone );
        return listBone;
    }

    void SkeletalAnimatorComponent::sampleClipIntoPose( const AnimationClip& clip, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask )
    {
        const vector<int32>& listTrackToBone = getTrackMap( clip, skeleton );
        // 본 LOD — 빠진 본의 트랙은 코덱이 풀지 않고(ACL 트랙 건너뛰기) 포즈에도 옮기지 않는다(레퍼런스로 부모를 따른다).
        const uint8* pTrackMask = nullptr;
        if ( pBoneMask != nullptr )
        {
            _listScratchTrackMask.resize( listTrackToBone.size() );
            for ( size_t trackIndex = 0; trackIndex < listTrackToBone.size(); ++trackIndex )
            {
                const int32 boneIndex             = listTrackToBone[trackIndex];
                _listScratchTrackMask[trackIndex] = ( boneIndex >= 0 && pBoneMask[static_cast<uint32>( boneIndex )] != 0 ) ? SW_TRUE : SW_FALSE;
            }
            pTrackMask = _listScratchTrackMask.data();
        }
        (void)clip.samplePose( time, listTrackToBone, inoutPose, _scratchTrackPose, _bExtractRootMotion == SW_TRUE, pTrackMask );
    }

    void SkeletalAnimatorComponent::refreshLayerMask( LayerState& layer, const Skeleton& skeleton )
    {
        if ( layer._pMaskSkeleton == &skeleton )
            return;
        layer._pMaskSkeleton   = &skeleton;
        const uint32 boneCount = skeleton.getBoneCount();
        if ( layer._pBoneMask != nullptr )
        {
            // 데이터 마스크(roots 퍼뜨림 + bones 예외)다. 스켈레톤에 없는 본을 가리키면 모두 0 이라 이 레이어는 아무것도 바꾸지 않는다.
            if ( layer._pBoneMask->computeBoneWeights( skeleton, layer._listBoneWeight ) == false )
                SW_LOG_ERROR( "Animation graph layer '%#' is off: bone mask '%#' does not fit the skeleton", layer._pGraphLayer->_name.c_str(), layer._pBoneMask->_name.c_str() );
        }
        else
        {
            layer._listBoneWeight.assign( boneCount, layer._desc._maskRootBone.empty() ? 1.0f : 0.0f );
            const int32 maskRoot = layer._desc._maskRootBone.empty() ? -1 : skeleton.findBoneIndex( layer._desc._maskRootBone );
            if ( layer._desc._maskRootBone.empty() == false && maskRoot < 0 )
                SW_LOG_WARNING( "Animation layer mask bone '%#' is not in the skeleton", layer._desc._maskRootBone.c_str() );
            // 부모가 앞에 있으므로 한 번 훑으면 마스크 본의 자손이 모두 1 이 된다.
            for ( uint32 boneIndex = 0; maskRoot >= 0 && boneIndex < boneCount; ++boneIndex )
            {
                const int32 parentIndex          = skeleton.getBone( boneIndex )._parentIndex;
                const bool  bInMask              = static_cast<int32>( boneIndex ) == maskRoot || ( parentIndex >= 0 && layer._listBoneWeight[static_cast<uint32>( parentIndex )] > 0.0f );
                layer._listBoneWeight[boneIndex] = bInMask ? 1.0f : 0.0f;
            }
        }
        // 가산 기준은 레이어 클립의 시각 0 본 포즈다.
        layer._additiveReference.setToReference( skeleton );
        if ( layer._clip != nullptr )
            sampleClipIntoPose( *layer._clip, 0.0f, skeleton, layer._additiveReference );
    }

    void SkeletalAnimatorComponent::updateBlendSpaceWeights()
    {
        AnimationPlayer& player = _graphPlayer.getPlayer();
        for ( const unique_ptr<SkeletalBlendSpaceState>& state : _listBlendSpace )
        {
            const float32 previousLength = state->getPlayLength();
            state->updateWeights( _parameters );
            const float32 length = state->getPlayLength();
            // 한 바퀴 길이가 바뀌면 같은 시각이 다른 위상이 된다(걷기 → 달리기에서 발이 튄다). 현재 슬롯은 위상을 지키게 시각을 옮긴다.
            const bool bRescale = player.getCurrentPlayable() == state.get() && previousLength > 0.0f && length > 0.0f && previousLength != length;
            if ( bRescale )
                player.setCurrentTime( player.getCurrentTime() * ( length / previousLength ) );
        }
    }

    const SkeletalBlendSpaceState* SkeletalAnimatorComponent::findBlendSpaceState( const IAnimationPlayable* pPlayable ) const
    {
        for ( const unique_ptr<SkeletalBlendSpaceState>& state : _listBlendSpace )
        {
            if ( state.get() == pPlayable )
                return state.get();
        }
        return nullptr;
    }

    void SkeletalAnimatorComponent::samplePlayableIntoPose( const IAnimationPlayable& playable, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask )
    {
        const SkeletalBlendSpaceState* pState = findBlendSpaceState( &playable );
        if ( pState == nullptr )
        {
            // 이 애니메이터가 상태 기계에 주는 재생할 것은 블렌드 스페이스 상태가 아니면 클립뿐이다(`SkeletalAnimatorBinding::findPlayable`).
            sampleClipIntoPose( static_cast<const AnimationClip&>( playable ), time, skeleton, inoutPose, pBoneMask );
            return;
        }
        const uint32 sampleCount = pState->getSampleCount();
        if ( _listScratchSamplePose.size() < sampleCount )
            _listScratchSamplePose.resize( sampleCount );
        _listScratchSamplePosePointer.clear();
        _listScratchSampleWeight.clear();
        // 가중치가 0 인 표본은 샘플하지 않는다(1D 는 많아야 둘, 2D 는 표본 위면 하나).
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            const float32 weight = pState->getSampleWeight( sampleIndex );
            if ( weight <= 0.0f )
                continue;
            Pose& samplePose = _listScratchSamplePose[sampleIndex];
            samplePose.setToReference( skeleton );
            sampleClipIntoPose( pState->getSampleClip( sampleIndex ), pState->computeSampleTime( sampleIndex, time ), skeleton, samplePose, pBoneMask );
            _listScratchSamplePosePointer.push_back( &samplePose );
            _listScratchSampleWeight.push_back( weight );
        }
        if ( _listScratchSamplePosePointer.empty() )
            return;
        Pose::blendWeighted( _listScratchSamplePosePointer.data(), _listScratchSampleWeight.data(), static_cast<uint32>( _listScratchSamplePosePointer.size() ), inoutPose );
    }

    BoneTransform SkeletalAnimatorComponent::computePlayableRootMotion( const IAnimationPlayable& playable, const AnimationTimeStep& step ) const
    {
        const SkeletalBlendSpaceState* pState = findBlendSpaceState( &playable );
        if ( pState == nullptr )
            return static_cast<const AnimationClip&>( playable ).computeRootMotionDelta( step );
        // 표본마다 같은 위상 구간의 움직임을 구해 가중치로 섞는다(누적 평균: 앞의 합과 새 값을 새 가중치의 몫만큼).
        BoneTransform result{};
        float32       accumulatedWeight = 0.0f;
        for ( uint32 sampleIndex = 0; sampleIndex < pState->getSampleCount(); ++sampleIndex )
        {
            const float32 weight = pState->getSampleWeight( sampleIndex );
            if ( weight <= 0.0f )
                continue;
            AnimationTimeStep sampleStep = step;
            sampleStep._previousTime     = pState->computeSampleTime( sampleIndex, step._previousTime );
            sampleStep._currentTime      = pState->computeSampleTime( sampleIndex, step._currentTime );
            const BoneTransform delta    = pState->getSampleClip( sampleIndex ).computeRootMotionDelta( sampleStep );
            accumulatedWeight += weight;
            result = BoneTransform::blend( result, delta, weight / accumulatedWeight );
        }
        return result;
    }

    void SkeletalAnimatorComponent::accumulatePlayableCurves( const IAnimationPlayable& playable, float32 time, float32 weight )
    {
        const SkeletalBlendSpaceState* pState = findBlendSpaceState( &playable );
        if ( pState == nullptr )
        {
            accumulateClipCurves( static_cast<const AnimationClip&>( playable ), time, weight );
            return;
        }
        for ( uint32 sampleIndex = 0; sampleIndex < pState->getSampleCount(); ++sampleIndex )
        {
            const float32 sampleWeight = pState->getSampleWeight( sampleIndex );
            if ( sampleWeight > 0.0f )
                accumulateClipCurves( pState->getSampleClip( sampleIndex ), pState->computeSampleTime( sampleIndex, time ), weight * sampleWeight );
        }
    }

    void SkeletalAnimatorComponent::accumulateClipCurves( const AnimationClip& clip, float32 time, float32 weight )
    {
        for ( const AnimationClipCurve& curve : clip.getCurves() )
        {
            _curves.add( curve._name, curve.evaluate( time ) * weight );
        }
    }

    void SkeletalAnimatorComponent::applyLayer( LayerState& layer, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask )
    {
        // inertialization 의 시간은 가중치가 0 이어도 흐른다(다시 켰을 때 오래된 오프셋이 남지 않게).
        layer._inertializer.update( _lastDeltaSeconds );
        if ( layer._clip == nullptr || layer._desc._weight <= 0.0f )
        {
            layer._history.invalidate(); // 얹지 않은 프레임이다 — 다시 켰을 때 옛 레이어 포즈에서 출발하지 않는다
            return;
        }
        refreshLayerMask( layer, skeleton );
        _scratchLayerPose.setToReference( skeleton );
        sampleClipIntoPose( *layer._clip, layer._player.getCurrentTime(), skeleton, _scratchLayerPose, pBoneMask );
        if ( layer._desc._halfLifeSeconds > 0.0f )
        {
            if ( layer._bRestartPending == SW_TRUE )
            {
                // 출발은 끊기기 직전에 화면에 얹힌 레이어 포즈(와 그 전 기록), 도착은 다시 시작한 첫 포즈다. 출발이 무효(화면 밖 프레임이 끼었다)면 새 포즈에서 시작한다.
                // 실패(본 수가 바뀜)는 begin 이 오류를 남기고 즉시 새 포즈다.
                const Pose* pSource = layer._history.findLast();
                if ( pSource == nullptr )
                    layer._inertializer.reset();
                else
                    (void)layer._inertializer.begin( *pSource, layer._history.findPrevious(), _scratchLayerPose, nullptr, layer._history.getIntervalSeconds(),
                                                     layer._desc._halfLifeSeconds );
                layer._bRestartPending = SW_FALSE;
            }
            layer._inertializer.apply( _scratchLayerPose );
            layer._history.record( _scratchLayerPose );
        }
        if ( layer._desc._blend == AnimationLayerBlend::Override )
        {
            Pose::blendMasked( inoutPose, _scratchLayerPose, layer._desc._weight, layer._listBoneWeight.data(), inoutPose );
        }
        else
        {
            Pose::makeAdditive( _scratchLayerPose, layer._additiveReference, _scratchPose );
            inoutPose.applyAdditive( _scratchPose, layer._desc._weight, layer._listBoneWeight.data() );
        }
    }

    void SkeletalAnimatorComponent::populateBasePose( SkeletalMeshComponent& unit )
    {
        const Skeleton&           skeleton = unit.getSkeleton();
        Pose&                     pose     = unit.getLocalPose();
        const AnimationPlayer&    player   = _graphPlayer.getPlayer();
        const IAnimationPlayable* pCurrent = player.getCurrentPlayable();
        const IAnimationPlayable* pNext    = player.getNextPlayable();
        const uint8*              pMask    = unit.findBoneLODMask();

        // pose 는 유닛이 레퍼런스(또는 리더 포즈)로 채워 둔 상태다. 지금 칸을 그 위에 샘플하고, 페이드 중이면 다음 칸과 섞는다.
        if ( pCurrent != nullptr )
            samplePlayableIntoPose( *pCurrent, player.getCurrentTime(), skeleton, pose, pMask );
        if ( pNext != nullptr )
        {
            _scratchPose.setToReference( skeleton );
            samplePlayableIntoPose( *pNext, player.getNextTime(), skeleton, _scratchPose, pMask );
            Pose::blend( pose, _scratchPose, player.getBlendAlpha(), pose );
        }
        if ( _bCarryingOver == SW_TRUE && _carryOverPose.getBoneCount() == pose.getBoneCount() )
            Pose::blend( _carryOverPose, pose, MathUtil::saturate( _carryOverElapsed / _carryOverDuration ), pose );
        if ( _bInertializationPending == SW_TRUE )
        {
            _bInertializationPending = SW_FALSE;
            // 전이와 이 포즈 사이에 화면 밖 프레임이 끼었으면 출발 포즈가 없다 — 아무도 보지 않았으므로 새 포즈에서 바로 시작한다.
            const Pose* pSource = _baseHistory.findLast();
            if ( pSource == nullptr )
                _inertializer.reset();
            else // 본 수가 다르면(스켈레톤이 바뀜) begin 이 오류를 남기고 섞지 않는다 — 새 포즈를 그대로 쓴다.
                (void)_inertializer.begin( *pSource, _baseHistory.findPrevious(), pose, nullptr, _baseHistory.getIntervalSeconds(), player.getInertializationHalfLife() );
        }
        _inertializer.apply( pose );
        _baseHistory.record( pose );
        _bPoseShownThisFrame = SW_TRUE;

        // 그래프 레이어(데이터)를 먼저, 코드 레이어(게임이 그 위에 덮는 것)를 나중에 얹는다.
        for ( LayerState& layer : _listGraphLayer )
        {
            applyLayer( layer, skeleton, pose, pMask );
        }
        for ( LayerState& layer : _listLayer )
        {
            applyLayer( layer, skeleton, pose, pMask );
        }

        if ( _sequencerClip != nullptr && _sequencerWeight > 0.0f )
        {
            _scratchPose.setToReference( skeleton );
            sampleClipIntoPose( *_sequencerClip, _sequencerTime, skeleton, _scratchPose, pMask );
            Pose::blend( pose, _scratchPose, _sequencerWeight, pose );
        }

        // 모프 타깃 커브 — 이름이 그리는 메시의 모프 타깃과 같은 커브는 그 가중치가 된다(임포트가 glTF weights 채널을 타깃 이름의 커브로 싣는다).
        if ( unit.getMorphTargetCount() == 0 )
            return;
        const vector<hashed_string>& listCurveName  = _curves.getNames();
        const vector<float32>&       listCurveValue = _curves.getValues();
        for ( size_t curveIndex = 0; curveIndex < listCurveName.size(); ++curveIndex )
        {
            const int32 targetIndex = unit.findMorphTargetIndex( listCurveName[curveIndex] );
            if ( targetIndex >= 0 )
                unit.addMorphWeight( static_cast<uint32>( targetIndex ), listCurveValue[curveIndex] );
        }
    }
} // namespace sw
