// 합성 리그 벤치 — 본 수(대표 합성 리그 20 · 60 · 150 · 300) × 캐릭터 수(1 · 100 · 1000) × 클립 혼합(짝수 clip_00 · 홀수 clip_01, 시작 시각 엇갈림)에서
// 애니메이션 갱신 한 프레임(`AnimationSystem::evaluate` — 포즈 평가 · 모프 커브 · 후처리 리그 · 스킨 팔레트)의 게임 스레드 비용. 값은 Release 로 읽는다.
// GPU 스키닝 시간(`GPU.Compute`)은 여기 없다 — 런타임이 읽는 임포트 결과(.mesh)가 커밋 한도를 넘어 App 벤치 씬에 싣지 않았다(docs/08_Verification.md).
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "EditorTest/Common/Asset/SyntheticRigTestUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( SyntheticRigBenchTest, "animation update cost over synthetic rigs (bones x characters x clip mix)" );
SW_TEST_TIME_BUDGET( SyntheticRigBenchTest, 45000, "Release 케이스 최대 11 s(2026-10-17, Ryzen 7 6800H, 다섯 번 중 최대)의 네 배 — CI 러너 여유" );

namespace
{
    struct SyntheticRigBenchInternal
    {
        static constexpr float32 kFrame       = 1.0f / 60.0f;
        static constexpr uint32  kWarmupFrame = 5;

        /** @brief 캐릭터가 무엇을 도는지입니다. */
        enum class Mode : uint8
        {
            Own,         ///< 캐릭터마다 자기 포즈(군중 공유 끔)
            OwnRig,      ///< Own + 후처리 리그(다리 TwoBoneIK · 사슬 SpringChain)
            OwnFullBody, ///< Own + 전신 IK 리그(`FullBodyIK` — 몸 사슬 넷 · 골반 반복 4, 계획 AnimationAdvancedFeatures A1)
            Crowd,       ///< 군중 포즈 공유(같은 클립 · 위상 슬롯이면 버킷 하나가 평가)
        };

        /** @brief 전신 IK 리그가 묶는 사슬 수입니다(다리 · 팔 가운데 앞에서부터). */
        static constexpr uint32 kFullBodyChainCount = 4;
        /** @brief 전신 IK 의 골반 이동 상한(m)과 목표를 레퍼런스 끝에서 밀어 두는 거리(m)입니다 — 목표가 닿지 않아야 골반 풀이가 실제로 돈다. */
        static constexpr float32 kFullBodyMaxPelvisOffset = 0.3f;
        static constexpr float32 kFullBodyTargetPush      = 0.3f;

        static const utf8* getModeName( Mode mode )
        {
            switch ( mode )
            {
                case Mode::Own:
                    return "own";
                case Mode::OwnRig:
                    return "own+rig";
                case Mode::OwnFullBody:
                    return "own+fullbody";
                case Mode::Crowd:
                    return "crowd";
            }
            return "?";
        }

        /** @brief 뼈 이름 뒤에 두 자리 번호를 붙입니다(`leg_l` 와 3 → `leg_l_03`). */
        static sw::string makeBoneName( const sw::string& prefix, uint32 index )
        {
            return prefix + "_" + ( index < 10 ? "0" : "" ) + sw::to_string( index ).c_str();
        }

        /**
         * @brief 합성 리그의 몸 사슬(`leg_*_00` · `arm_*_00` 로 시작하는 뼈줄)을 앞에서 넷 모아 `FullBodyIK` 하나를 가진 리그를 만듭니다.
         * @details 골반은 루트(`root`)이고 사슬 뿌리는 모두 그 자손이다. 목표는 사슬 끝의 바인드 자리에서 앞으로 `kFullBodyTargetPush` 밀어 두어 닿지 않게 한다 —
         *          그래야 골반 반복이 끝까지 돈다(비용 상한). 사슬이 하나도 없으면 false 입니다.
         */
        static bool makeFullBodyRig( const test::SyntheticRig& rig, sw::shared_ptr<const sw::RigAsset>& outRig )
        {
            const sw::Skeleton& skeleton = *rig._skeleton;
            sw::Pose            pose;
            pose.setToReference( skeleton );
            sw::vector<sw::float4x4> listModel;
            pose.computeModelSpace( skeleton.getParentIndices(), listModel );
            const sw::float3 rootPosition = listModel[0].getTranslation();

            sw::string targets;
            sw::string chains;
            uint32     chainCount = 0;
            for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount() && chainCount < kFullBodyChainCount; ++boneIndex )
            {
                const sw::string name  = skeleton.getBone( boneIndex )._name.c_str();
                const bool       bLimb = sw::StringUtil::startsWith( name, "leg_" ) || sw::StringUtil::startsWith( name, "arm_" );
                if ( bLimb == false || sw::StringUtil::endsWith( name, "_00" ) == false )
                    continue;
                const sw::string prefix = name.substr( 0, name.size() - 3 );
                sw::string       bones;
                int32            lastBone = -1;
                for ( uint32 index = 0;; ++index )
                {
                    const int32 found = skeleton.findBoneIndex( sw::hashed_string( makeBoneName( prefix, index ) ) );
                    if ( found < 0 )
                        break;
                    bones += "<item>" + makeBoneName( prefix, index ) + "</item>";
                    lastBone = found;
                }
                const bool bNotChain = bones.empty() || lastBone < 0;
                if ( bNotChain || lastBone == static_cast<int32>( boneIndex ) )
                    continue; // 뼈 하나짜리 줄은 사슬이 아니다
                const sw::string chainName = "chain" + sw::to_string( chainCount );
                sw::float3       target    = listModel[static_cast<size_t>( lastBone )].getTranslation() - rootPosition;
                target._z += kFullBodyTargetPush;
                targets += "<item name=\"" + chainName + "\" bone=\"root\" translation=\"" + sw::XMLReadUtil::makeFloatListText( &target._x, 3 ) + "\"/>";
                chains += "<item target=\"" + chainName + "\" weight=\"1\"><bones>" + bones + "</bones></item>";
                ++chainCount;
            }
            if ( chainCount == 0 )
                return false;
            const sw::string xml = "<RigAsset><targets>" + targets + "</targets><nodes><item type=\"FullBodyIK\" name=\"full_body\" pelvis=\"root\" max_pelvis_offset=\"" +
                                   sw::XMLReadUtil::makeFloatListText( &kFullBodyMaxPelvisOffset, 1 ) + "\" pelvis_iterations=\"4\"><chains>" + chains + "</chains></item></nodes></RigAsset>";
            sw::shared_ptr<sw::RigAsset> built = sw::make_shared<sw::RigAsset>();
            if ( built->parseXML( xml, "synthetic full body rig" ) == false )
                return false;
            outRig = built;
            return true;
        }

        /** @brief 디코드 비용 벤치의 원본 표본율(Hz)입니다 — 임포트 규칙의 기본과 같다. */
        static constexpr float32 kDecodeSampleRate = 30.0f;
        /** @brief 디코드 한 판의 호출 수와 판 수입니다(가장 짧은 판을 쓴다). */
        static constexpr uint32 kDecodeOpsPerRound = 2000;
        static constexpr uint32 kDecodeRoundCount  = 5;

        /** @brief 임포트한 첫 클립을 스켈레톤 본 순서의 원본 클립(30 Hz 균일 표본)으로 다시 뽑습니다 — 코덱마다 같은 입력을 압축하려는 것이다. */
        static bool makeRawClip( const test::SyntheticRig& rig, sw::AnimationRawClip& outRaw )
        {
            const sw::AnimationClip& clip     = rig._import._listClip[0]._clip;
            const sw::Skeleton&      skeleton = *rig._skeleton;
            const float32            duration = clip.getDuration();
            if ( duration <= 0.0f )
                return false;
            outRaw              = sw::AnimationRawClip{};
            outRaw._sampleCount = sw::MathUtil::max( 2u, static_cast<uint32>( sw::MathUtil::ceil( duration * kDecodeSampleRate ) ) + 1u );
            outRaw._sampleRate  = static_cast<float32>( outRaw._sampleCount - 1 ) / duration;
            for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
            {
                outRaw._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
                outRaw._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
            }
            sw::vector<int32> listTrackToBone;
            clip.makeTrackToBoneMap( skeleton, listTrackToBone );
            sw::Pose trackPose;
            sw::Pose bonePose;
            for ( uint32 sampleIndex = 0; sampleIndex < outRaw._sampleCount; ++sampleIndex )
            {
                const float32 time = sw::MathUtil::min( static_cast<float32>( sampleIndex ) / outRaw._sampleRate, duration );
                bonePose.setToReference( skeleton );
                if ( clip.sampleTracks( time, trackPose ) == false )
                    return false;
                sw::AnimationClip::copyTracksToPose( trackPose, listTrackToBone, bonePose );
                for ( uint32 boneIndex = 0; boneIndex < bonePose.getBoneCount(); ++boneIndex )
                {
                    outRaw._listSample.push_back( bonePose.getBoneTransform( boneIndex ) );
                }
            }
            return true;
        }

        /** @brief 캐릭터 수가 많으면 프레임 수를 줄인다 — p99 는 표본 수가 적으면 최댓값에 가깝다(보고에 표본 수가 찍힌다). */
        static uint32 getFrameCount( uint32 characterCount ) { return characterCount >= 1000 ? 60u : 120u; }

        /** @brief 캐릭터 @p characterCount 명을 스폰해 프레임마다 `evaluate` 를 측정합니다. 평가가 실제로 돌았는지 확인합니다. */
        static bool measure( const test::SyntheticRig& rig, uint32 characterCount, Mode mode, sw::vector<int64>& outListMicro )
        {
            sw::shared_ptr<const sw::RigAsset> postProcessRig = rig._rig;
            if ( mode == Mode::OwnFullBody && makeFullBodyRig( rig, postProcessRig ) == false )
                return false;
            sw::GameObjectManager manager;
            sw::AnimationSystem&  system = manager.getSubsystem<sw::AnimationSystem>();
            system.setCrowdSettings( sw::AnimationCrowdSettings{} );
            const float32 duration = rig._import._listClip[0]._clip.getDuration();
            for ( uint32 index = 0; index < characterCount; ++index )
            {
                sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( ( sw::string( "Unit" ) + sw::to_string( index ).c_str() ).c_str() ) );
                if ( pObject == nullptr )
                    return false;
                sw::SkeletalMeshComponent*     pUnit     = pObject->addComponent<sw::SkeletalMeshComponent>();
                sw::SkeletalAnimatorComponent* pAnimator = pObject->addComponent<sw::SkeletalAnimatorComponent>();
                if ( pUnit == nullptr || pAnimator == nullptr )
                    return false;
                pUnit->setShareCrowdPose( mode == Mode::Crowd );
                pUnit->setSkeleton( rig._skeleton );
                pUnit->setMesh( rig._mesh );
                pUnit->resolveRenderAssets();
                pAnimator->setClipFolder( rig._clipFolder );
                pAnimator->setInitialState( ( index % 2 ) == 0 ? "clip_00" : "clip_01" );
                pAnimator->setInitialTime( duration * static_cast<float32>( ( index * 37u ) % 100u ) / 100.0f );
                pAnimator->dispatchBeginPlay();
                pAnimator->waitForAnimationAssets(); // 시작의 클립 읽기는 비동기다 — 읽히기 전의 유닛은 포즈를 만들지 않아 평가 수 확인이 진다
                if ( mode == Mode::OwnRig || mode == Mode::OwnFullBody )
                {
                    sw::PoseModifierComponent* pModifier = pObject->addComponent<sw::PoseModifierComponent>();
                    if ( pModifier == nullptr )
                        return false;
                    pModifier->setRigAsset( postProcessRig );
                    pModifier->dispatchBeginPlay();
                    if ( pModifier->isRigReady() == false )
                        return false;
                }
            }
            for ( uint32 frame = 0; frame < kWarmupFrame; ++frame )
            {
                system.evaluate( kFrame );
            }
            // 군중은 유닛이 포즈를 만들지 않고 버킷이 만든다. 나머지는 유닛마다 만든다 — 아니면 측정하는 것이 없다.
            const bool bWorked = mode == Mode::Crowd ? ( system.getPoseEvaluatedUnitCount() == 0 && system.getCrowd().getEvaluatedBucketCount() > 0 )
                                                     : system.getPoseEvaluatedUnitCount() == characterCount;
            if ( bWorked == false )
                return false;
            outListMicro.clear();
            const uint32 frameCount = getFrameCount( characterCount );
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                const sw::Stopwatch watch;
                system.evaluate( kFrame );
                outListMicro.push_back( watch.getElapsedNanoseconds() / 1000 );
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [SyntheticRigBenchTest] 대표 합성 리그 네 개 × 캐릭터 1 · 100 · 1000 × (자기 포즈 · 자기 포즈 + 리그 · 자기 포즈 + 전신 IK · 군중 공유)의 `AnimationSystem::evaluate` 한 프레임
 * @details 캐릭터는 짝수가 clip_00, 홀수가 clip_01 을 엇갈린 시각에서 돈다(합성 클립은 관절마다 위상이 다른 사인 — 모든 값이 움직인다).
 *          워밍업 5 프레임 뒤 1/60 초 프레임을 120 번(캐릭터 1000 은 60 번) 측정한다. 엔진 서비스가 묶여 있으면 캐릭터 평가는 워커로 나뉜다(`runParallelByCost`).
 */
SW_TEST_CASE( SyntheticRigBenchTest, EvaluateByBonesCharactersAndMode )
{
    using Internal = SyntheticRigBenchInternal;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::vector<sw::string> listPath = test::collectSyntheticRigPaths();
    SW_ASSERT_TRUE( listPath.empty() == false );
    SW_LOG_INFO( "[Bench] synthetic rig  engine services bound (parallel units): %#", sw::engine::areEngineServicesBound() ? "yes" : "no" );
    for ( const sw::string& glbPath : listPath )
    {
        test::SyntheticRig rig;
        SW_ASSERT_TRUE_MSG( test::loadSyntheticRig( glbPath, rig ), glbPath.c_str() );
        SW_ASSERT_TRUE( rig._import._listClip.size() >= 2 ); // 클립 혼합은 clip_00 · clip_01 둘이 필요하다
        for ( const uint32 characterCount : { 1u, 100u, 1000u } )
        {
            for ( const Internal::Mode mode : { Internal::Mode::Own, Internal::Mode::OwnRig, Internal::Mode::OwnFullBody, Internal::Mode::Crowd } )
            {
                sw::vector<int64> listMicro;
                const sw::string  label = "synthetic rig  " + rig._stem + "  x" + sw::to_string( characterCount ) + "  " + Internal::getModeName( mode ) + ": evaluate";
                SW_EXPECT_TRUE_MSG( Internal::measure( rig, characterCount, mode, listMicro ), label.c_str() );
                test::logBenchSamples( label.c_str(), listMicro );
                // 가장 느린 프레임의 번호 — 처음 몇 프레임에 몰리면 첫 접근(페이지 · 캐시) 비용이고, 흩어져 있으면 스케줄러 · 판 사이 흔들림이다(AnimationUpdateAnomalies 의 p99 꼬리).
                size_t slowestIndex = 0;
                for ( size_t index = 1; index < listMicro.size(); ++index )
                {
                    if ( listMicro[index] > listMicro[slowestIndex] )
                        slowestIndex = index;
                }
                SW_LOG_INFO( "[Bench] %#  slowest frame #%# of %#", label, static_cast<uint32>( slowestIndex ), static_cast<uint32>( listMicro.size() ) );
            }
        }
    }
}

/**
 * @brief [SyntheticRigBenchTest] 코덱별 디코드 비용 — 대표 합성 리그 네 개의 첫 클립을 30 Hz 원본으로 다시 뽑아 Raw 와 ACL 로 압축하고, 같은 시각 열을 샘플하는 데 드는 시간(호출당 ns)과
 *        압축 크기 · 압축률 · 최대 오차를 찍는다. ACL 은 트랙 절반을 끈 마스크(본 LOD)도 잰다 — 계획 AnimationAdvancedFeatures A3 가 샘플(디코드)의 몫을 코덱 선택의 근거로 삼는다.
 * @details ACL 의 키 줄이기는 가상 정점(`_shellDistance`)의 모델 공간 오차를 계층(`_listTrackParent`)을 따라 잰다 — 오차 예산을 본 깊이로 나누는 일을 라이브러리가 한다.
 *          값은 Release 로 읽는다. 가장 짧은 판의 호출당 시간이라 스케줄러가 끼어든 판은 버려진다.
 */
SW_TEST_CASE( SyntheticRigBenchTest, DecodeCostByCodec )
{
    using Internal = SyntheticRigBenchInternal;
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::vector<sw::string> listPath = test::collectSyntheticRigPaths();
    SW_ASSERT_TRUE( listPath.empty() == false );
    for ( const sw::string& glbPath : listPath )
    {
        test::SyntheticRig rig;
        SW_ASSERT_TRUE_MSG( test::loadSyntheticRig( glbPath, rig ), glbPath.c_str() );
        sw::AnimationRawClip raw;
        SW_ASSERT_TRUE_MSG( Internal::makeRawClip( rig, raw ), glbPath.c_str() );
        const float32 duration   = raw.getDuration();
        const uint32  trackCount = raw.getTrackCount();
        // 마스크: 홀수 번째 트랙을 끈다(본 LOD 로 반을 디코드하지 않는 경우).
        sw::vector<uint8> listHalfMask( trackCount, 1 );
        for ( uint32 trackIndex = 1; trackIndex < trackCount; trackIndex += 2 )
        {
            listHalfMask[trackIndex] = 0;
        }
        for ( const sw::AnimationCodecID codecID : { sw::AnimationCodecID::Raw, sw::AnimationCodecID::ACL } )
        {
            const sw::IAnimationCodec* pCodec = sw::AnimationCodecRegistry::findCodec( codecID );
            SW_ASSERT_TRUE( pCodec != nullptr );
            sw::vector<uint8>       bytes;
            sw::AnimationCodecStats stats;
            SW_ASSERT_TRUE_MSG( sw::AnimationCodecRegistry::compressAndMeasure( *pCodec, raw, sw::AnimationCodecSettings{}, bytes, stats ), rig._stem.c_str() );
            // 블롭은 16 바이트 정렬로 보관한다(ACL 의 요구).
            sw::vector<sw::AnimationCodecBlock> listBlock( ( bytes.size() + 15 ) / 16 );
            std::memcpy( listBlock.data(), bytes.data(), bytes.size() );
            const uint8* pBlob = reinterpret_cast<const uint8*>( listBlock.data() );
            SW_LOG_INFO( "[Bench] codec decode  %#  %#: %# tracks  %# bytes  ratio %#  max error %# m", rig._stem.c_str(), pCodec->getName(), trackCount, stats._compressedByteCount,
                         stats.computeRatio(), stats._maxError );
            const bool bMaskable = codecID == sw::AnimationCodecID::ACL;
            for ( const bool bHalfMask : { false, true } )
            {
                if ( bHalfMask && bMaskable == false )
                    continue;
                sw::Pose         pose;
                bool             bAllSampled = true;
                const int64      deciNanos   = test::measureBestDeciNanosPerOp( Internal::kDecodeOpsPerRound, Internal::kDecodeRoundCount, [&]()
                       {
                    for ( uint32 operation = 0; operation < Internal::kDecodeOpsPerRound; ++operation )
                    {
                        const float32 time = duration * static_cast<float32>( ( operation * 37u ) % 100u ) / 100.0f;
                        bAllSampled        = pCodec->sample( pBlob, bytes.size(), time, pose, bHalfMask ? listHalfMask.data() : nullptr ) && bAllSampled;
                    }
                } );
                const sw::string label       = "codec decode  " + rig._stem + "  " + pCodec->getName() + ( bHalfMask ? "  half tracks" : "  all tracks" );
                SW_EXPECT_TRUE_MSG( bAllSampled, label.c_str() );
                test::logBenchDeciNanos( label.c_str(), deciNanos );
            }
        }
    }
}
