#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationGraphLayersTest — 그래프 데이터의 본 마스크(roots 퍼뜨림 · bones 예외) · 레이어(마스크 · 가중치 · 합치기 · 가중치 파라미터)와,
// 스켈레탈 애니메이터가 그래프의 블렌드 스페이스 상태와 레이어를 데이터만으로 재생하는 연결.

namespace
{
    struct TestAnimationGraphLayersInternal
    {
        /**
         * @brief 모든 본의 이동 x 가 @p offsetX + @p slopePerSecond × 시각인 클립을 `<폴더>/<소문자 이름>.animclip` 으로 씁니다. 길이는 (@p sampleCount - 1) / 30 초입니다.
         */
        [[nodiscard]] static bool saveOffsetClip( const Skeleton& skeleton, const string& folder, const utf8* pName, float32 offsetX, uint32 sampleCount,
                                                  float32 slopePerSecond = 0.0f )
        {
            AnimationRawClip raw;
            raw._sampleRate  = 30.0f;
            raw._sampleCount = sampleCount;
            for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
            {
                raw._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
                raw._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
            }
            for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
            {
                for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
                {
                    BoneTransform transform   = skeleton.getBone( boneIndex )._referencePose;
                    transform._translation._x = offsetX + slopePerSecond * static_cast<float32>( sampleIndex ) / raw._sampleRate;
                    raw._listSample.push_back( transform );
                }
            }
            AnimationClip clip;
            clip.setName( hashed_string( pName ) );
            if ( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) == false )
                return false;
            return clip.saveToFile( FileUtil::joinPath( folder, StringUtil::toLower( pName ) + ".animclip" ) );
        }

        static AnimationBoneMaskEntry makeEntry( const utf8* pBone, float32 weight, bool bPropagate )
        {
            AnimationBoneMaskEntry entry{};
            entry._bone       = hashed_string( pBone );
            entry._weight     = weight;
            entry._bPropagate = bPropagate ? SW_TRUE : SW_FALSE;
            return entry;
        }

        static float32 getBoneX( const SkeletalMeshComponent& unit, uint32 boneIndex ) { return unit.getLocalPose().getBoneTransform( boneIndex )._translation._x; }
    };
} // namespace

/**
 * @brief [AnimationGraphLayersTest] 본 마스크 — root 는 자손에 퍼지고 더 깊은 root 가 그 아래를 이기며, bones 는 그 본 하나만 바꾼다. 없는 본은 오류다
 */
SW_TEST_CASE( AnimationGraphLayersTest, BoneMaskPropagatesRootsAndOverridesSingleBones )
{
    const Skeleton       skeleton = test::makeChainSkeleton( 5 );
    AnimationBoneMaskDef mask{};
    mask._name = hashed_string( "Upper" );
    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "bone1", 1.0f, true ) );
    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "bone3", 0.25f, true ) );
    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "bone2", 0.5f, false ) );

    vector<float32> listWeight;
    SW_ASSERT_TRUE( mask.computeBoneWeights( skeleton, listWeight ) );
    SW_ASSERT_EQUAL( 5u, static_cast<uint32>( listWeight.size() ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listWeight[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listWeight[1], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listWeight[2], 1e-6f );  // 예외는 그 본 하나다
    SW_EXPECT_NEAR_EQUAL( 0.25f, listWeight[3], 1e-6f ); // 더 깊은 root 가 이긴다
    SW_EXPECT_NEAR_EQUAL( 0.25f, listWeight[4], 1e-6f );

    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "missing", 1.0f, true ) );
    SW_EXPECT_FALSE( mask.computeBoneWeights( skeleton, listWeight ) );
    for ( const float32 weight : listWeight )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, weight, 1e-6f );
    }
}

/**
 * @brief [AnimationGraphLayersTest] 그래프 JSON — 본 마스크와 레이어를 읽고, 쓰고 다시 읽어도 같다
 */
SW_TEST_CASE( AnimationGraphLayersTest, LayersAndBoneMasksRoundTripThroughJSON )
{
    const string_view   json = R"({ "nodes": [ { "id": 1, "name": "Idle" } ],
        "bone_masks": [ { "name": "Upper", "roots": [ { "bone": "spine", "weight": 1 } ], "bones": [ { "bone": "spine", "weight": 0.5 } ] } ],
        "layers": [ { "name": "Aim", "clip": "AimClip", "mask": "Upper", "weight": 0.75, "blend": "Additive", "loop": false, "weight_param": "AimWeight", "half_life": 0.15 },
                    { "name": "Breath", "clip": "BreathClip", "weight": 1, "blend": "Override" } ] })";
    AnimationGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseJSON( json ) );
    AnimationGraphAsset again;
    SW_ASSERT_TRUE( again.parseJSON( parsed.toJSON() ) );
    for ( const AnimationGraphAsset* pGraph : { &parsed, &again } )
    {
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pGraph->_listBoneMask.size() ) );
        SW_ASSERT_EQUAL( 2u, static_cast<uint32>( pGraph->_listLayer.size() ) );
        const AnimationBoneMaskDef* pMask = pGraph->findBoneMask( "Upper" );
        SW_ASSERT_NOT_NULL( pMask );
        SW_ASSERT_EQUAL( 2u, static_cast<uint32>( pMask->_listEntry.size() ) );
        SW_EXPECT_TRUE( pMask->_listEntry[0]._bPropagate == SW_TRUE );
        SW_EXPECT_TRUE( pMask->_listEntry[1]._bPropagate == SW_FALSE );
        SW_EXPECT_NEAR_EQUAL( 0.5f, pMask->_listEntry[1]._weight, 1e-6f );
        const AnimationGraphLayerDef* pAim = pGraph->findLayer( "Aim" );
        SW_ASSERT_NOT_NULL( pAim );
        SW_EXPECT_TRUE( pAim->_clipName == hashed_string( "AimClip" ) );
        SW_EXPECT_TRUE( pAim->_maskName == hashed_string( "Upper" ) );
        SW_EXPECT_TRUE( pAim->_weightParameter == hashed_string( "AimWeight" ) );
        SW_EXPECT_TRUE( pAim->_blend == AnimationLayerBlend::Additive );
        SW_EXPECT_TRUE( pAim->_bLoop == SW_FALSE );
        SW_EXPECT_NEAR_EQUAL( 0.75f, pAim->_weight, 1e-6f );
        SW_EXPECT_NEAR_EQUAL( 0.15f, pAim->_halfLifeSeconds, 1e-6f );
        const AnimationGraphLayerDef* pBreath = pGraph->findLayer( "Breath" );
        SW_ASSERT_NOT_NULL( pBreath );
        SW_EXPECT_TRUE( pBreath->_maskName.empty() );
        SW_EXPECT_TRUE( pBreath->_blend == AnimationLayerBlend::Override );
    }
}

/**
 * @brief [AnimationGraphLayersTest] Shooter3D 플레이어 그래프는 상체 레이어 둘(Aim · Shoot)을 데이터로 갖는다 — 프리셋 마스크 파일을 읽고, 게임 코드는 가중치 파라미터만 쓴다
 * @details 예전에는 `ShooterAvatarComponent` 가 코드로 레이어를 더했다(`addLayer`, 마스크 루트 본 프로퍼티). 이름이 게임의 `ShooterAnimationParameter`
 *          (`AimWeight` · `ShootWeight`)와 어긋나면 레이어가 0 가중치로 남아 상체가 조용히 사라진다 — 그 계약을 여기서 지킨다.
 */
SW_TEST_CASE( AnimationGraphLayersTest, Shooter3DPlayerGraphCarriesTheUpperBodyLayers )
{
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.loadFromFile( "game/shooter3d/data/anim/adventurer.animgraph.json" ) );
    const AnimationBoneMaskDef* pMask = graph.findBoneMask( "UpperBody" );
    SW_ASSERT_NOT_NULL( pMask );
    SW_EXPECT_FALSE( pMask->_listEntry.empty() );
    const AnimationGraphLayerDef* pAim   = graph.findLayer( "Aim" );
    const AnimationGraphLayerDef* pShoot = graph.findLayer( "Shoot" );
    SW_ASSERT_TRUE( pAim != nullptr && pShoot != nullptr );
    SW_EXPECT_TRUE( pAim->_weightParameter == hashed_string( "AimWeight" ) );
    SW_EXPECT_TRUE( pShoot->_weightParameter == hashed_string( "ShootWeight" ) );
    SW_EXPECT_TRUE( pAim->_maskName == hashed_string( "UpperBody" ) && pShoot->_maskName == hashed_string( "UpperBody" ) );
    SW_EXPECT_TRUE( pAim->_bLoop == SW_FALSE ); // 겨누기 클립은 겨눈 자세로 끝난다 — 돌리면 끝에서 처음으로 팔이 튄다
}

/**
 * @brief [AnimationGraphLayersTest] 잘못된 레이어 데이터는 조용히 기본값이 되지 않고 그래프 전체가 로드 오류다
 * @details 없는 마스크 이름, 모르는 합치기, 가중치 없음 · 범위 밖, 프리셋 파일과 본문을 함께 적은 마스크, 겹친 이름, 빈 마스크.
 */
SW_TEST_CASE( AnimationGraphLayersTest, InvalidLayerDataFailsTheGraphLoad )
{
    const string_view arrJSON[] = {
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "layers": [ { "name": "A", "clip": "C", "mask": "Nope", "weight": 1, "blend": "Override" } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "layers": [ { "name": "A", "clip": "C", "weight": 1, "blend": "Multiply" } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "layers": [ { "name": "A", "clip": "C", "blend": "Override" } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "layers": [ { "name": "A", "clip": "C", "weight": 1.5, "blend": "Override" } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "layers": [ { "name": "A", "clip": "C", "weight": 1, "blend": "Override", "half_life": 0 } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "bone_masks": [ { "name": "M", "file": "x.bonemask.json", "roots": [ { "bone": "b", "weight": 1 } ] } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "bone_masks": [ { "name": "M", "roots": [ { "bone": "b", "weight": 1 } ] }, { "name": "M", "roots": [ { "bone": "b", "weight": 1 } ] } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "bone_masks": [ { "name": "M" } ] })",
        R"({ "nodes": [ { "id": 1, "name": "Idle" } ], "bone_masks": [ { "name": "M", "roots": [ { "bone": "b" } ] } ] })",
    };
    for ( const string_view json : arrJSON )
    {
        AnimationGraphAsset graph;
        SW_EXPECT_FALSE( graph.parseJSON( json ) );
        SW_EXPECT_TRUE( graph._listNode.empty() );
        SW_EXPECT_TRUE( graph._listLayer.empty() );
        SW_EXPECT_TRUE( graph._listBoneMask.empty() );
    }
}

/**
 * @brief [AnimationGraphLayersTest] 애니메이터가 그래프의 블렌드 스페이스 상태를 재생한다 — 축 파라미터로 표본 포즈를 섞고, 한 바퀴 길이는 표본 길이의 가중 평균이다
 * @details 표본 Slow(x 0, 1 초)와 Fast(x 2, 0.5 초). Speed 0.5 면 본 x 1 · 길이 0.75 초, Speed 1 이면 x 2 · 길이 0.5 초다.
 */
SW_TEST_CASE( AnimationGraphLayersTest, AnimatorPlaysBlendSpaceStateFromGraph )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    const string   folder   = test::makeTempDirectory( "graph_blendspace" );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Slow", 0.0f, 31 ) );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Fast", 2.0f, 16 ) );

    AnimationGraphAsset graph;
    AnimationGraphNode  node{};
    node._id                     = 1;
    node._name                   = "Move";
    node._blendSpace._parameterX = hashed_string( "Speed" );
    node._blendSpace._space.setAxisCount( 1 );
    node._blendSpace._space.addSample( float2{ 0.0f, 0.0f }, hashed_string( "Slow" ) );
    node._blendSpace._space.addSample( float2{ 1.0f, 0.0f }, hashed_string( "Fast" ) );
    graph._listNode.push_back( node );
    const string graphPath = FileUtil::joinPath( folder, "move.animgraph.json" );
    SW_ASSERT_TRUE( graph.saveToFile( graphPath ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Runner" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->getParameters().setFloat( hashed_string( "Speed" ), 0.5f );
    pAnimator->setAnimationGraphPath( graphPath );
    pAnimator->setInitialState( "Move" );
    pAnimator->dispatchBeginPlay();
    SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == hashed_string( "Move" ) );
    const IAnimationPlayable* pPlaying = pAnimator->getGraphPlayer().getPlayer().getCurrentPlayable();
    SW_ASSERT_NOT_NULL( pPlaying );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pPlaying->getPlayLength(), 1e-4f );

    manager.getAnimationSystem().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 1e-3f );

    pAnimator->getParameters().setFloat( hashed_string( "Speed" ), 1.0f );
    manager.getAnimationSystem().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pPlaying->getPlayLength(), 1e-4f );
    SW_EXPECT_TRUE( pAnimator->getGraphPlayer().getPlayer().getCurrentTime() <= 0.5f );
}

/**
 * @brief [AnimationGraphLayersTest] 그래프 레이어가 데이터 마스크로 상체만 덮는다 — 코드 없이 상하체 분리, 가중치 파라미터로 켜고 끈다
 * @details 기본 상태 Base(x 0) 위에 레이어 Upper(x 2), 마스크 roots bone1 1 · bones bone1 0.5. bone0 0 · bone1 1 · bone2 2 다.
 *          가중치는 파라미터 UpperWeight 가 정하므로 0 이면 레이어가 꺼지고, 코드로 가중치를 바꾸려 하면 거절한다(값의 출처는 하나).
 */
SW_TEST_CASE( AnimationGraphLayersTest, GraphLayerMasksUpperBodyFromData )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 3 );
    const string   folder   = test::makeTempDirectory( "graph_layers" );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Base", 0.0f, 31 ) );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Upper", 2.0f, 31 ) );

    AnimationGraphAsset graph;
    AnimationGraphNode  node{};
    node._id   = 1;
    node._name = "Base";
    graph._listNode.push_back( node );
    AnimationBoneMaskDef mask{};
    mask._name = hashed_string( "UpperBody" );
    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "bone1", 1.0f, true ) );
    mask._listEntry.push_back( TestAnimationGraphLayersInternal::makeEntry( "bone1", 0.5f, false ) );
    graph._listBoneMask.push_back( mask );
    AnimationGraphLayerDef layer{};
    layer._name            = hashed_string( "Upper" );
    layer._clipName        = hashed_string( "Upper" );
    layer._maskName        = hashed_string( "UpperBody" );
    layer._weightParameter = hashed_string( "UpperWeight" );
    layer._weight          = 1.0f;
    layer._blend           = AnimationLayerBlend::Override;
    graph._listLayer.push_back( layer );
    const string graphPath = FileUtil::joinPath( folder, "layers.animgraph.json" );
    SW_ASSERT_TRUE( graph.saveToFile( graphPath ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Gunner" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->setAnimationGraphPath( graphPath );
    pAnimator->setInitialState( "Base" );
    pAnimator->getParameters().setFloat( hashed_string( "UpperWeight" ), 1.0f );
    pAnimator->dispatchBeginPlay();
    SW_ASSERT_EQUAL( 1u, pAnimator->getGraphLayerCount() );

    manager.getAnimationSystem().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 2 ), 1e-3f );

    pAnimator->getParameters().setFloat( hashed_string( "UpperWeight" ), 0.0f );
    manager.getAnimationSystem().evaluate( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 2 ), 1e-3f );
    SW_EXPECT_FALSE( pAnimator->setGraphLayerWeight( hashed_string( "Upper" ), 0.5f ) );
    SW_EXPECT_FALSE( pAnimator->setGraphLayerWeight( hashed_string( "Missing" ), 0.5f ) );
}

/**
 * @brief [AnimationGraphLayersTest] 반감기가 있는 레이어를 다시 시작하면 끊긴 레이어 포즈에서 이어 간다(inertialization) — 처음 포즈로 튀지 않고, 반감기 몇 배 뒤엔 클립 값이다
 * @details 레이어 클립 Ramp 는 본 x 가 시각과 같다(1 초 반복). 0.5 초에 다시 시작하면 바로 다음 프레임도 x ≈ 0.5 이고, 0.6 초 뒤에는 클립 값(≈ 0.6)이다.
 */
SW_TEST_CASE( AnimationGraphLayersTest, RestartedLayerInertializesFromInterruptedPose )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    const string   folder   = test::makeTempDirectory( "graph_layer_restart" );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Base", 0.0f, 31 ) );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Ramp", 0.0f, 31, 1.0f ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Flincher" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    pAnimator->setInitialState( "Base" );
    pAnimator->dispatchBeginPlay();
    AnimationLayerDesc desc{};
    desc._clipName         = hashed_string( "Ramp" );
    desc._halfLifeSeconds  = 0.1f;
    const int32 layerIndex = pAnimator->addLayer( desc );
    SW_ASSERT_TRUE( layerIndex >= 0 );

    for ( uint32 frameIndex = 0; frameIndex < 10; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( 0.05f );
    }
    const float32 beforeRestart = TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, beforeRestart, 0.02f );

    pAnimator->restartLayer( static_cast<uint32>( layerIndex ) );
    manager.getAnimationSystem().evaluate( 0.001f );
    SW_EXPECT_NEAR_EQUAL( beforeRestart, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 0.05f ); // 0 으로 튀지 않았다

    for ( uint32 frameIndex = 0; frameIndex < 12; ++frameIndex )
    {
        manager.getAnimationSystem().evaluate( 0.05f );
    }
    SW_EXPECT_NEAR_EQUAL( 0.601f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 0.03f );
}

/**
 * @brief [AnimationGraphLayersTest] 화면 밖에서 포즈를 건너뛰는 동안 일어난 inertialization 전이는 다시 보일 때 옛 포즈에서 출발하지 않는다 — 새 포즈다
 * @details Idle(x 0) → Walk(x 2) 링크가 inertialization 이다. 보이는 동안 전이하면 첫 프레임은 끊긴 포즈(x 0)에서 이어 간다.
 *          숨긴 채 전이하고 몇 프레임 뒤 다시 보이면, 지난 기록(x 0)은 아무도 보지 않은 옛 포즈라 출발점이 아니다 — 바로 x 2 다(예전에는 x 0 으로 튀었다).
 */
SW_TEST_CASE( AnimationGraphLayersTest, InertializationSkipsPoseNobodySaw )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    const string   folder   = test::makeTempDirectory( "graph_inertialization_hidden" );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Idle", 0.0f, 31 ) );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Walk", 2.0f, 31 ) );
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseJSON( R"({ "nodes": [ { "id": 1, "name": "Idle" }, { "id": 2, "name": "Walk" } ],
        "links": [ { "id": 1, "from": 1, "to": 2, "blend_mode": "inertialization", "half_life": 0.2, "condition": { "param": "Speed", "op": ">", "value": 0.5 } } ] })" ) );
    const string graphPath = FileUtil::joinPath( folder, "walk.animgraph.json" );
    SW_ASSERT_TRUE( graph.saveToFile( graphPath ) );

    for ( const bool bHiddenDuringTransition : { false, true } )
    {
        GameObjectManager manager;
        GameObject*       pObject = manager.createGameObject( hashed_string( "Walker" ) );
        SW_ASSERT_NOT_NULL( pObject );
        SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
        SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
        SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
        pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
        pAnimator->setClipFolder( folder );
        pAnimator->setAnimationGraphPath( graphPath );
        pAnimator->setInitialState( "Idle" );
        pAnimator->dispatchBeginPlay();
        for ( uint32 frameIndex = 0; frameIndex < 4; ++frameIndex )
        {
            manager.getAnimationSystem().evaluate( 0.05f );
        }
        SW_EXPECT_NEAR_EQUAL( 0.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 1e-3f );

        pAnimator->getParameters().setFloat( hashed_string( "Speed" ), 1.0f );
        if ( bHiddenDuringTransition )
        {
            pUnit->setVisibleHint( false );
            for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
            {
                manager.getAnimationSystem().evaluate( 0.05f );
            }
            SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == hashed_string( "Walk" ) );
            pUnit->setVisibleHint( true );
            manager.getAnimationSystem().evaluate( 0.05f );
            SW_EXPECT_NEAR_EQUAL( 2.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 1e-3f ); // 옛 포즈(x 0)로 튀지 않았다
        }
        else
        {
            manager.getAnimationSystem().evaluate( 0.05f );
            SW_EXPECT_TRUE( pAnimator->getCurrentStateName() == hashed_string( "Walk" ) );
            SW_EXPECT_NEAR_EQUAL( 0.0f, TestAnimationGraphLayersInternal::getBoneX( *pUnit, 1 ), 1e-3f ); // 보인 포즈에서 이어 간다
        }
    }
}

/**
 * @brief [AnimationGraphLayersTest] 이 애니메이터가 재생할 수 없는 것을 적은 그래프는 쓰지 않는다 — 클립 없는 레이어 · 포즈 노드만 빼고 돌지 않는다
 * @details 빠진 것만 빼고 돌면 데이터가 말한 것과 다른 포즈가 나온다. 파일을 못 읽은 그래프와 같이 플레이어에 그래프를 걸지 않는다.
 */
SW_TEST_CASE( AnimationGraphLayersTest, UnplayableGraphIsNotUsed )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    const Skeleton skeleton = test::makeChainSkeleton( 2 );
    const string   folder   = test::makeTempDirectory( "graph_unplayable" );
    SW_ASSERT_TRUE( TestAnimationGraphLayersInternal::saveOffsetClip( skeleton, folder, "Base", 0.0f, 31 ) );

    AnimationGraphAsset layerGraph;
    AnimationGraphNode  node{};
    node._id   = 1;
    node._name = "Base";
    layerGraph._listNode.push_back( node );
    AnimationGraphLayerDef layer{};
    layer._name     = hashed_string( "Missing" );
    layer._clipName = hashed_string( "NoSuchClip" );
    layer._weight   = 1.0f;
    layer._blend    = AnimationLayerBlend::Override;
    layerGraph._listLayer.push_back( layer );
    const string layerGraphPath = FileUtil::joinPath( folder, "missing_layer.animgraph.json" );
    SW_ASSERT_TRUE( layerGraph.saveToFile( layerGraphPath ) );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Broken" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
    SW_ASSERT_TRUE( pUnit != nullptr && pAnimator != nullptr );
    pUnit->setSkeleton( make_shared<Skeleton>( skeleton ) );
    pAnimator->setClipFolder( folder );
    {
        test::ScopedDefensiveTestLog expected( "a graph layer without its clip fails the graph" );
        pAnimator->setAnimationGraphPath( layerGraphPath );
    }
    SW_EXPECT_EQUAL( 0u, pAnimator->getGraphLayerCount() );
    SW_EXPECT_TRUE( pAnimator->getGraphPlayer().getGraph() == nullptr );
}
