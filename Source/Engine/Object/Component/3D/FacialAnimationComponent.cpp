#include "pch.h"

#include "Engine/Object/Component/3D/FacialAnimationComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FacialAnimation" );

    namespace
    {
        struct FacialAnimationInternal
        {
            /** @brief 이름들을 그리는 메시의 타깃 번호로 바꿉니다(없으면 -1 — 검증이 이미 알렸다). */
            static void mapTargets( const SkeletalMeshComponent& unit, const FacialPose& pose, vector<int32>& outListIndex )
            {
                outListIndex.clear();
                for ( const FacialTargetWeight& target : pose._listTarget )
                    outListIndex.push_back( unit.findMorphTargetIndex( target._target ) );
            }

            /** @brief 0 ~ 1 사이 [min, max] 의 값입니다. */
            static float32 pickBetween( float32 minValue, float32 maxValue, float32 random ) { return minValue + ( maxValue - minValue ) * random; }

            /** @brief 깜빡임 정도입니다 — 길이의 앞 절반에 감고 뒤 절반에 뜬다. */
            static float32 computeBlinkAmount( float32 elapsed, float32 duration )
            {
                if ( elapsed < 0.0f || duration <= 0.0f || elapsed >= duration )
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
        const FacialRig& rig = _owner._rig;
        return rig.getExpressions().empty() == false || rig.getVisemes().empty() == false || rig.getBlink()._listTarget.empty() == false ||
               rig.getGaze()._listEyeBone.empty() == false;
    }

    void FacialAnimationBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner.advanceTime( context._deltaSeconds );
        else if ( phase == AnimationPhase::PostProcess )
            _owner.applyFace( unit );
    }

    void FacialAnimationBinding::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        // 시선 목표를 모델 공간으로 옮겨 둔다(게임 스레드 — 트랜스폼을 읽는다). 다음 프레임의 후처리가 쓴다.
        if ( _owner._bLookAt == SW_TRUE )
        {
            _owner._lookAtModel       = float3::transform( _owner._lookAtWorld, unit.getWorldMatrix().invert() );
            _owner._bLookAtModelReady = SW_TRUE;
        }
        // 같은 오브젝트의 애니메이터(표정 커브)를 다시 찾는다 — 컴포넌트가 붙고 떨어질 수 있다.
        GameObject*                pOwner    = unit.getOwner();
        SkeletalAnimatorComponent* pAnimator = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        _owner._animator                     = ( pAnimator != nullptr ) ? pAnimator->getHandle() : ComponentHandle{};
    }

    void FacialAnimationBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    FacialAnimationComponent::FacialAnimationComponent()
        : _facialRigPath{}
        , _binding{ *this }
        , _rig{}
        , _lipSync{}
        , _speechTrack{}
        , _listSpeechSample{}
        , _listExpressionWeight{}
        , _listExpressionTarget{}
        , _listVisemeTarget{}
        , _listBlinkTarget{}
        , _listEyeBone{}
        , _listTrackToRigViseme{}
        , _listScratchVisemeWeight{}
        , _animator{}
        , _pUnit{ nullptr }
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
        , _randomState{ 0x9E3779B9u }
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
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
    }

    void FacialAnimationComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 깜빡임 · 사카드가 캐릭터마다 다른 박자로 돌게 씨앗을 컴포넌트 id 로 섞는다(결정적).
        const uint64 mixed = getHandle().componentId() * 0x9E3779B97F4A7C15ull;
        _randomState       = static_cast<uint32>( mixed ^ ( mixed >> 32 ) ) | 1u;
        // 경로로 이미 읽었으면(`setFacialRigPath` · `setFacialRig`) 그대로 둔다 — 시작 전에 정한 표정 · 말하기를 지우지 않게.
        if ( _binding.isAnimationActive() == false )
            loadRig();
        bindToUnit();
        _blinkTimer = FacialAnimationInternal::pickBetween( _rig.getBlink()._minInterval, _rig.getBlink()._maxInterval, nextRandom() );
    }

    void FacialAnimationComponent::onEndPlay()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = nullptr;
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

    void FacialAnimationComponent::setFacialRig( const FacialRig& rig )
    {
        _rig = rig;
        _listExpressionWeight.assign( _rig.getExpressions().size(), 0.0f );
        _pBoundMesh     = nullptr;
        _pBoundSkeleton = nullptr;
        // 말하는 중인 트랙의 비즘 번호를 새 리그로 다시 잇는다.
        _listTrackToRigViseme.clear();
        for ( const hashed_string& visemeName : _speechTrack._listViseme )
            _listTrackToRigViseme.push_back( _rig.findVisemeIndex( visemeName ) );
        bindToUnit();
    }

    void FacialAnimationComponent::loadRig()
    {
        FacialRig rig;
        if ( _facialRigPath.empty() == false && rig.loadFromResource( _facialRigPath ) == false )
            SW_LOG_ERROR( "Facial rig '%#' could not be loaded", _facialRigPath.c_str() );
        setFacialRig( rig );
    }

    void FacialAnimationComponent::bindToUnit()
    {
        GameObject*                pOwner    = getOwner();
        SkeletalMeshComponent*     pUnit     = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        SkeletalAnimatorComponent* pAnimator = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        // 표정 커브를 낼 애니메이터 — 첫 평가부터 읽게 지금 찾는다(뒤에 붙으면 게임 스레드 마무리가 찾는다).
        _animator = ( pAnimator != nullptr ) ? pAnimator->getHandle() : ComponentHandle{};
        if ( pUnit == _pUnit )
            return;
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _pUnit->addAnimationPhaseTask( &_binding );
        else if ( pOwner != nullptr )
            SW_LOG_WARNING( "'%#': facial animation has no SkeletalMeshComponent on its object", pOwner->getName().c_str() );
    }

    bool FacialAnimationComponent::setExpressionWeight( const hashed_string& expressionName, float32 weight )
    {
        const int32 expressionIndex = _rig.findExpressionIndex( expressionName );
        if ( expressionIndex < 0 )
            return false;
        _listExpressionWeight[static_cast<size_t>( expressionIndex )] = weight;
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
        return true;
    }

    void FacialAnimationComponent::setLookAtTarget( const float3& worldPosition )
    {
        _lookAtWorld = worldPosition;
        _bLookAt     = SW_TRUE;
        if ( _pUnit != nullptr )
        {
            _lookAtModel       = float3::transform( worldPosition, _pUnit->getWorldMatrix().invert() );
            _bLookAtModelReady = SW_TRUE;
        }
    }

    void FacialAnimationComponent::clearLookAtTarget()
    {
        _bLookAt           = SW_FALSE;
        _bLookAtModelReady = SW_FALSE;
    }

    void FacialAnimationComponent::setLipSyncSettings( const LipSyncSettings& settings )
    {
        _lipSync               = settings;
        _bLipSyncSettingsReady = SW_TRUE;
        _listTrackToRigViseme.clear();
    }

    bool FacialAnimationComponent::speak( string_view audioPath )
    {
        if ( _bLipSyncSettingsReady == SW_FALSE )
        {
            if ( _lipSync.loadFromResource( LipSyncSettings::kResourcePath ) == false )
                SW_LOG_ERROR( "Lip sync settings '%#' could not be loaded", LipSyncSettings::kResourcePath );
            _bLipSyncSettingsReady = SW_TRUE;
        }
        // 임포트가 만든 비즘 트랙이 먼저다. 없으면 PCM 을 풀어 진폭으로 입을 연다.
        const string trackPath = VisemeTrack::makePathForAudio( audioPath );
        VisemeTrack  track;
        string       trackText;
        bool         bStarted = false;
        if ( ResourceUtil::readTextResource( trackPath, trackText ) && track.parseJson( trackText, trackPath ) )
        {
            speakTrack( track );
            bStarted = true;
        }
        else
        {
            vector<uint8>   bytes;
            AudioPcm        pcm;
            vector<float32> listSample;
            if ( ResourceUtil::readBinaryResource( audioPath, bytes ) && AudioClipDecoder::decode( audioPath, bytes.data(), bytes.size(), pcm ) &&
                 LipSyncAnalyzer::decodeMono( pcm, listSample ) )
            {
                speakSamples( std::move( listSample ), pcm._sampleRate );
                bStarted = true;
            }
        }
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "'%#': neither a viseme track nor decodable audio", string( audioPath ).c_str() );
            return false;
        }
        IAudioSystem* pAudio = engine::getBoundEngineServices()._pAudioSystem;
        if ( pAudio != nullptr && pAudio->isInitialized() )
            (void)pAudio->play( audioPath );
        return true;
    }

    void FacialAnimationComponent::speakTrack( const VisemeTrack& track )
    {
        _speechTrack = track;
        _listSpeechSample.clear();
        _speechSampleRate = 0;
        _speechTime       = 0.0f;
        _bSpeaking        = SW_TRUE;
        // 트랙의 비즘 순서 → 리그 비즘 번호(리그에 없는 비즘은 버린다). 리그가 바뀌면 `setFacialRig` 가 다시 잇는다.
        _listTrackToRigViseme.clear();
        for ( const hashed_string& visemeName : _speechTrack._listViseme )
            _listTrackToRigViseme.push_back( _rig.findVisemeIndex( visemeName ) );
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    void FacialAnimationComponent::speakSamples( vector<float32> listSample, uint32 sampleRate )
    {
        _speechTrack      = VisemeTrack{};
        _listSpeechSample = std::move( listSample );
        _speechSampleRate = sampleRate;
        _speechTime       = 0.0f;
        _bSpeaking        = ( _speechSampleRate > 0 && _listSpeechSample.empty() == false ) ? SW_TRUE : SW_FALSE;
        _listTrackToRigViseme.clear();
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

    float32 FacialAnimationComponent::nextRandom()
    {
        uint32 state = _randomState;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        _randomState = state;
        return static_cast<float32>( state & 0xFFFFFFu ) / static_cast<float32>( 0x1000000u );
    }

    void FacialAnimationComponent::advanceTime( float32 deltaSeconds )
    {
        // 깜빡임: 타이머가 다 되면 길이만큼 감았다 뜨고, 다음 간격을 뽑는다.
        const FacialBlinkSettings& blink = _rig.getBlink();
        if ( _bBlink == SW_TRUE && blink._listTarget.empty() == false )
        {
            if ( _blinkElapsed >= 0.0f )
            {
                _blinkElapsed += deltaSeconds;
                if ( _blinkElapsed >= blink._duration )
                {
                    _blinkElapsed = -1.0f;
                    _blinkTimer   = FacialAnimationInternal::pickBetween( blink._minInterval, blink._maxInterval, nextRandom() );
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
        const FacialGazeSettings& gaze = _rig.getGaze();
        if ( _bSaccades == SW_TRUE && _bLookAt == SW_TRUE && gaze._listEyeBone.empty() == false )
        {
            _saccadeTimer -= deltaSeconds;
            if ( _saccadeTimer <= 0.0f )
            {
                const float32 amplitude = MathUtil::toRadian( gaze._saccadeAmplitudeDegrees );
                _saccadeOffset          = float2{ ( nextRandom() * 2.0f - 1.0f ) * amplitude, ( nextRandom() * 2.0f - 1.0f ) * amplitude };
                _saccadeTimer           = FacialAnimationInternal::pickBetween( gaze._saccadeMinInterval, gaze._saccadeMaxInterval, nextRandom() );
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

    void FacialAnimationComponent::refreshBindings( const SkeletalMeshComponent& unit )
    {
        const Mesh*     pMesh     = unit.getRawMesh();
        const Skeleton* pSkeleton = &unit.getSkeleton();
        if ( pMesh == _pBoundMesh && pSkeleton == _pBoundSkeleton )
            return;
        _pBoundMesh     = pMesh;
        _pBoundSkeleton = pSkeleton;

        vector<hashed_string> listMorphTargetName;
        if ( pMesh != nullptr )
        {
            for ( const MeshMorphTarget& target : pMesh->getMorphTargets() )
                listMorphTargetName.push_back( target._name );
        }
        (void)_rig.validate( listMorphTargetName, *pSkeleton, _facialRigPath.empty() ? string_view( "(runtime rig)" ) : string_view( _facialRigPath ) );

        _listExpressionTarget.resize( _rig.getExpressions().size() );
        for ( size_t poseIndex = 0; poseIndex < _rig.getExpressions().size(); ++poseIndex )
            FacialAnimationInternal::mapTargets( unit, _rig.getExpressions()[poseIndex], _listExpressionTarget[poseIndex] );
        _listVisemeTarget.resize( _rig.getVisemes().size() );
        for ( size_t poseIndex = 0; poseIndex < _rig.getVisemes().size(); ++poseIndex )
            FacialAnimationInternal::mapTargets( unit, _rig.getVisemes()[poseIndex], _listVisemeTarget[poseIndex] );
        _listBlinkTarget.clear();
        for ( const hashed_string& target : _rig.getBlink()._listTarget )
            _listBlinkTarget.push_back( unit.findMorphTargetIndex( target ) );
        _listEyeBone.clear();
        for ( const hashed_string& bone : _rig.getGaze()._listEyeBone )
            _listEyeBone.push_back( pSkeleton->findBoneIndex( bone ) );
    }

    void FacialAnimationComponent::addPose( SkeletalMeshComponent& unit, const vector<int32>& listTargetIndex, const FacialPose& pose, float32 weight ) const
    {
        if ( weight == 0.0f )
            return;
        for ( size_t targetIndex = 0; targetIndex < listTargetIndex.size() && targetIndex < pose._listTarget.size(); ++targetIndex )
        {
            if ( listTargetIndex[targetIndex] >= 0 )
                unit.addMorphWeight( static_cast<uint32>( listTargetIndex[targetIndex] ), pose._listTarget[targetIndex]._weight * weight );
        }
    }

    void FacialAnimationComponent::applyFace( SkeletalMeshComponent& unit )
    {
        refreshBindings( unit );

        // 표정 — 명시 가중치 + 같은 오브젝트 애니메이터의 같은 이름 커브(애니메이터의 시간 단계가 이미 이번 프레임 커브를 냈다).
        const SkeletalAnimatorComponent* pAnimator = nullptr;
        if ( _animator.isValid() && unit.getOwner() != nullptr && unit.getOwner()->getManager() != nullptr )
            pAnimator = castTo<SkeletalAnimatorComponent>( unit.getOwner()->getManager()->resolveComponent( _animator ) );
        const vector<FacialPose>& listExpression = _rig.getExpressions();
        for ( size_t poseIndex = 0; poseIndex < listExpression.size(); ++poseIndex )
        {
            float32 weight = _listExpressionWeight[poseIndex];
            if ( pAnimator != nullptr )
                weight += pAnimator->getCurveValue( listExpression[poseIndex]._name );
            addPose( unit, _listExpressionTarget[poseIndex], listExpression[poseIndex], weight );
        }

        // 립싱크 — 트랙이면 그 프레임의 비즘 가중치, 아니면 진폭이 대체 비즘을 연다.
        if ( _bSpeaking == SW_TRUE )
        {
            const vector<FacialPose>& listViseme = _rig.getVisemes();
            if ( _speechSampleRate > 0 )
            {
                const float32 windowSeconds = 1.0f / MathUtil::max( _lipSync._frameRate, 1.0f );
                const float32 openness      = LipSyncAnalyzer::computeOpenness( LipSyncAnalyzer::computeRms( _listSpeechSample, _speechSampleRate, _speechTime, windowSeconds ),
                                                                                _lipSync );
                const int32   visemeIndex   = _rig.findVisemeIndex( _lipSync._fallbackViseme );
                if ( visemeIndex >= 0 )
                    addPose( unit, _listVisemeTarget[static_cast<size_t>( visemeIndex )], listViseme[static_cast<size_t>( visemeIndex )], openness );
            }
            else
            {
                _speechTrack.sample( _speechTime, _listScratchVisemeWeight );
                for ( size_t trackViseme = 0; trackViseme < _listScratchVisemeWeight.size() && trackViseme < _listTrackToRigViseme.size(); ++trackViseme )
                {
                    const int32 visemeIndex = _listTrackToRigViseme[trackViseme];
                    if ( visemeIndex >= 0 )
                        addPose( unit, _listVisemeTarget[static_cast<size_t>( visemeIndex )], listViseme[static_cast<size_t>( visemeIndex )],
                                 _listScratchVisemeWeight[trackViseme] );
                }
            }
        }

        // 깜빡임
        for ( const int32 targetIndex : _listBlinkTarget )
        {
            if ( targetIndex >= 0 && _blinkAmount > 0.0f )
                unit.addMorphWeight( static_cast<uint32>( targetIndex ), _blinkAmount );
        }

        applyGaze( unit );
    }

    void FacialAnimationComponent::applyGaze( SkeletalMeshComponent& unit )
    {
        if ( _bLookAt == SW_FALSE || _bLookAtModelReady == SW_FALSE || _listEyeBone.empty() )
            return;
        const FacialGazeSettings& gaze       = _rig.getGaze();
        const vector<float4x4>&   listModel  = unit.getModelSpaceTransforms();
        const vector<int32>&      listParent = unit.getSkeleton().getParentIndices();
        Pose&                     pose       = unit.getLocalPose();
        quaternion*               pRotation  = pose.getRotationData();
        const float32             maxAngle   = MathUtil::toRadian( gaze._maxAngleDegrees );
        const quaternion          saccade    = quaternion::createFromYawPitchRoll( _saccadeOffset._x, _saccadeOffset._y, 0.0f );
        for ( const int32 boneIndex : _listEyeBone )
        {
            if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= listModel.size() || static_cast<uint32>( boneIndex ) >= pose.getBoneCount() )
                continue;
            // 부모 공간에서 돌린다 — 지금 로컬 회전의 앞 방향을 목표 방향으로 돌리는 가장 작은 회전을 최대 각으로 자르고 로컬 회전 뒤에 붙인다.
            const int32    parentIndex = listParent[static_cast<size_t>( boneIndex )];
            const float4x4 toParent    = parentIndex >= 0 ? listModel[static_cast<size_t>( parentIndex )].invert() : float4x4::Identity;
            const float3   eyePosition = listModel[static_cast<size_t>( boneIndex )].getTranslation();
            float3         desired     = float3::transformVector( _lookAtModel - eyePosition, toParent );
            if ( desired.getLengthSquared() <= MathUtil::Epsilon )
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
