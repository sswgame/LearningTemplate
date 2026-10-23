// 합성 리그 벤치 — 본 수(대표 합성 리그 20 · 60 · 150 · 300) × 캐릭터 수(1 · 100 · 1000) × 클립 혼합(짝수 clip_00 · 홀수 clip_01, 시작 시각 엇갈림)에서
// 애니메이션 갱신 한 프레임(`AnimationSystem::evaluate` — 포즈 평가 · 모프 커브 · 후처리 리그 · 스킨 팔레트)의 게임 스레드 비용. 값은 Release 로 읽는다.
// GPU 스키닝 시간(`GPU.Compute`)은 여기 없다 — 런타임이 읽는 임포트 결과(.mesh)가 커밋 한도를 넘어 App 벤치 씬에 싣지 않았다(docs/08_Verification.md).
#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "EditorTest/Common/Asset/SyntheticRigTestUtil.h"

#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"

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
            Own,    ///< 캐릭터마다 자기 포즈(군중 공유 끔)
            OwnRig, ///< Own + 후처리 리그(다리 TwoBoneIK · 사슬 SpringChain)
            Crowd,  ///< 군중 포즈 공유(같은 클립 · 위상 슬롯이면 버킷 하나가 평가)
        };

        static const utf8* getModeName( Mode mode )
        {
            switch ( mode )
            {
                case Mode::Own:
                    return "own";
                case Mode::OwnRig:
                    return "own+rig";
                case Mode::Crowd:
                    return "crowd";
            }
            return "?";
        }

        /** @brief 캐릭터 수가 많으면 프레임 수를 줄인다 — p99 는 표본 수가 적으면 최댓값에 가깝다(보고에 표본 수가 찍힌다). */
        static uint32 getFrameCount( uint32 characterCount ) { return characterCount >= 1000 ? 60u : 120u; }

        /** @brief 캐릭터 @p characterCount 명을 스폰해 프레임마다 `evaluate` 를 측정합니다. 평가가 실제로 돌았는지 확인합니다. */
        static bool measure( const test::SyntheticRig& rig, uint32 characterCount, Mode mode, sw::vector<int64>& outListMicro )
        {
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
                if ( mode == Mode::OwnRig )
                {
                    sw::PoseModifierComponent* pModifier = pObject->addComponent<sw::PoseModifierComponent>();
                    if ( pModifier == nullptr )
                        return false;
                    pModifier->setRigAsset( rig._rig );
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
 * @brief [SyntheticRigBenchTest] 대표 합성 리그 네 개 × 캐릭터 1 · 100 · 1000 × (자기 포즈 · 자기 포즈 + 리그 · 군중 공유)의 `AnimationSystem::evaluate` 한 프레임
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
            for ( const Internal::Mode mode : { Internal::Mode::Own, Internal::Mode::OwnRig, Internal::Mode::Crowd } )
            {
                sw::vector<int64> listMicro;
                const sw::string  label = "synthetic rig  " + rig._stem + "  x" + sw::to_string( characterCount ) + "  " + Internal::getModeName( mode ) + ": evaluate";
                SW_EXPECT_TRUE_MSG( Internal::measure( rig, characterCount, mode, listMicro ), label.c_str() );
                test::logBenchSamples( label.c_str(), listMicro );
            }
        }
    }
}
