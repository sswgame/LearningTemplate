#include "pch.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Resource/AnimationAssetCache.h"

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

    void SkeletalAnimatorBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner.advanceTime( context );
        else if ( phase == AnimationPhase::BasePose )
            _owner.buildBasePose( unit );
    }

    AnimPlayer* SkeletalAnimatorBinding::findSyncPlayer( hashed_string& outGroupName, float32& outWeight )
    {
        if ( _owner._syncGroup.empty() )
            return nullptr;
        outGroupName = hashed_string( _owner._syncGroup );
        outWeight    = _owner._syncWeight;
        return &_owner._graphPlayer.getPlayer();
    }

    void SkeletalAnimatorBinding::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        // 루트 모션은 오브젝트 트랜스폼에 쓴다 — 게임 스레드에서, 모든 단계가 끝난 뒤다(아래 트랜스폼 플러시가 반영한다).
        if ( _owner._bExtractRootMotion == SW_FALSE || unit.getOwner() == nullptr )
            return;
        const BoneTransform& delta = _owner._rootMotionDelta;
        if ( delta._translation.getLengthSquared() <= 0.0f && delta._rotation == quaternion::Identity )
            return;
        SceneComponent* pRoot = unit.getOwner()->getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return;
        // 움직임은 캐릭터 공간(루트 본의 부모 = 모델 공간)의 값이다 — 오브젝트의 로컬 회전 · 스케일로 돌려 부모 공간 이동으로 바꾼다.
        const float3     localRotation = pRoot->getLocalRotation();
        const float4x4   orientation   = float4x4::createTrs( float3{}, localRotation, pRoot->getLocalScale() );
        const float3     parentMove    = float3::transformVector( delta._translation, orientation );
        const quaternion turned        = ( quaternion::createFromYawPitchRoll( localRotation ) * delta._rotation ).normalize();
        pRoot->setLocalPosition( pRoot->getLocalPosition() + parentMove );
        pRoot->setLocalRotation( turned.getEulerAngles() );
    }

    void SkeletalAnimatorBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    bool SkeletalAnimatorBinding::describeSharedPose( AnimSharedPoseRequest& outRequest ) const
    {
        return _owner.describeSharedPose( outRequest );
    }

    shared_ptr<const AnimClip> SkeletalAnimatorBinding::findSharedPoseClip( const AnimClip* pClip ) const
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
        for ( const AnimFiredNotify& notify : _owner._listFiredNotify )
            inoutState._listNotify.push_back( notify._name );
        inoutState._listCurveName.insert( inoutState._listCurveName.end(), _owner._listCurveName.begin(), _owner._listCurveName.end() );
        inoutState._listCurveValue.insert( inoutState._listCurveValue.end(), _owner._listCurveValue.begin(), _owner._listCurveValue.end() );
        inoutState._rootMotionTranslation = _owner._rootMotionDelta._translation;
        inoutState._rootMotionRotation    = _owner._rootMotionDelta._rotation;
    }

    const IAnimPlayable* SkeletalAnimatorBinding::findPlayable( const hashed_string& name ) const
    {
        return _owner.findClip( name );
    }

    SkeletalAnimatorComponent::SkeletalAnimatorComponent()
        : _animGraphPath{}
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
        , _mapTrackToBone{}
        , _pTrackMapSkeleton{ nullptr }
        , _listLayer{}
        , _listFiredNotify{}
        , _listCurveName{}
        , _listCurveValue{}
        , _scratchPose{}
        , _scratchTrackPose{}
        , _scratchLayerPose{}
        , _listScratchTrackMask{}
        , _sequencerClip{}
        , _rootMotionDelta{}
        , _pUnit{ nullptr }
        , _sequencerTime{ 0.0f }
        , _sequencerWeight{ 0.0f }
        , _initialTime{ 0.0f }
        , _bExtractRootMotion{ SW_FALSE }
        , _bPlayOnBegin{ SW_TRUE }
        , _reserved{ 0 }
    {
        _graphPlayer.setPlayableSource( &_binding );
    }

    SkeletalAnimatorComponent::~SkeletalAnimatorComponent()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
    }

    void SkeletalAnimatorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _graphPlayer.setDefaultBlendSeconds( _blendSeconds );
        loadGraph();
        bindToUnit();
        if ( _bPlayOnBegin == SW_FALSE )
            return;
        // 핫 리로드로 되살아났으면 그 상태 · 시각에서 잇는다. 아니면 처음 상태부터.
        const float32 resumeTime = _stateTime;
        if ( _currentState.empty() == false && play( hashed_string( _currentState ), true, 0.0f ) )
            _graphPlayer.getPlayer().setCurrentTime( resumeTime );
        else if ( play( hashed_string( _initialState ), true, 0.0f ) && _initialTime > 0.0f )
            _graphPlayer.getPlayer().setCurrentTime( _initialTime );
    }

    void SkeletalAnimatorComponent::onEndPlay()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = nullptr;
        Component::onEndPlay();
    }

    void SkeletalAnimatorComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_graphPathName( "_animGraphPath" );
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
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _pUnit->addAnimationPhaseTask( &_binding );
        else
            SW_LOG_WARNING( "'%#': skeletal animator has no SkeletalMeshComponent on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
    }

    void SkeletalAnimatorComponent::loadGraph()
    {
        _graph = AnimGraphAsset{};
        if ( _animGraphPath.empty() == false && _graph.loadFromFile( _animGraphPath ) == false )
            SW_LOG_ERROR( "Animation graph '%#' could not be loaded", _animGraphPath.c_str() );
        _graphPlayer.setGraph( _graph._listNode.empty() ? nullptr : &_graph );
        // 워커의 전이는 읽어 둔 클립만 찾으므로 노드의 클립은 지금(게임 스레드) 읽는다.
        for ( const AnimGraphNode& node : _graph._listNode )
            (void)preloadClip( hashed_string( node._name ) );
    }

    void SkeletalAnimatorComponent::setClipFolder( string_view folder )
    {
        // 플레이어가 빌린 클립 포인터를 먼저 놓고 표를 비운다.
        _graphPlayer.stop();
        _clipFolder = string{ folder };
        _mapClip.clear();
        _mapTrackToBone.clear();
        for ( const AnimGraphNode& node : _graph._listNode )
            (void)preloadClip( hashed_string( node._name ) );
    }

    void SkeletalAnimatorComponent::setAnimGraphPath( string_view path )
    {
        _animGraphPath = string{ path };
        loadGraph();
    }

    string SkeletalAnimatorComponent::makeClipPath( const hashed_string& clipName ) const
    {
        const string fileName = StringUtil::toLower( clipName.c_str() ) + string( AnimClip::kExtension );
        return _clipFolder.empty() ? fileName : FileUtil::joinPath( _clipFolder, fileName );
    }

    bool SkeletalAnimatorComponent::preloadClip( const hashed_string& clipName )
    {
        if ( clipName.empty() )
            return false;
        if ( _mapClip.find( clipName ) != _mapClip.end() )
            return true;
        shared_ptr<const AnimClip> clip = AnimClipCache::acquire( makeClipPath( clipName ) );
        if ( clip == nullptr )
            return false;
        _mapClip.emplace( clipName, std::move( clip ) );
        return true;
    }

    const AnimClip* SkeletalAnimatorComponent::findClip( const hashed_string& name ) const
    {
        const auto it = _mapClip.find( name );
        return ( it != _mapClip.end() ) ? it->second.get() : nullptr;
    }

    bool SkeletalAnimatorComponent::play( const hashed_string& stateName, bool bLoop, float32 blendSeconds )
    {
        const AnimGraphNode* pNode = stateName.empty() ? _graph.findEntryNode() : _graph.findNodeByName( stateName.c_str() );
        (void)preloadClip( pNode != nullptr ? hashed_string( pNode->_name ) : stateName );
        const bool bFound = _graphPlayer.play( stateName, bLoop, blendSeconds < 0.0f ? _blendSeconds : blendSeconds );
        _currentState     = _graphPlayer.getCurrentStateName().c_str();
        _stateTime        = 0.0f;
        if ( bFound == false && stateName.empty() == false )
            SW_LOG_WARNING( "Animation clip '%#' was not found in '%#'", stateName.c_str(), _clipFolder.c_str() );
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
        return bFound;
    }

    void SkeletalAnimatorComponent::stop()
    {
        _graphPlayer.stop();
        _currentState.clear();
        _stateTime = 0.0f;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    int32 SkeletalAnimatorComponent::addLayer( const AnimLayerDesc& desc )
    {
        if ( preloadClip( desc._clipName ) == false )
        {
            SW_LOG_WARNING( "Animation layer clip '%#' was not found in '%#'", desc._clipName.c_str(), _clipFolder.c_str() );
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

    void SkeletalAnimatorComponent::clearLayers()
    {
        _listLayer.clear();
    }

    void SkeletalAnimatorComponent::setSequencerOverride( shared_ptr<const AnimClip> clip, float32 time, float32 weight )
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
        for ( size_t curveIndex = 0; curveIndex < _listCurveName.size(); ++curveIndex )
        {
            if ( _listCurveName[curveIndex] == curveName )
                return _listCurveValue[curveIndex];
        }
        return 0.0f;
    }

    bool SkeletalAnimatorComponent::describeSharedPose( AnimSharedPoseRequest& outRequest ) const
    {
        const AnimPlayer& player   = _graphPlayer.getPlayer();
        const AnimClip*   pCurrent = static_cast<const AnimClip*>( player.getCurrentPlayable() );
        // 섞는 중 · 레이어 · 시퀀서 덮어쓰기는 캐릭터마다 다르다. 반복하지 않는 클립은 시작 시각이 캐릭터마다 달라 칸으로 묶지 않는다.
        if ( pCurrent == nullptr || player.isCrossfading() || player.isCurrentLooping() == false || _listLayer.empty() == false || _sequencerWeight > 0.0f )
            return false;
        outRequest._pClip             = pCurrent;
        outRequest._time              = player.getCurrentTime();
        outRequest._playRate          = _playRate * player.getSpeed();
        outRequest._bAnchorRootMotion = _bExtractRootMotion;
        return true;
    }

    bool SkeletalAnimatorComponent::isAnimationActive() const
    {
        const bool bPlaying = _graphPlayer.getPlayer().getCurrentPlayable() != nullptr;
        return bPlaying || _listLayer.empty() == false || _sequencerWeight > 0.0f;
    }

    void SkeletalAnimatorComponent::advanceTime( const AnimationFrameContext& context )
    {
        const float32 deltaSeconds = context._deltaSeconds * _playRate;
        _listFiredNotify.clear();
        _graphPlayer.update( deltaSeconds, &_parameters, &_listFiredNotify );
        for ( LayerState& layer : _listLayer )
            layer._player.update( deltaSeconds, &_listFiredNotify );

        const AnimPlayer& player   = _graphPlayer.getPlayer();
        const AnimClip*   pCurrent = static_cast<const AnimClip*>( player.getCurrentPlayable() );
        const AnimClip*   pNext    = static_cast<const AnimClip*>( player.getNextPlayable() );
        const float32     alpha    = player.getBlendAlpha();

        // 루트 모션 — 두 칸의 움직임을 섞임 가중치로 섞는다.
        _rootMotionDelta = BoneTransform{};
        if ( _bExtractRootMotion == SW_TRUE && pCurrent != nullptr )
        {
            _rootMotionDelta = pCurrent->computeRootMotionDelta( player.getCurrentStep() );
            if ( pNext != nullptr )
                _rootMotionDelta = BoneTransform::blend( _rootMotionDelta, pNext->computeRootMotionDelta( player.getNextStep() ), alpha );
        }

        // 커브 — 두 칸의 값을 섞는다. 한쪽에만 있는 커브는 그쪽 가중치만큼입니다.
        _listCurveName.clear();
        _listCurveValue.clear();
        const AnimClip* arrClip[2]   = { pCurrent, pNext };
        const float32   arrWeight[2] = { 1.0f - alpha, alpha };
        const float32   arrTime[2]   = { player.getCurrentTime(), player.getNextTime() };
        for ( uint32 slotIndex = 0; slotIndex < 2; ++slotIndex )
        {
            if ( arrClip[slotIndex] == nullptr )
                continue;
            for ( const AnimCurve& curve : arrClip[slotIndex]->getCurves() )
            {
                const float32 value = curve.evaluate( arrTime[slotIndex] ) * arrWeight[slotIndex];
                const auto    it    = std::find( _listCurveName.begin(), _listCurveName.end(), curve._name );
                if ( it == _listCurveName.end() )
                {
                    _listCurveName.push_back( curve._name );
                    _listCurveValue.push_back( value );
                }
                else
                {
                    _listCurveValue[static_cast<size_t>( it - _listCurveName.begin() )] += value;
                }
            }
        }

        // 핫 리로드가 되살릴 상태 · 시각을 PROPERTY 에 비춘다(게임 스레드가 아니어도 이 컴포넌트만의 칸이다).
        _stateTime = player.getCurrentTime();
    }

    const vector<int32>& SkeletalAnimatorComponent::getTrackMap( const AnimClip& clip, const Skeleton& skeleton )
    {
        if ( _pTrackMapSkeleton != &skeleton )
        {
            _mapTrackToBone.clear();
            _pTrackMapSkeleton = &skeleton;
            for ( LayerState& layer : _listLayer )
                layer._pMaskSkeleton = nullptr;
        }
        vector<int32>& listBone = _mapTrackToBone[&clip];
        if ( listBone.size() != clip.getTrackCount() )
            clip.makeTrackToBoneMap( skeleton, listBone );
        return listBone;
    }

    void SkeletalAnimatorComponent::sampleClipIntoPose( const AnimClip& clip, float32 time, const Skeleton& skeleton, Pose& inoutPose, const uint8* pBoneMask )
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
        // 가산 기준은 레이어 클립의 시각 0 본 포즈다.
        layer._additiveReference.setToReference( skeleton );
        if ( layer._clip != nullptr )
            sampleClipIntoPose( *layer._clip, 0.0f, skeleton, layer._additiveReference );
    }

    void SkeletalAnimatorComponent::buildBasePose( SkeletalMeshComponent& unit )
    {
        const Skeleton&   skeleton = unit.getSkeleton();
        Pose&             pose     = unit.getLocalPose();
        const AnimPlayer& player   = _graphPlayer.getPlayer();
        const AnimClip*   pCurrent = static_cast<const AnimClip*>( player.getCurrentPlayable() );
        const AnimClip*   pNext    = static_cast<const AnimClip*>( player.getNextPlayable() );
        const uint8*      pMask    = unit.findBoneLodMask();

        // pose 는 유닛이 레퍼런스(또는 리더 포즈)로 채워 둔 상태다. 지금 칸을 그 위에 샘플하고, 페이드 중이면 다음 칸과 섞는다.
        if ( pCurrent != nullptr )
            sampleClipIntoPose( *pCurrent, player.getCurrentTime(), skeleton, pose, pMask );
        if ( pNext != nullptr )
        {
            _scratchPose.setToReference( skeleton );
            sampleClipIntoPose( *pNext, player.getNextTime(), skeleton, _scratchPose, pMask );
            Pose::blend( pose, _scratchPose, player.getBlendAlpha(), pose );
        }

        for ( LayerState& layer : _listLayer )
        {
            if ( layer._clip == nullptr || layer._desc._weight <= 0.0f )
                continue;
            refreshLayerMask( layer, skeleton );
            _scratchLayerPose.setToReference( skeleton );
            sampleClipIntoPose( *layer._clip, layer._player.getCurrentTime(), skeleton, _scratchLayerPose, pMask );
            if ( layer._desc._blend == AnimLayerBlend::Override )
            {
                Pose::blendMasked( pose, _scratchLayerPose, layer._desc._weight, layer._listBoneWeight.data(), pose );
            }
            else
            {
                Pose::makeAdditive( _scratchLayerPose, layer._additiveReference, _scratchPose );
                pose.applyAdditive( _scratchPose, layer._desc._weight, layer._listBoneWeight.data() );
            }
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
        for ( size_t curveIndex = 0; curveIndex < _listCurveName.size(); ++curveIndex )
        {
            const int32 targetIndex = unit.findMorphTargetIndex( _listCurveName[curveIndex] );
            if ( targetIndex >= 0 )
                unit.addMorphWeight( static_cast<uint32>( targetIndex ), _listCurveValue[curveIndex] );
        }
    }
} // namespace sw
