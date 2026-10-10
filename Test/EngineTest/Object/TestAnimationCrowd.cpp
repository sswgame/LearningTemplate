#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Object/Animation/AnimationCrowd.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Animation/VertexAnimationCooker.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationCrowdTest — 군중 포즈 공유(묶음 · 변형 칸 · 혼자 · VAT · 지우기)와 VAT 굽기.

namespace
{
    struct TestAnimationCrowdInternal
    {
        /** @brief 사슬 2 본(bone0 → bone1) 스켈레톤입니다. */
        static shared_ptr<Skeleton> makeSkeleton() { return make_shared<Skeleton>( test::makeChainSkeleton( 2 ) ); }

        /** @brief 단위 큐브의 위쪽 정점(y > 0)을 bone1, 아래쪽을 bone0 에 묶은 스킨드 메시입니다. */
        static shared_ptr<Mesh> makeSkinnedCube()
        {
            shared_ptr<Mesh>       cube = MeshUtil::createUnitCube();
            vector<MeshSkinVertex> listSkin;
            for ( const RHIVertex& vertex : cube->getVertices() )
            {
                MeshSkinVertex skin{};
                skin._arrJoint[0] = vertex._arrPosition[1] > 0.0f ? 1u : 0u;
                listSkin.push_back( skin );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            mesh->setVertices( cube->getVertices() );
            mesh->setSkin( listSkin, 2 );
            return mesh;
        }

        /** @brief 반복 클립 둘(`wave` · `sway`)을 임시 폴더에 쓰고 그 폴더를 돌려줍니다. */
        static string writeClips( const Skeleton& skeleton )
        {
            const string       folder     = test::makeTempPath( "crowdclips" );
            const AnimRawClip  wave       = test::makeChainRawClip( skeleton, 31, 30.0f, 0.6f );
            const AnimRawClip  sway       = test::makeChainRawClip( skeleton, 31, 30.0f, 1.2f );
            const AnimRawClip* arrRaw[2]  = { &wave, &sway };
            const utf8*        arrName[2] = { "Wave", "Sway" };
            for ( uint32 clipIndex = 0; clipIndex < 2; ++clipIndex )
            {
                AnimClip clip;
                clip.setName( hashed_string( arrName[clipIndex] ) );
                clip.setLooping( true );
                if ( clip.compressFrom( *arrRaw[clipIndex], RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) == false )
                    return string{};
                const string fileName = clipIndex == 0 ? "wave.animclip" : "sway.animclip";
                if ( clip.saveToFile( FileUtil::joinPath( folder, fileName ) ) == false )
                    return string{};
            }
            return folder;
        }

        /** @brief 군중 공유를 켠 캐릭터 하나(유닛 + 애니메이터)를 만들고 시작합니다. */
        static SkeletalMeshComponent* createCharacter( GameObjectManager& manager, const utf8* pName, const shared_ptr<Skeleton>& skeleton,
                                                       const shared_ptr<Mesh>& mesh, const string& folder, const utf8* pState, float32 startTime,
                                                       bool bShare = true )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
            SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
            if ( pUnit == nullptr || pAnimator == nullptr )
                return nullptr;
            pUnit->setShareCrowdPose( bShare );
            pUnit->setSkeleton( skeleton );
            pUnit->setMesh( mesh );
            pUnit->resolveRenderAssets();
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( pState );
            pAnimator->setInitialTime( startTime );
            pAnimator->dispatchBeginPlay();
            return pUnit;
        }

        /** @brief 유닛 bone1 의 로컬 회전입니다. */
        static quaternion getBoneRotation( const SkeletalMeshComponent& unit ) { return unit.getLocalPose().getBoneTransform( 1 )._rotation; }

        static bool isSameRotation( const quaternion& expected, const quaternion& actual, float32 tolerance )
        {
            return MathUtil::abs( MathUtil::abs( expected.dot( actual ) ) - 1.0f ) <= tolerance;
        }
    };
} // namespace

/**
 * @brief [AnimationCrowdTest] 같은 상태 · 같은 위상 칸의 유닛은 묶음 하나의 포즈 · 메시를 나눈다 — 묶음마다 평가 한 번, 유닛은 포즈 단계를 돌지 않는다
 * @details 칸 넷(길이 1 초 → 0.25 초 폭). 시작 시각 0 · 0.02 · 0.27 · 0.5 · 0.52 의 Wave 다섯은 칸 0 · 0 · 1 · 2 · 2 로, Sway 하나는 따로 — 묶음 넷.
 *          나눈 유닛의 포즈는 묶음 시각(시계 + 칸 × 0.25)의 클립 값이고, 유닛 자신의 시각과 반 칸 안이다.
 */
SW_TEST_CASE( AnimationCrowdTest, SameStateUnitsShareOneBucket )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const string               folder   = TestAnimationCrowdInternal::writeClips( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    AnimationSystem&  system = manager.getAnimationSystem();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    const float32          arrStart[5] = { 0.0f, 0.02f, 0.27f, 0.5f, 0.52f };
    SkeletalMeshComponent* arrUnit[6]  = {};
    for ( uint32 index = 0; index < 5; ++index )
    {
        const string name = string( "Wave" ) + to_string( index ).c_str();
        arrUnit[index]    = TestAnimationCrowdInternal::createCharacter( manager, name.c_str(), skeleton, mesh, folder, "Wave", arrStart[index] );
        SW_ASSERT_NOT_NULL( arrUnit[index] );
    }
    arrUnit[5] = TestAnimationCrowdInternal::createCharacter( manager, "Sway0", skeleton, mesh, folder, "Sway", 0.0f );
    SW_ASSERT_NOT_NULL( arrUnit[5] );

    system.evaluate( 0.0f );
    const AnimationCrowd& crowd = system.getCrowd();
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( crowd.getBuckets().size() ) );
    SW_EXPECT_EQUAL( 4u, crowd.getEvaluatedBucketCount() );
    for ( SkeletalMeshComponent* pUnit : arrUnit )
    {
        SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Shared );
        SW_ASSERT_NOT_NULL( pUnit->findCrowdBucket() );
        SW_EXPECT_TRUE( pUnit->getMesh() == pUnit->findCrowdBucket()->getMesh() );
        SW_EXPECT_EQUAL( 0u, pUnit->getPoseEvaluationCount() ); // 유닛은 포즈를 만들지 않았다 — 묶음이 만들었다
    }
    // 같은 칸이면 같은 묶음 · 같은 메시(한 배치), 다른 칸이면 다른 묶음.
    SW_EXPECT_TRUE( arrUnit[0]->findCrowdBucket() == arrUnit[1]->findCrowdBucket() );
    SW_EXPECT_TRUE( arrUnit[3]->findCrowdBucket() == arrUnit[4]->findCrowdBucket() );
    SW_EXPECT_TRUE( arrUnit[0]->findCrowdBucket() != arrUnit[2]->findCrowdBucket() );
    SW_EXPECT_TRUE( arrUnit[0]->findCrowdBucket() != arrUnit[5]->findCrowdBucket() );
    SW_EXPECT_TRUE( arrUnit[0]->getMesh() != mesh );                                  // 원본이 아니라 묶음의 사본을 그린다
    SW_EXPECT_EQUAL( mesh->getSkinDataId(), arrUnit[0]->getMesh()->getSkinDataId() ); // 사본은 원본의 스킨 데이터를 나눈다

    // 묶음의 포즈 = 묶음 시각의 클립 값, 유닛 시각과는 반 칸(0.125 초) 안.
    const AnimRawClip raw = test::makeChainRawClip( *skeleton, 31, 30.0f, 0.6f );
    for ( uint32 index = 0; index < 5; ++index )
    {
        const AnimationCrowdBucket* pBucket    = arrUnit[index]->findCrowdBucket();
        const float32               bucketTime = pBucket->computeTime( crowd.getClock() );
        SW_EXPECT_TRUE( MathUtil::abs( bucketTime - arrStart[index] ) <= 0.125f + 1e-4f );
        Pose expected;
        raw.sample( bucketTime, expected );
        SW_EXPECT_TRUE( TestAnimationCrowdInternal::isSameRotation( expected.getBoneTransform( 1 )._rotation, TestAnimationCrowdInternal::getBoneRotation( *arrUnit[index] ),
                                                                    1e-4f ) );
    }

    // 다음 프레임 — 묶음은 다시 한 번씩, 유닛은 여전히 0 번.
    system.evaluate( 0.1f );
    uint32 totalEvaluations = 0;
    for ( const unique_ptr<AnimationCrowdBucket>& bucket : crowd.getBuckets() )
    {
        totalEvaluations += bucket->getEvaluationCount();
    }
    SW_EXPECT_EQUAL( 8u, totalEvaluations );
    SW_EXPECT_EQUAL( 0u, system.getPoseEvaluatedUnitCount() );
}

/**
 * @brief [AnimationCrowdTest] 섞는 중에는 나눌 수 없다 — 사본 풀의 메시로 혼자 평가하고, 섞기가 끝나면 다시 묶음으로 돌아가 사본을 풀에 돌려준다
 */
SW_TEST_CASE( AnimationCrowdTest, CrossfadeFallsBackToSoloThenRejoins )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const string               folder   = TestAnimationCrowdInternal::writeClips( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    AnimationSystem&  system = manager.getAnimationSystem();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    SkeletalMeshComponent* pUnit = TestAnimationCrowdInternal::createCharacter( manager, "Hero", skeleton, mesh, folder, "Wave", 0.0f );
    SW_ASSERT_NOT_NULL( pUnit );
    system.evaluate( 0.0f );
    SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Shared );

    SkeletalAnimatorComponent* pAnimator = pUnit->getOwner()->getComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    SW_ASSERT_TRUE( pAnimator->play( hashed_string( "Sway" ), true, 0.3f ) );
    system.evaluate( 0.05f );
    SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Solo );
    SW_EXPECT_NULL( pUnit->findCrowdBucket() );
    SW_EXPECT_TRUE( pUnit->getMesh() != mesh );
    SW_EXPECT_EQUAL( mesh->getSkinDataId(), pUnit->getMesh()->getSkinDataId() );
    const uint32 soloEvaluations = pUnit->getPoseEvaluationCount();
    SW_EXPECT_TRUE( soloEvaluations >= 1u ); // 혼자 평가한다
    const Mesh* pSoloMesh = pUnit->getRawMesh();

    // 섞기가 끝나면(0.3 초) Sway 묶음으로 돌아간다.
    for ( uint32 frame = 0; frame < 8; ++frame )
    {
        system.evaluate( 0.05f );
    }
    SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == hashed_string( "Sway" ) );
    SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Shared );
    SW_ASSERT_NOT_NULL( pUnit->findCrowdBucket() );
    SW_EXPECT_TRUE( &pUnit->findCrowdBucket()->getClip() == pAnimator->findClip( hashed_string( "Sway" ) ) );

    // 돌려받은 사본은 다음에 혼자가 될 때 다시 쓴다(새로 만들지 않는다).
    SW_ASSERT_TRUE( pAnimator->play( hashed_string( "Wave" ), true, 0.3f ) );
    system.evaluate( 0.05f );
    SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Solo );
    SW_EXPECT_TRUE( pUnit->getRawMesh() == pSoloMesh );
}

/**
 * @brief [AnimationCrowdTest] 아주 먼 유닛은 VAT 메시로 넘어간다 — 같은 클립의 유닛은 VAT 메시 하나를 나누고, 시각 오프셋은 유닛 시각 - 군중 시계다.
 *        따르는 유닛(리더 포즈)이 있는 유닛은 넘어가지 않는다.
 */
SW_TEST_CASE( AnimationCrowdTest, FarUnitsSwitchToVertexAnimation )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const string               folder   = TestAnimationCrowdInternal::writeClips( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager manager;
    AnimationSystem&  system = manager.getAnimationSystem();
    system.setCrowdSettings( AnimationCrowdSettings{} );
    system.getCrowd().setClock( 10.0 );
    SkeletalMeshComponent* pFarA   = TestAnimationCrowdInternal::createCharacter( manager, "FarA", skeleton, mesh, folder, "Wave", 0.3f );
    SkeletalMeshComponent* pFarB   = TestAnimationCrowdInternal::createCharacter( manager, "FarB", skeleton, mesh, folder, "Wave", 0.6f );
    SkeletalMeshComponent* pLeader = TestAnimationCrowdInternal::createCharacter( manager, "Leader", skeleton, mesh, folder, "Wave", 0.0f );
    SW_ASSERT_TRUE( pFarA != nullptr && pFarB != nullptr && pLeader != nullptr );
    // 리더를 따르는 부품 — 리더는 따르는 유닛이 있어 VAT 로 넘어가지 않는다.
    GameObject*            pPartObject = manager.createGameObject( hashed_string( "Helmet" ) );
    SkeletalMeshComponent* pPart       = pPartObject != nullptr ? pPartObject->addComponent<SkeletalMeshComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pPart );
    pPart->setSkeleton( skeleton );
    pPart->setLeaderPose( pLeader );

    AnimationLODState farState{};
    farState._bVertexAnimation = SW_TRUE;
    for ( SkeletalMeshComponent* pUnit : { pFarA, pFarB, pLeader } )
    {
        pUnit->applyAnimationLOD( farState );
    }
    system.evaluate( 0.0f );

    SW_EXPECT_TRUE( pFarA->getCrowdMode() == AnimationCrowdMode::VertexAnimation );
    SW_EXPECT_TRUE( pFarB->getCrowdMode() == AnimationCrowdMode::VertexAnimation );
    SW_EXPECT_TRUE( pLeader->getCrowdMode() == AnimationCrowdMode::Shared );
    SW_EXPECT_TRUE( pFarA->getMesh() == pFarB->getMesh() ); // VAT 메시 하나 — 한 배치
    SW_ASSERT_NOT_NULL( pFarA->getMesh()->findVertexAnimation() );
    SW_EXPECT_FALSE( pFarA->getMesh()->hasSkin() );
    SW_EXPECT_EQUAL( 1u, system.getCrowd().getVertexAnimationMeshCount() );
    SW_EXPECT_NEAR_EQUAL( 0.3f - 10.0f, pFarA->getVertexAnimationPhase(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.6f - 10.0f, pFarB->getVertexAnimationPhase(), 1e-4f );

    // 시각이 흘러도 오프셋은 그대로(VAT 시계와 같이 흐른다) — 인스턴스를 다시 올리지 않는다.
    system.evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.3f - 10.0f, pFarA->getVertexAnimationPhase(), 1e-4f );

    // 가까워지면 묶음으로 돌아온다.
    pFarA->applyAnimationLOD( AnimationLODState{} );
    system.evaluate( 0.1f );
    SW_EXPECT_TRUE( pFarA->getCrowdMode() == AnimationCrowdMode::Shared );
    SW_EXPECT_TRUE( pFarA->getMesh()->hasSkin() );
}

/**
 * @brief [AnimationCrowdTest] VAT 굽기 — 프레임 시각의 표 위치는 그 시각 포즈로 CPU 스키닝한 위치와 같고, 프레임 사이는 선형 보간, 반복 클립은 끝에서 처음으로 감긴다
 */
SW_TEST_CASE( AnimationCrowdTest, VertexAnimationBakeMatchesSkinning )
{
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const AnimRawClip          raw      = test::makeChainRawClip( *skeleton, 31, 30.0f, 0.6f );
    AnimClip                   clip;
    clip.setLooping( true );
    SW_ASSERT_TRUE( clip.compressFrom( raw, RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) );

    MeshVertexAnimation animation;
    SW_ASSERT_TRUE( MeshVertexAnimationBaker::bake( *mesh, *skeleton, clip, 10.0f, false, animation ) );
    SW_EXPECT_EQUAL( 10u, animation._frameCount ); // 반복 — 끝 프레임은 처음과 같아 싣지 않는다
    SW_EXPECT_EQUAL( mesh->getVertexCount(), animation._vertexCount );
    SW_EXPECT_NEAR_EQUAL( 10.0f, animation._framesPerSecond, 1e-4f );

    // 프레임 시각 0.3 초에서 CPU 스키닝과 같다.
    vector<int32> listTrackToBone;
    clip.makeTrackToBoneMap( *skeleton, listTrackToBone );
    auto skinAt = [&]( float32 time, vector<float3>& outListPosition )
    {
        Pose             pose;
        Pose             scratch;
        vector<float4x4> listModel;
        vector<float4x4> listPalette;
        vector<float3>   listNormal;
        pose.setToReference( *skeleton );
        (void)clip.samplePose( time, listTrackToBone, pose, scratch, false );
        pose.computeModelSpace( skeleton->getParentIndices(), listModel );
        Pose::computeSkinPalette( *skeleton, listModel, listPalette );
        MeshVertexAnimationBaker::skinVertices( *mesh, listPalette, outListPosition, listNormal );
    };
    vector<float3> listExpected;
    skinAt( 0.3f, listExpected );
    float32 maxError = 0.0f;
    for ( uint32 vertexIndex = 0; vertexIndex < animation._vertexCount; ++vertexIndex )
    {
        maxError = MathUtil::max( maxError, ( animation.samplePosition( 0.3f, vertexIndex ) - listExpected[vertexIndex] ).getLength() );
    }
    SW_EXPECT_TRUE( maxError < 1e-4f );
    // 표가 레스트 포즈가 아니다(클립이 정점을 옮겼다).
    float32 maxMove = 0.0f;
    for ( uint32 vertexIndex = 0; vertexIndex < animation._vertexCount; ++vertexIndex )
    {
        const float3 rest{ mesh->getVertices()[vertexIndex]._arrPosition[0], mesh->getVertices()[vertexIndex]._arrPosition[1], mesh->getVertices()[vertexIndex]._arrPosition[2] };
        maxMove = MathUtil::max( maxMove, ( animation.samplePosition( 0.3f, vertexIndex ) - rest ).getLength() );
    }
    SW_EXPECT_TRUE( maxMove > 0.05f );
    // 감기 — 1.3 초는 0.3 초와 같다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, ( animation.samplePosition( 1.3f, 5 ) - animation.samplePosition( 0.3f, 5 ) ).getLength(), 1e-5f );

    // 노멀 담기 — 팔면체 12 + 12 비트, 왕복 오차는 1 도 안쪽.
    const float3 arrNormal[4] = {
        float3{ 0.0f,  1.0f,   0.0f},
        float3{ 0.3f, -0.8f,  0.52f}
            .normalize(),
        float3{-0.6f,  0.1f, -0.79f}
            .normalize(),
        float3{ 0.0f,  0.0f,  -1.0f}
    };
    for ( const float3& normal : arrNormal )
    {
        SW_EXPECT_TRUE( normal.dot( MeshVertexAnimation::unpackNormal( MeshVertexAnimation::packNormal( normal ) ) ) > 0.9998f );
    }
}

/**
 * @brief [AnimationCrowdTest] 멤버 · 참조가 없는 묶음은 쉰 지 `bucket_keep_seconds` 가 지나면 지운다 — 그 전에는 남긴다
 */
SW_TEST_CASE( AnimationCrowdTest, IdleBucketsRetireAfterKeepSeconds )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const string               folder   = TestAnimationCrowdInternal::writeClips( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );

    GameObjectManager      manager;
    AnimationSystem&       system   = manager.getAnimationSystem();
    AnimationCrowdSettings settings = {};
    settings._bucketKeepSeconds     = 1.0f;
    system.setCrowdSettings( settings );
    SkeletalMeshComponent* pUnit = TestAnimationCrowdInternal::createCharacter( manager, "Walker", skeleton, mesh, folder, "Wave", 0.0f );
    SW_ASSERT_NOT_NULL( pUnit );
    system.evaluate( 0.0f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( system.getCrowd().getBuckets().size() ) );

    pUnit->setShareCrowdPose( false ); // 묶음을 떠나 자기 사본으로
    SW_EXPECT_TRUE( pUnit->getCrowdMode() == AnimationCrowdMode::Own );
    SW_EXPECT_TRUE( pUnit->getMesh() != mesh );
    system.evaluate( 0.5f );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( system.getCrowd().getBuckets().size() ) ); // 아직 쉰 지 0.5 초
    system.evaluate( 0.6f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( system.getCrowd().getBuckets().size() ) );
}

/**
 * @brief [AnimationCrowdTest] VAT 쿠킹 — 목록이 고른 (메시, 클립)의 쿠킹본은 런타임 굽기와 바이트까지 같고, 쿠킹본이 있으면 런타임은 굽지 않고 읽는다
 */
SW_TEST_CASE( AnimationCrowdTest, CookedVertexAnimationMatchesRuntimeBake )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = TestAnimationCrowdInternal::makeSkeleton();
    const shared_ptr<Mesh>     mesh     = TestAnimationCrowdInternal::makeSkinnedCube();
    const string               folder   = TestAnimationCrowdInternal::writeClips( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );
    const string  meshPath     = FileUtil::joinPath( folder, "cube.mesh" );
    const string  skeletonPath = FileUtil::joinPath( folder, "cube.skeleton.json" );
    MeshAssetData meshData;
    meshData._listVertex     = mesh->getVertices();
    meshData._listSkinVertex = mesh->getSkinVertices();
    meshData._skinBoneCount  = mesh->getSkinBoneCount();
    SW_ASSERT_TRUE( MeshAssetFormat::saveToFile( meshPath, meshData ) );
    SW_ASSERT_TRUE( skeleton->saveToFile( skeletonPath ) );

    VertexAnimationCookList list;
    list._meshPath     = meshPath;
    list._skeletonPath = skeletonPath;
    list._clipFolder   = folder;
    list._listClip     = { hashed_string( "Wave" ), hashed_string( "Sway" ) };
    uint32 failedCount = 0;
    // 쿠킹 폴더를 비우면 메시 경로 곁에 쓴다 — 런타임이 같은 경로로 찾는다.
    SW_EXPECT_EQUAL( 2u, VertexAnimationCooker::cookList( list, string{}, 15.0f, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );

    const string cookedPath = MeshVertexAnimation::makeCookedPath( meshPath, hashed_string( "Wave" ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( cookedPath, "cube.wave.vat" ) );
    vector<uint8> cookedBytes;
    SW_ASSERT_TRUE( ResourceUtil::readBinaryResource( cookedPath, cookedBytes ) );
    AnimClip clip;
    SW_ASSERT_TRUE( clip.loadFromResource( FileUtil::joinPath( folder, "wave.animclip" ) ) );
    MeshVertexAnimation runtime;
    SW_ASSERT_TRUE( MeshVertexAnimationBaker::bake( *mesh, *skeleton, clip, 15.0f, false, runtime ) );
    vector<uint8> runtimeBytes;
    runtime.makeBytes( runtimeBytes );
    SW_EXPECT_TRUE( cookedBytes == runtimeBytes );
    MeshVertexAnimation reread;
    SW_ASSERT_TRUE( reread.readFromBytes( cookedBytes.data(), cookedBytes.size(), "cooked" ) );
    SW_EXPECT_EQUAL( runtime._frameCount, reread._frameCount );

    // 쿠킹본이 있으면 런타임은 읽는다(굽지 않는다).
    AnimationCrowd crowd;
    crowd.setSettings( AnimationCrowdSettings{} );
    SW_ASSERT_NOT_NULL( crowd.findVertexAnimationMesh( mesh, *skeleton, clip, false, meshPath ).get() );
    SW_EXPECT_EQUAL( 1u, crowd.getCookedVertexAnimationCount() );

    test::ScopedDefensiveTestLog expected( "a truncated or wrong-version vat is rejected" );
    MeshVertexAnimation          broken;
    SW_EXPECT_FALSE( broken.readFromBytes( cookedBytes.data(), cookedBytes.size() - 4, "truncated" ) );
    cookedBytes[4] = 9;
    SW_EXPECT_FALSE( broken.readFromBytes( cookedBytes.data(), cookedBytes.size(), "version" ) );
}
