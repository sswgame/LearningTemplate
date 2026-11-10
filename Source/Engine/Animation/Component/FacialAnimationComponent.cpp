#include "pch.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Facial/FacialLiveSource.h"
#include "Engine/Animation/Facial/FacialRig.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Presentation/ISoundPlayer.h"
#include "Engine/Presentation/IVoiceDecoder.h"

namespace sw
{
    namespace
    {
        struct FacialAnimationInternal
        {
            /** @brief 이름들을 그리는 메시의 타깃 번호로 바꿉니다(없으면 없음 — 검증 `FacialRig::validate` 가 이미 알렸고 적용이 건너뛴다). */
            static void mapTargets( const SkeletalMeshComponent& skeletalMesh, const FacialPose& pose, vector<optional<uint32>>& outListIndex )
            {
                outListIndex.clear();
                for ( const FacialTargetWeight& target : pose._listTarget )
                {
                    outListIndex.push_back( skeletalMesh.findMorphTargetIndex( target._target ) );
                }
            }

            /** @brief 0 ~ 1 사이 [min, max] 의 값입니다. */
            static float32 pickBetween( float32 minValue, float32 maxValue, float32 random ) { return minValue + ( maxValue - minValue ) * random; }

            /** @brief 깜빡임 정도입니다 — 길이의 앞 절반에 감고 뒤 절반에 뜬다. */
            static float32 computeBlinkAmount( float32 elapsed, float32 duration )
            {
                const bool bOutsideBlinkWindow = elapsed < 0.0f || duration <= 0.0f || elapsed >= duration;
                if ( bOutsideBlinkWindow )
                    return 0.0f;
                return 1.0f - MathUtil::abs( 2.0f * elapsed / duration - 1.0f );
            }
        };
    } // namespace

    FacialAnimationBinding::FacialAnimationBinding( FacialAnimationComponent& owner )
        : _owner{ owner }
    {
    }

    bool FacialAnimationBinding::isAnimationActive() const
    {
        const FacialRig& rig = _owner.getFacialRig();
        return rig.getExpressions().empty() == false || rig.getVisemes().empty() == false || rig.getBlink()._listTarget.empty() == false ||
               rig.getGaze()._listEyeBone.empty() == false;
    }

    AnimationPoseAccess FacialAnimationBinding::getPoseAccess() const
    {
        return _owner.isDrivingFace() ? AnimationPoseAccess::Writes : AnimationPoseAccess::ReadsOnly;
    }

    void FacialAnimationBinding::prepareAnimationFrame( SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        (void)skeletalMesh;
        (void)context;
        // 실시간 입력은 게임 스레드에서 표정 순서대로 옮겨 둔다 — 워커의 얼굴 단계는 이 값만 읽는다.
        const vector<FacialPose>& listExpression = _owner.getFacialRig().getExpressions();
        _owner._listLiveWeight.assign( listExpression.size(), 0.0f );
        if ( _owner._pLiveSource == nullptr )
            return;
        for ( size_t poseIndex = 0; poseIndex < listExpression.size(); ++poseIndex )
        {
            float32 weight = 0.0f;
            if ( _owner._pLiveSource->tryGetChannel( listExpression[poseIndex]._name, weight ) )
                _owner._listLiveWeight[poseIndex] = weight;
        }
    }

    void FacialAnimationBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner.advanceTime( context._deltaSeconds );
        else if ( phase == AnimationPhase::PostProcess )
            _owner.applyFace( skeletalMesh );
    }

    void FacialAnimationBinding::finishAnimationFrame( SkeletalMeshComponent& skeletalMesh )
    {
        // 시선 목표를 모델 공간으로 옮겨 둔다(게임 스레드 — 트랜스폼을 읽는다). 다음 프레임의 후처리가 쓴다.
        if ( _owner._bLookAt == SW_TRUE )
        {
            _owner._lookAtModel       = float3::transform( _owner._lookAtWorld, skeletalMesh.getWorldMatrix().invert() );
            _owner._bLookAtModelReady = SW_TRUE;
        }
        // 같은 오브젝트의 애니메이터(표정 커브)를 다시 찾는다 — 컴포넌트가 붙고 떨어질 수 있다.
        GameObject*                pOwner    = skeletalMesh.getOwner();
        SkeletalAnimatorComponent* pAnimator = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        _owner._animator                     = TComponentHandle<SkeletalAnimatorComponent>{ pAnimator };
        _owner.pushWrinkles( skeletalMesh );
    }

    void FacialAnimationBinding::onAnimationMeshDetached( SkeletalMeshComponent& skeletalMesh )
    {
        if ( _owner._pSkeletalMesh == &skeletalMesh )
            _owner._pSkeletalMesh = nullptr;
    }

    FacialAnimationComponent::FacialAnimationComponent()
        : _facialRigPath{}
        , _binding{ *this }
        , _pRig{}
        , _pLipSync{}
        , _speechTrack{}
        , _listSpeechSample{}
        , _listExpressionWeight{}
        , _listLiveWeight{}
        , _pLiveSource{ nullptr }
        , _liveSourceSubscriptions{ ItemSubscriptionList<const IFacialLiveSource>::ReleaseFunction::create<&FacialAnimationComponent::detachLiveSource>( this ) }
        , _listExpressionTarget{}
        , _listVisemeTarget{}
        , _listBlinkTarget{}
        , _listEyeBone{}
        , _listTrackToRigViseme{}
        , _listScratchVisemeWeight{}
        , _pWrinkleMap{}
        , _listScratchExpressionName{}
        , _listScratchExpressionWeight{}
        , _wrinkleWeights{}
        , _pushedWrinkleWeights{}
        , _animator{}
        , _pSkeletalMesh{ nullptr }
        , _pBoundMesh{ nullptr }
        , _pBoundSkeleton{ nullptr }
        , _lookAtWorld{}
        , _lookAtModel{}
        , _saccadeOffset{}
        , _speechTime{ 0.0f }
        , _speechSampleRate{ 0 }
        , _blinkTimer{ 1.0f }
        , _blinkElapsed{ -1.0f }
        , _blinkAmount{ 0.0f }
        , _saccadeTimer{ 0.0f }
        , _random{}
        , _bBlink{ SW_TRUE }
        , _bSaccades{ SW_TRUE }
        , _bLookAt{ SW_FALSE }
        , _bLookAtModelReady{ SW_FALSE }
        , _bSpeaking{ SW_FALSE }
        , _bLipSyncSettingsReady{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FacialAnimationComponent::~FacialAnimationComponent()
    {
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
    }

    void FacialAnimationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 깜빡임 · 사카드가 캐릭터마다 다른 박자로 돌게 씨앗을 컴포넌트 id 로 섞는다(결정적).
        const uint64 mixed = getHandle().componentID() * HashUtil::kGoldenRatio64;
        _random.setSeed( static_cast<uint32>( mixed ^ ( mixed >> 32 ) ) | 1u ); // `| 1` — 0 이 아닌 씨앗
        // 경로로 이미 읽었으면(`setFacialRigPath` · `setFacialRig`) 그대로 둔다 — 시작 전에 정한 표정 · 말하기를 지우지 않게.
        if ( _binding.isAnimationActive() == false )
            loadRig();
        bindToUnit();
        _blinkTimer = FacialAnimationInternal::pickBetween( getFacialRig().getBlink()._minInterval, getFacialRig().getBlink()._maxInterval, _random.nextFloat() );
    }

    void FacialAnimationComponent::onEndPlay()
    {
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
        _pSkeletalMesh = nullptr;
        Component::onEndPlay();
    }

    void FacialAnimationComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_rigPathName( "_facialRigPath" );
        if ( propertyName == s_rigPathName )
            loadRig();
    }

    void FacialAnimationComponent::setFacialRigPath( string_view path )
    {
        _facialRigPath = string( path );
        loadRig();
    }

    const FacialRig& FacialAnimationComponent::getFacialRig() const
    {
        // 리그가 없는 상태는 빈 리그로 읽는다 — 값을 지어내는 대체가 아니라 "움직일 것 없음" 이다(모든 목록이 빈다).
        static const FacialRig s_noRig{};
        return _pRig != nullptr ? *_pRig : s_noRig;
    }

    void FacialAnimationComponent::setFacialRig( shared_ptr<const FacialRig> pRig )
    {
        _pRig = std::move( pRig );
        _listExpressionWeight.assign( getFacialRig().getExpressions().size(), 0.0f );
        _pBoundMesh     = nullptr;
        _pBoundSkeleton = nullptr;
        // 말하는 중인 트랙의 비즘 번호를 새 리그로 다시 잇는다.
        remapTrackVisemes();
        bindToUnit();
    }

    void FacialAnimationComponent::loadRig()
    {
        // 리그는 공유 캐시의 한 벌이다 — 제자리 다시 읽기 뒤 핫 리로드가 이 칸을 알리면 같은 객체를 다시 받고 번호를 다시 짓는다.
        shared_ptr<const FacialRig> pRig = _facialRigPath.acquire();
        if ( _facialRigPath.empty() == false && pRig == nullptr )
            SW_LOG_ERROR_ID( "FacialAnimation.FacialRigCouldNot", "Facial rig '%#' could not be loaded", getFacialRigPath().c_str() );
        setFacialRig( std::move( pRig ) );
    }

    void FacialAnimationComponent::bindToUnit()
    {
        GameObject*                pOwner        = getOwner();
        SkeletalMeshComponent*     pSkeletalMesh = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        SkeletalAnimatorComponent* pAnimator     = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        // 표정 커브를 낼 애니메이터 — 첫 평가부터 읽게 지금 찾는다(뒤에 붙으면 게임 스레드 마무리가 찾는다).
        _animator = TComponentHandle<SkeletalAnimatorComponent>{ pAnimator };
        if ( pSkeletalMesh == _pSkeletalMesh )
            return;
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
        _pSkeletalMesh = pSkeletalMesh;
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration = _pSkeletalMesh->registerAnimationPhaseTask( &_binding );
        else if ( pOwner != nullptr )
            SW_LOG_WARNING_ID( "FacialAnimation.FacialAnimationNoSkeletalMeshComponent", "'%#': facial animation has no SkeletalMeshComponent on its object", pOwner->getName().c_str() );
    }

    bool FacialAnimationComponent::setExpressionWeight( const hashed_string& expressionName, float32 weight )
    {
        const optional<uint32> expressionIndex = getFacialRig().findExpressionIndex( expressionName );
        if ( expressionIndex.has_value() == false )
            return false;
        _listExpressionWeight[*expressionIndex] = weight;
        if ( _pSkeletalMesh != nullptr )
            _pSkeletalMesh->markPoseDirty();
        return true;
    }

    Subscription FacialAnimationComponent::registerLiveSource( const IFacialLiveSource* pSource )
    {
        if ( pSource == nullptr )
            return {};
        _pLiveSource = pSource;
        if ( _pSkeletalMesh != nullptr )
            _pSkeletalMesh->markPoseDirty();
        return _liveSourceSubscriptions.issue( pSource );
    }

    void FacialAnimationComponent::detachLiveSource( const IFacialLiveSource* pSource )
    {
        if ( _pLiveSource != pSource )
            return;
        _pLiveSource = nullptr;
        if ( _pSkeletalMesh != nullptr )
            _pSkeletalMesh->markPoseDirty();
    }

    void FacialAnimationComponent::setLookAtTarget( const float3& worldPosition )
    {
        _lookAtWorld = worldPosition;
        _bLookAt     = SW_TRUE;
        if ( _pSkeletalMesh != nullptr )
        {
            _lookAtModel       = float3::transform( worldPosition, _pSkeletalMesh->getWorldMatrix().invert() );
            _bLookAtModelReady = SW_TRUE;
        }
    }

    void FacialAnimationComponent::clearLookAtTarget()
    {
        _bLookAt           = SW_FALSE;
        _bLookAtModelReady = SW_FALSE;
    }

    void FacialAnimationComponent::setLipSyncSettings( shared_ptr<const LipSyncSettings> pSettings )
    {
        _pLipSync              = std::move( pSettings );
        _bLipSyncSettingsReady = SW_TRUE;
        _listTrackToRigViseme.clear();
    }

    bool FacialAnimationComponent::speak( const AssetID& assetID )
    {
        const string_view audioPath = assetID.view();
        if ( _bLipSyncSettingsReady == SW_FALSE )
        {
            const string& lipSyncPath = engine::getSubsystem<EngineDefaultAssets>()._lipSyncSettings.get();
            _pLipSync                 = SharedAssetCache<LipSyncSettings>::acquire( lipSyncPath ); // 얼굴마다 다시 읽지 않는다 — 엔진 기본 표 한 벌
            if ( _pLipSync == nullptr )
                SW_LOG_ERROR_ID( "FacialAnimation.LipSyncSettingsCould", "Lip sync settings '%#' could not be loaded", lipSyncPath );
            _bLipSyncSettingsReady = SW_TRUE;
        }
        // 임포트가 만든 비즘 트랙이 먼저다. 없으면 PCM 을 풀어 진폭으로 입을 연다.
        const string trackPath = VisemeTrack::makePathForAudio( audioPath );
        VisemeTrack  track;
        string       trackText;
        bool         bStarted = false;
        if ( AssetIO::readTextAsset( AssetID{ trackPath }, trackText ) && track.loadFromXMLText( trackText, trackPath ) )
        {
            speakTrack( track );
            bStarted = true;
        }
        else
        {
            // 음성 디코드는 오디오의 창구다 — 오디오를 짓지 않는 구성(전용 서버)은 트랙만 쓴다.
            IVoiceDecoder*  pDecoder = IVoiceDecoder::find();
            vector<float32> listSample;
            uint32          sampleRate = 0;
            if ( pDecoder != nullptr && pDecoder->decodeMono( assetID, listSample, sampleRate ) )
            {
                speakSamples( std::move( listSample ), sampleRate );
                bStarted = true;
            }
        }
        if ( bStarted == false )
        {
            SW_LOG_ERROR_ID( "FacialAnimation.NeitherVisemeTrackNor", "'%#': neither a viseme track nor decodable audio", string( audioPath ).c_str() );
            return false;
        }
        // 소리는 이 오브젝트가 속한 씬이 받은 엔진 범위의 소리 창구로 낸다(`SceneEngineServices::_pEngineSubsystems` — 전역 표를 다시 찾지 않는다).
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        ISoundPlayer*            pSound   = pManager != nullptr ? locateSubsystem<ISoundPlayer>( pManager->getSceneEngineServices()._pEngineSubsystems ) : nullptr;
        if ( pSound != nullptr )
            (void)pSound->play( assetID, hashed_string{} );
        return true;
    }

    void FacialAnimationComponent::speakTrack( const VisemeTrack& track )
    {
        _speechTrack = track;
        _listSpeechSample.clear();
        _speechSampleRate = 0;
        _speechTime       = 0.0f;
        _bSpeaking        = SW_TRUE;
        // 리그가 바뀌면 `setFacialRig` 가 다시 잇는다.
        remapTrackVisemes();
        if ( _pSkeletalMesh != nullptr )
            _pSkeletalMesh->markPoseDirty();
    }

    void FacialAnimationComponent::remapTrackVisemes()
    {
        // 트랙의 비즘 순서 → 리그 비즘 번호. 리그에 없는 비즘은 알리고 적용에서 건너뛴다(리그가 없으면 움직일 것이 없으니 알리지 않는다).
        _listTrackToRigViseme.clear();
        for ( const hashed_string& visemeName : _speechTrack._listViseme )
        {
            const optional<uint32> visemeIndex = getFacialRig().findVisemeIndex( visemeName );
            if ( visemeIndex.has_value() == false && _pRig != nullptr )
                SW_LOG_WARNING_ID( "FacialAnimation.TrackVisemeNotInRig", "'%#': viseme track names viseme '%#' the facial rig does not have - it is skipped", getFacialRigPath().c_str(),
                                   visemeName.c_str() );
            _listTrackToRigViseme.push_back( visemeIndex );
        }
    }

    void FacialAnimationComponent::speakSamples( vector<float32> listSample, uint32 sampleRate )
    {
        _speechTrack      = VisemeTrack{};
        _listSpeechSample = std::move( listSample );
        _speechSampleRate = sampleRate;
        _speechTime       = 0.0f;
        _bSpeaking        = ( _speechSampleRate > 0 && _listSpeechSample.empty() == false ) ? SW_TRUE : SW_FALSE;
        _listTrackToRigViseme.clear();
        // 진폭 립싱크는 표의 대체 비즘 하나를 연다 — 리그에 없으면 여기서 한 번 알리고 적용(`applyFace`)이 건너뛴다.
        const bool bFallbackMissing = _bSpeaking == SW_TRUE && _pLipSync != nullptr && _pRig != nullptr && getFacialRig().findVisemeIndex( _pLipSync->_fallbackViseme ).has_value() == false;
        if ( bFallbackMissing )
            SW_LOG_WARNING_ID( "FacialAnimation.FallbackVisemeNotInRig", "'%#': lip sync fallback viseme '%#' is not in the facial rig - amplitude lip sync does not open the mouth", getFacialRigPath().c_str(),
                               _pLipSync->_fallbackViseme.c_str() );
    }

    void FacialAnimationComponent::stopSpeaking()
    {
        _bSpeaking   = SW_FALSE;
        _speechTrack = VisemeTrack{};
        _listSpeechSample.clear();
    }

    bool FacialAnimationComponent::isSpeaking() const
    {
        return _bSpeaking == SW_TRUE;
    }

    void FacialAnimationComponent::advanceTime( float32 deltaSeconds )
    {
        // 깜빡임: 타이머가 다 되면 길이만큼 감았다 뜨고, 다음 간격을 뽑는다.
        const FacialBlinkDef& blink = getFacialRig().getBlink();
        if ( _bBlink == SW_TRUE && blink._listTarget.empty() == false )
        {
            if ( _blinkElapsed >= 0.0f )
            {
                _blinkElapsed += deltaSeconds;
                if ( _blinkElapsed >= blink._duration )
                {
                    _blinkElapsed = -1.0f;
                    _blinkTimer   = FacialAnimationInternal::pickBetween( blink._minInterval, blink._maxInterval, _random.nextFloat() );
                }
            }
            else
            {
                _blinkTimer -= deltaSeconds;
                if ( _blinkTimer <= 0.0f )
                    _blinkElapsed = 0.0f;
            }
            _blinkAmount = FacialAnimationInternal::computeBlinkAmount( _blinkElapsed, blink._duration );
        }
        else
        {
            _blinkAmount = 0.0f;
        }

        // 사카드: 간격마다 최대 크기 안의 작은 요 · 피치로 튄다(시선 목표가 있을 때만).
        const FacialGazeDef& gaze           = getFacialRig().getGaze();
        const bool           bSaccadeActive = _bSaccades == SW_TRUE && _bLookAt == SW_TRUE && gaze._listEyeBone.empty() == false;
        if ( bSaccadeActive )
        {
            _saccadeTimer -= deltaSeconds;
            if ( _saccadeTimer <= 0.0f )
            {
                const float32 amplitude = gaze._saccadeAmplitude;
                _saccadeOffset          = float2{ ( _random.nextFloat() * 2.0f - 1.0f ) * amplitude, ( _random.nextFloat() * 2.0f - 1.0f ) * amplitude };
                _saccadeTimer           = FacialAnimationInternal::pickBetween( gaze._saccadeMinInterval, gaze._saccadeMaxInterval, _random.nextFloat() );
            }
        }
        else
        {
            _saccadeOffset = float2{};
        }

        if ( _bSpeaking == SW_TRUE )
        {
            _speechTime += deltaSeconds;
            const float32 duration = _speechSampleRate > 0 ? static_cast<float32>( _listSpeechSample.size() ) / static_cast<float32>( _speechSampleRate )
                                                           : _speechTrack.getDuration();
            if ( _speechTime >= duration )
                _bSpeaking = SW_FALSE;
        }
    }

    void FacialAnimationComponent::refreshBindings( const SkeletalMeshComponent& skeletalMesh )
    {
        const Mesh*     pMesh     = skeletalMesh.getRawMesh();
        const Skeleton* pSkeleton = &skeletalMesh.getSkeleton();
        if ( pMesh == _pBoundMesh && pSkeleton == _pBoundSkeleton )
            return;
        _pBoundMesh     = pMesh;
        _pBoundSkeleton = pSkeleton;

        vector<hashed_string> listMorphTargetName;
        if ( pMesh != nullptr )
        {
            for ( const MeshMorphTarget& target : pMesh->getMorphTargets() )
            {
                listMorphTargetName.push_back( target._name );
            }
        }
        (void)getFacialRig().validate( listMorphTargetName, *pSkeleton, _facialRigPath.empty() ? string_view( "(runtime rig)" ) : string_view( _facialRigPath ) );

        _listExpressionTarget.resize( getFacialRig().getExpressions().size() );
        for ( size_t poseIndex = 0; poseIndex < getFacialRig().getExpressions().size(); ++poseIndex )
        {
            FacialAnimationInternal::mapTargets( skeletalMesh, getFacialRig().getExpressions()[poseIndex], _listExpressionTarget[poseIndex] );
        }
        _listVisemeTarget.resize( getFacialRig().getVisemes().size() );
        for ( size_t poseIndex = 0; poseIndex < getFacialRig().getVisemes().size(); ++poseIndex )
        {
            FacialAnimationInternal::mapTargets( skeletalMesh, getFacialRig().getVisemes()[poseIndex], _listVisemeTarget[poseIndex] );
        }
        _listBlinkTarget.clear();
        for ( const hashed_string& target : getFacialRig().getBlink()._listTarget )
        {
            _listBlinkTarget.push_back( skeletalMesh.findMorphTargetIndex( target ) );
        }
        _listEyeBone.clear();
        for ( const hashed_string& bone : getFacialRig().getGaze()._listEyeBone )
        {
            // 스켈레톤에 없는 눈 본은 위 검증(`FacialRig::validate`)이 알렸다 — 시선에서 뺀다.
            const optional<uint32> boneIndex = pSkeleton->findBoneIndex( bone );
            if ( boneIndex.has_value() )
                _listEyeBone.push_back( *boneIndex );
        }
    }

    void FacialAnimationComponent::addPose( SkeletalMeshComponent& skeletalMesh, const vector<optional<uint32>>& listTargetIndex, const FacialPose& pose, float32 weight ) const
    {
        if ( weight == 0.0f )
            return;
        for ( size_t targetIndex = 0; targetIndex < listTargetIndex.size() && targetIndex < pose._listTarget.size(); ++targetIndex )
        {
            if ( listTargetIndex[targetIndex].has_value() )
                skeletalMesh.addMorphWeight( *listTargetIndex[targetIndex], pose._listTarget[targetIndex]._weight * weight );
        }
    }

    const SkeletalAnimatorComponent* FacialAnimationComponent::findAnimator( const SkeletalMeshComponent* pSkeletalMesh ) const
    {
        const GameObject* pOwner      = ( pSkeletalMesh != nullptr ) ? pSkeletalMesh->getOwner() : nullptr;
        const bool        bResolvable = _animator.isValid() && pOwner != nullptr && pOwner->getManager() != nullptr;
        return bResolvable ? pOwner->getManager()->resolveComponent( _animator ) : nullptr;
    }

    float32 FacialAnimationComponent::computeExpressionWeight( size_t poseIndex, const SkeletalAnimatorComponent* pAnimator ) const
    {
        float32 weight = _listExpressionWeight[poseIndex];
        if ( poseIndex < _listLiveWeight.size() )
            weight += _listLiveWeight[poseIndex]; // 실시간 입력(게임 스레드가 옮겨 둔 값)
        if ( pAnimator != nullptr )
            weight += pAnimator->getCurveValue( getFacialRig().getExpressions()[poseIndex]._name );
        return weight;
    }

    bool FacialAnimationComponent::isDrivingFace() const
    {
        const SkeletalAnimatorComponent* pAnimator = findAnimator( _pSkeletalMesh );
        for ( size_t poseIndex = 0; poseIndex < getFacialRig().getExpressions().size(); ++poseIndex )
        {
            if ( computeExpressionWeight( poseIndex, pAnimator ) != 0.0f )
                return true;
        }
        const bool bSpeakingVisemes = _bSpeaking == SW_TRUE && getFacialRig().getVisemes().empty() == false;
        const bool bBlinking        = _blinkAmount > 0.0f && getFacialRig().getBlink()._listTarget.empty() == false;
        const bool bGazing          = _bLookAt == SW_TRUE && _bLookAtModelReady == SW_TRUE && getFacialRig().getGaze()._listEyeBone.empty() == false;
        return bSpeakingVisemes || bBlinking || bGazing;
    }

    void FacialAnimationComponent::setWrinkleMap( shared_ptr<const WrinkleMap> pMap )
    {
        _pWrinkleMap = std::move( pMap );
        if ( _pWrinkleMap == nullptr )
            return;
        vector<hashed_string> listName;
        for ( const FacialPose& pose : getFacialRig().getExpressions() )
            listName.push_back( pose._name );
        vector<hashed_string> listMissing;
        if ( _pWrinkleMap->checkExpressions( span<const hashed_string>( listName.data(), listName.size() ), &listMissing ) )
            return;
        for ( const hashed_string& missing : listMissing )
            SW_LOG_ERROR_ID( "Facial.WrinkleExpressionUnknown", "Facial rig '%#': wrinkle source expression '%#' is not one of its expressions - it adds nothing", getFacialRigPath().c_str(), missing.c_str() );
    }

    void FacialAnimationComponent::computeWrinkles( const SkeletalAnimatorComponent* pAnimator, bool bDriving )
    {
        if ( _pWrinkleMap == nullptr || bDriving == false )
        {
            _wrinkleWeights = WrinkleWeights{};
            return;
        }
        const vector<FacialPose>& listExpression = getFacialRig().getExpressions();
        _listScratchExpressionName.resize( listExpression.size() );
        _listScratchExpressionWeight.resize( listExpression.size() );
        for ( size_t poseIndex = 0; poseIndex < listExpression.size(); ++poseIndex )
        {
            _listScratchExpressionName[poseIndex]   = listExpression[poseIndex]._name;
            _listScratchExpressionWeight[poseIndex] = computeExpressionWeight( poseIndex, pAnimator );
        }
        _pWrinkleMap->evaluate( span<const hashed_string>( _listScratchExpressionName.data(), _listScratchExpressionName.size() ),
                                span<const float32>( _listScratchExpressionWeight.data(), _listScratchExpressionWeight.size() ), _wrinkleWeights );
    }

    void FacialAnimationComponent::pushWrinkles( SkeletalMeshComponent& skeletalMesh )
    {
        if ( _wrinkleWeights._maskA == _pushedWrinkleWeights._maskA && _wrinkleWeights._maskB == _pushedWrinkleWeights._maskB )
            return;
        // 외형의 표면 상태(`CharacterSurfaceBinding::acquireInstance`)와 같은 인스턴스를 나눈다 — 있으면 그것, 없으면 메시 머티리얼로 하나 만든다.
        // shortcut: 메시의 주 머티리얼 인스턴스 하나에만 넣는다 — 얼굴이 따로 구간 머티리얼이면 구간 인스턴스로 넓힌다.
        shared_ptr<MaterialInstance> instance = skeletalMesh.getMaterialInstance();
        if ( instance == nullptr && skeletalMesh.getMaterial() != nullptr )
        {
            instance = MaterialInstance::create( skeletalMesh.getMaterial() );
            skeletalMesh.setMaterialInstance( instance );
        }
        if ( instance == nullptr )
            return;
        instance->setVectorParameter( hashed_string( WrinkleMap::kMaskAParameter ), _wrinkleWeights._maskA );
        instance->setVectorParameter( hashed_string( WrinkleMap::kMaskBParameter ), _wrinkleWeights._maskB );
        _pushedWrinkleWeights = _wrinkleWeights;
    }

    void FacialAnimationComponent::applyFace( SkeletalMeshComponent& skeletalMesh )
    {
        refreshBindings( skeletalMesh );
        const bool                       bDriving  = isDrivingFace();
        const SkeletalAnimatorComponent* pAnimator = findAnimator( &skeletalMesh );
        computeWrinkles( pAnimator, bDriving );
        if ( bDriving == false )
            return;

        // 표정 — 명시 가중치 + 같은 오브젝트 애니메이터의 같은 이름 커브(애니메이터의 시간 단계가 이미 이번 프레임 커브를 냈다).
        const vector<FacialPose>& listExpression = getFacialRig().getExpressions();
        for ( size_t poseIndex = 0; poseIndex < listExpression.size(); ++poseIndex )
        {
            addPose( skeletalMesh, _listExpressionTarget[poseIndex], listExpression[poseIndex], computeExpressionWeight( poseIndex, pAnimator ) );
        }

        // 립싱크 — 트랙이면 그 프레임의 비즘 가중치, 아니면 진폭이 대체 비즘을 연다.
        if ( _bSpeaking == SW_TRUE )
        {
            const vector<FacialPose>& listViseme = getFacialRig().getVisemes();
            if ( _speechSampleRate > 0 && _pLipSync != nullptr ) // 표를 받지 못했으면 진폭으로 입을 열지 않는다(읽기가 오류를 남겼다)
            {
                const LipSyncSettings& lipSync       = *_pLipSync;
                const float32          windowSeconds = 1.0f / MathUtil::max( lipSync._frameRate, 1.0f );
                const float32          openness      = VisemeTrack::computeOpenness( VisemeTrack::computeRms( _listSpeechSample, _speechSampleRate, _speechTime, windowSeconds ), lipSync );
                const optional<uint32> visemeIndex   = getFacialRig().findVisemeIndex( lipSync._fallbackViseme ); // 없음은 `speakSamples` 가 알렸다
                if ( visemeIndex.has_value() )
                    addPose( skeletalMesh, _listVisemeTarget[*visemeIndex], listViseme[*visemeIndex], openness );
            }
            else
            {
                _speechTrack.sample( _speechTime, _listScratchVisemeWeight );
                for ( size_t trackViseme = 0; trackViseme < _listScratchVisemeWeight.size() && trackViseme < _listTrackToRigViseme.size(); ++trackViseme )
                {
                    const optional<uint32>& visemeIndex = _listTrackToRigViseme[trackViseme]; // 없음은 `remapTrackVisemes` 가 알렸다
                    if ( visemeIndex.has_value() )
                        addPose( skeletalMesh, _listVisemeTarget[*visemeIndex], listViseme[*visemeIndex], _listScratchVisemeWeight[trackViseme] );
                }
            }
        }

        // 깜빡임
        for ( const optional<uint32>& targetIndex : _listBlinkTarget )
        {
            if ( targetIndex.has_value() && _blinkAmount > 0.0f )
                skeletalMesh.addMorphWeight( *targetIndex, _blinkAmount );
        }

        applyGaze( skeletalMesh );
    }

    void FacialAnimationComponent::applyGaze( SkeletalMeshComponent& skeletalMesh )
    {
        const bool bGazeUnavailable = _bLookAt == SW_FALSE || _bLookAtModelReady == SW_FALSE || _listEyeBone.empty();
        if ( bGazeUnavailable )
            return;
        const FacialGazeDef&    gaze       = getFacialRig().getGaze();
        const vector<float4x4>& listModel  = skeletalMesh.getModelSpaceTransforms();
        const vector<int32>&    listParent = skeletalMesh.getSkeleton().getParentIndices();
        Pose&                   pose       = skeletalMesh.getLocalPose();
        quaternion*             pRotation  = pose.getRotationData();
        const float32           maxAngle   = gaze._maxAngle;
        const quaternion        saccade    = quaternion::makeFromYawPitchRoll( _saccadeOffset._x, _saccadeOffset._y, 0.0f );
        for ( const uint32 boneIndex : _listEyeBone )
        {
            const bool bInvalidEyeBone = boneIndex >= listModel.size() || boneIndex >= pose.getBoneCount();
            if ( bInvalidEyeBone )
                continue;
            // 부모 공간에서 돌린다 — 지금 로컬 회전의 앞 방향을 목표 방향으로 돌리는 가장 작은 회전을 최대 각으로 자르고 로컬 회전 뒤에 붙인다.
            const int32    parentIndex = listParent[boneIndex];
            const float4x4 toParent    = parentIndex >= 0 ? listModel[static_cast<size_t>( parentIndex )].invert() : float4x4::Identity;
            const float3   eyePosition = listModel[boneIndex].getTranslation();
            float3         desired     = float3::transformVector( _lookAtModel - eyePosition, toParent );
            if ( desired.getLengthSquared() <= MathUtil::kEpsilon )
                continue;
            desired                         = float3::transform( desired.normalize(), saccade );
            const quaternion localRotation  = pRotation[boneIndex];
            const float3     currentForward = float3::transform( gaze._forwardAxis, localRotation ).normalize();
            quaternion       delta          = quaternion::fromToRotation( currentForward, desired );
            const float32    angle          = MathUtil::acos( MathUtil::clamp( currentForward.dot( desired ), -1.0f, 1.0f ) );
            if ( angle > maxAngle && angle > 0.0f )
                delta = quaternion::slerp( quaternion::Identity, delta, maxAngle / angle );
            pRotation[boneIndex] = quaternion::concatenate( localRotation, delta ).normalize();
        }
    }
} // namespace sw
