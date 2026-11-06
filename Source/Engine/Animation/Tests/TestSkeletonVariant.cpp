#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;

// 스켈레톤 변형 — 부모와 합치기, 부모 본 번호 유지, 호환성 검사(메시 · 클립), 비율이 다른 변형에 클립을 재생하는 규칙. 디바이스 없음(nogpu).

namespace
{
    struct SkeletonVariantTestUtil
    {
        /** @brief 부모 사슬 bone0 → bone1 → bone2 입니다(본마다 Y 로 1 m). */
        static Skeleton makeBase() { return test::makeChainSkeleton( 3u ); }

        static SkeletonBoneOverride makeOverride( const utf8* pBone, float32 translationScale, RetargetTranslationMode mode )
        {
            SkeletonBoneOverride boneOverride{};
            boneOverride._bone             = hashed_string( pBone );
            boneOverride._translationScale = translationScale;
            boneOverride._translationMode  = mode;
            return boneOverride;
        }

        static SkeletonAddedBone makeAdded( const utf8* pName, const utf8* pParent )
        {
            SkeletonAddedBone added{};
            added._name                       = hashed_string( pName );
            added._parentBone                 = hashed_string( pParent );
            added._referencePose._translation = float3{ 0.0f, -0.5f, 0.0f };
            return added;
        }

        /** @brief @p skeleton 의 뼈대로 1 초짜리 원본 클립을 raw 코덱으로 압축합니다. */
        static void makeClip( const Skeleton& skeleton, AnimationClip& outClip )
        {
            const IAnimationCodec* pCodec = AnimationCodecRegistry::findCodecByName( "raw" );
            SW_REQUIRE_TRUE( pCodec != nullptr );
            SW_REQUIRE_TRUE( outClip.compressFrom( test::makeChainRawClip( skeleton, 31u, 30.0f, 0.5f ), *pCodec, AnimationCodecSettings{}, nullptr ) );
        }
    };
} // namespace

/**
 * @brief [SkeletonVariantTest] 더한 본이 뒤에 붙어 부모 번호 · 부모 인덱스가 그대로이고, 앞쪽 본 배치 지문도 그대로인지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, AddedBonesKeepBaseNumbersAndLayout )
{
    using Util          = SkeletonVariantTestUtil;
    const Skeleton base = Util::makeBase();

    SkeletonVariant variant{};
    variant._base = "base.skeleton.xml";
    variant._listAddedBone.push_back( Util::makeAdded( "hair0", "bone2" ) );
    variant._listAddedBone.push_back( Util::makeAdded( "hair1", "hair0" ) ); // 앞서 더한 본도 부모가 된다

    Skeleton merged;
    SW_REQUIRE_TRUE( merged.applyVariant( variant, base, "test" ) );
    SW_REQUIRE_EQUAL( 5u, merged.getBoneCount() );
    for ( uint32 boneIndex = 0; boneIndex < base.getBoneCount(); ++boneIndex )
    {
        SW_EXPECT_TRUE( merged.getBone( boneIndex )._name == base.getBone( boneIndex )._name );
        SW_EXPECT_EQUAL( base.getBone( boneIndex )._parentIndex, merged.getBone( boneIndex )._parentIndex );
        SW_EXPECT_FALSE( merged.isAddedBone( boneIndex ) );
    }
    SW_EXPECT_EQUAL( 2, merged.getBone( 3u )._parentIndex );
    SW_EXPECT_EQUAL( 3, merged.getBone( 4u )._parentIndex );
    SW_EXPECT_TRUE( merged.isAddedBone( 3u ) );

    // 부모의 메시가 쓰는 앞쪽 지문은 그대로이고, 전체 지문은 달라진다.
    SW_EXPECT_EQUAL( base.getHierarchyHash( 3u ), merged.getHierarchyHash( 3u ) );
    SW_EXPECT_NOT_EQUAL( base.getHierarchyHash( 3u ), merged.getHierarchyHash( 5u ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, merged.getHierarchyHash( 6u ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, merged.getBasePaths().size() );
    SW_EXPECT_TRUE( merged.getBasePaths()[0] == "base.skeleton.xml" );
}

/**
 * @brief [SkeletonVariantTest] 호환성 판정 — 부모의 메시는 변형에 걸리고, 변형의 메시는 부모에 걸리지 않으며, 배치가 다른 메시는 본 수가 같아도 지는지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, MeshFitsByBoneLayoutNotByCount )
{
    using Util           = SkeletonVariantTestUtil;
    const Skeleton  base = Util::makeBase();
    SkeletonVariant variant{};
    variant._listAddedBone.push_back( Util::makeAdded( "hair0", "bone2" ) );
    Skeleton merged;
    SW_REQUIRE_TRUE( merged.applyVariant( variant, base, "test" ) );

    MeshSkinBinding baseMesh = MeshSkinBinding::makeIdentity( 3u );
    baseMesh._layoutHash     = base.getHierarchyHash( 3u );
    SW_EXPECT_TRUE( base.canDrawSkinBinding( baseMesh ) );
    SW_EXPECT_TRUE( merged.canDrawSkinBinding( baseMesh ) );

    MeshSkinBinding variantMesh = MeshSkinBinding::makeIdentity( 4u );
    variantMesh._layoutHash     = merged.getHierarchyHash( 4u );
    SW_EXPECT_TRUE( merged.canDrawSkinBinding( variantMesh ) );
    SW_EXPECT_FALSE( base.canDrawSkinBinding( variantMesh ) );

    // 본 수는 같고 이름이 다른 스켈레톤의 메시 — 예전의 본 수 검사로는 조용히 틀리게 스키닝했다.
    Skeleton other;
    (void)other.addBone( hashed_string( "a" ), -1, BoneTransform{} );
    (void)other.addBone( hashed_string( "b" ), 0, BoneTransform{} );
    (void)other.addBone( hashed_string( "c" ), 1, BoneTransform{} );
    MeshSkinBinding otherMesh = MeshSkinBinding::makeIdentity( 3u );
    otherMesh._layoutHash     = other.getHierarchyHash( 3u );
    SW_EXPECT_FALSE( base.canDrawSkinBinding( otherMesh ) );

    // 배치를 따지지 않는 메시(강체 사본)는 본 수만 본다.
    SW_EXPECT_TRUE( base.canDrawSkinBinding( MeshSkinBinding::makeIdentity( 1u ) ) );
    SW_EXPECT_FALSE( base.canDrawSkinBinding( MeshSkinBinding{} ) );
}

/**
 * @brief [SkeletonVariantTest] 잘못된 차이(모르는 본 고치기, 있는 이름 더하기, 모르는 부모, 모르는 소켓 본, 골반 없는 ScaleByPelvisHeight)는 오류이고 내용을 비우는지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, BadDifferencesAreRejected )
{
    using Util          = SkeletonVariantTestUtil;
    const Skeleton base = Util::makeBase();
    Skeleton       merged;

    SkeletonVariant unknownBone{};
    unknownBone._listBoneOverride.push_back( Util::makeOverride( "nope", 2.0f, RetargetTranslationMode::None ) );
    SW_EXPECT_LOG( "Skeleton.VariantOverrideUnknownBone" );
    SW_EXPECT_FALSE( merged.applyVariant( unknownBone, base, "test" ) );
    SW_EXPECT_EQUAL( 0u, merged.getBoneCount() );

    SkeletonVariant duplicate{};
    duplicate._listAddedBone.push_back( Util::makeAdded( "bone1", "bone0" ) );
    SkeletonVariant unknownParent{};
    unknownParent._listAddedBone.push_back( Util::makeAdded( "hair0", "nope" ) );
    SW_EXPECT_LOG( "Skeleton.VariantAddedBoneInvalid" );
    SW_EXPECT_FALSE( merged.applyVariant( duplicate, base, "test" ) );
    SW_EXPECT_FALSE( merged.applyVariant( unknownParent, base, "test" ) );

    SkeletonVariant noPelvis{};
    noPelvis._listBoneOverride.push_back( Util::makeOverride( "bone1", 2.0f, RetargetTranslationMode::ScaleByPelvisHeight ) );
    SW_EXPECT_LOG( "Skeleton.VariantPelvisInvalid" );
    SW_EXPECT_FALSE( merged.applyVariant( noPelvis, base, "test" ) );

    SkeletonVariant badSocket{};
    SkeletonSocket  socket{};
    socket._name       = hashed_string( "Hand" );
    socket._parentBone = hashed_string( "nope" );
    badSocket._listSocket.push_back( socket );
    SW_EXPECT_LOG( "Skeleton.SocketNamesUnknownBone" );
    SW_EXPECT_FALSE( merged.applyVariant( badSocket, base, "test" ) );
}

/**
 * @brief [SkeletonVariantTest] 비율을 고치면 기준 포즈가 바뀌고, 이동 트랙은 RetargetTranslationMode 대로, 회전 · 크기 트랙은 차이를 곱해 재생되는지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, ClipRulesFollowTranslationMode )
{
    using Util          = SkeletonVariantTestUtil;
    const Skeleton base = Util::makeBase();

    SkeletonVariant variant{};
    variant._pelvisBone = hashed_string( "bone1" );
    variant._listBoneOverride.push_back( Util::makeOverride( "bone1", 2.0f, RetargetTranslationMode::ScaleByPelvisHeight ) ); // 골반 높이 1 → 2
    variant._listBoneOverride.push_back( Util::makeOverride( "bone2", 3.0f, RetargetTranslationMode::None ) );
    SkeletonBoneOverride scaled = Util::makeOverride( "bone0", 1.0f, RetargetTranslationMode::Copy );
    scaled._scale               = float3{ 2.0f, 2.0f, 2.0f };
    variant._listBoneOverride.push_back( scaled );
    Skeleton merged;
    SW_REQUIRE_TRUE( merged.applyVariant( variant, base, "test" ) );
    SW_EXPECT_TRUE( merged.hasClipRules() );
    SW_EXPECT_FALSE( base.hasClipRules() );
    SW_EXPECT_NEAR( 2.0f, merged.getBone( 1u )._referencePose._translation._y, 1e-6f );
    SW_EXPECT_NEAR( 3.0f, merged.getBone( 2u )._referencePose._translation._y, 1e-6f );

    BoneTransform sampled{};
    sampled._translation = float3{ 0.0f, 1.5f, 0.0f }; // 부모 기준(1 m)에서 0.5 m 움직인 트랙

    // None — 뼈 길이를 지킨다(기본).
    SW_EXPECT_NEAR( 3.0f, merged.applyClipRule( 2u, sampled )._translation._y, 1e-6f );
    // ScaleByPelvisHeight — 변형 기준(2 m) + 움직인 양(0.5 m) * 골반 높이 비(2).
    SW_EXPECT_NEAR( 3.0f, merged.applyClipRule( 1u, sampled )._translation._y, 1e-6f );
    // Copy — 트랙 값 그대로이고, 크기 차이는 곱한다.
    const BoneTransform copied = merged.applyClipRule( 0u, sampled );
    SW_EXPECT_NEAR( 1.5f, copied._translation._y, 1e-6f );
    SW_EXPECT_NEAR( 2.0f, copied._scale._x, 1e-6f );
}

/**
 * @brief [SkeletonVariantTest] 부모의 클립이 변형에 오류 없이 재생되고(더한 본은 기준 포즈), 메시의 역 바인드로 구한 팔레트가 비율 차이를 그대로 담는지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, BaseClipPlaysOnVariant )
{
    using Util          = SkeletonVariantTestUtil;
    const Skeleton base = Util::makeBase();
    AnimationClip  clip;
    Util::makeClip( base, clip );

    SkeletonVariant variant{};
    variant._listBoneOverride.push_back( Util::makeOverride( "bone1", 2.0f, RetargetTranslationMode::None ) );
    variant._listAddedBone.push_back( Util::makeAdded( "hair0", "bone2" ) );
    Skeleton merged;
    SW_REQUIRE_TRUE( merged.applyVariant( variant, base, "test" ) );

    vector<int32> listTrackToBone;
    SW_REQUIRE_TRUE( clip.makeTrackToBoneMap( merged, listTrackToBone ) );
    Pose pose;
    Pose scratchTrackPose;
    pose.setToReference( merged );
    SW_REQUIRE_TRUE( clip.samplePose( merged, 0.5f, listTrackToBone, pose, scratchTrackPose, false ) );
    SW_EXPECT_NEAR( 2.0f, pose.getBoneTransform( 1u )._translation._y, 1e-5f );  // 트랙이 부모 길이(1 m)를 줘도 변형의 뼈 길이다
    SW_EXPECT_NEAR( -0.5f, pose.getBoneTransform( 3u )._translation._y, 1e-5f ); // 더한 본은 클립에 없어 기준 포즈 그대로다

    // 부모 비율로 저작한 메시 한 벌을 변형에 그린다 — 레퍼런스 포즈의 팔레트가 비율 차이(본 2 가 1 m 더 높다)를 낸다.
    vector<float4x4> listInverseBind;
    base.computeReferenceInverseBinds( listInverseBind );
    pose.setToReference( merged );
    vector<float4x4> listModel;
    pose.computeModelSpace( merged.getParentIndices(), listModel );
    vector<float4x4> listPalette;
    Pose::computeSkinPalette( listInverseBind, listModel, listPalette );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listPalette.size() ); // 메시가 쓰는 본만 — 더한 본은 팔레트에 들지 않는다
    SW_EXPECT_NEAR( 0.0f, listPalette[0].getTranslation()._y, 1e-5f );
    SW_EXPECT_NEAR( 1.0f, listPalette[2].getTranslation()._y, 1e-5f );
}

/**
 * @brief [SkeletonVariantTest] 스켈레톤에 없는 본의 트랙은 오류로 알리고 있는 본은 잇는지, 부품으로 선언한 스켈레톤은 오류가 아닌지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, ClipTracksMissingFromSkeletonAreReported )
{
    using Util = SkeletonVariantTestUtil;
    AnimationClip clip;
    Util::makeClip( Util::makeBase(), clip );

    Skeleton      shorter = test::makeChainSkeleton( 2u );
    vector<int32> listTrackToBone;
    SW_EXPECT_LOG( "AnimationClip.TracksNotInSkeleton" );
    SW_EXPECT_FALSE( clip.makeTrackToBoneMap( shorter, listTrackToBone ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listTrackToBone.size() );
    SW_EXPECT_EQUAL( 1, listTrackToBone[1] );
    SW_EXPECT_EQUAL( -1, listTrackToBone[2] );

    shorter.setPartial( true );
    SW_EXPECT_TRUE( clip.makeTrackToBoneMap( shorter, listTrackToBone ) );
    SW_EXPECT_EQUAL( -1, listTrackToBone[2] );
}

/**
 * @brief [SkeletonVariantTest] 변형 파일의 루트 이름으로 읽기가 갈리고, 평평한 스켈레톤은 소켓 · 부품 선언과 함께 왕복하며 역 바인드를 적지 않는지 검증
 */
SW_TEST_CASE( SkeletonVariantTest, FilesRoundTrip )
{
    using Util = SkeletonVariantTestUtil;
    SkeletonVariant variant{};
    variant._base = "base.skeleton.xml";
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( variant.toXML(), "test" ) );
    SW_EXPECT_TRUE( static_cast<bool>( document.getRoot( "SkeletonVariant" ) ) );

    SkeletonVariant withSocket{};
    SkeletonSocket  socket{};
    socket._name       = hashed_string( "Hand" );
    socket._parentBone = hashed_string( "bone2" );
    withSocket._listSocket.push_back( socket );
    withSocket._bPartial = SW_TRUE;
    Skeleton merged;
    SW_REQUIRE_TRUE( merged.applyVariant( withSocket, Util::makeBase(), "test" ) );

    const string xml = merged.toXML();
    SW_EXPECT_TRUE( xml.find( "_inverseBind" ) == string::npos );
    Skeleton read;
    SW_REQUIRE_TRUE( read.parseXML( xml, "test" ) );
    SW_EXPECT_EQUAL( 3u, read.getBoneCount() );
    SW_EXPECT_TRUE( read.isPartial() );
    SW_EXPECT_TRUE( read.findSocket( hashed_string( "Hand" ) ) != nullptr );
    SW_EXPECT_EQUAL( merged.getHierarchyHash( 3u ), read.getHierarchyHash( 3u ) );

    // 소켓의 본이 없는 파일은 읽기 오류다.
    Skeleton     bad;
    const string badXML = "<Skeleton><_listBone><SkeletonBone _name=\"a\" _parentIndex=\"-1\"><_referencePose _translation=\"0,0,0\" _rotation=\"0,0,0,1\" _scale=\"1,1,1\" /></SkeletonBone></_listBone><_listAttachment />"
                          "<_listSocket><SkeletonSocket _name=\"s\" _parentBone=\"nope\"><_localTransform _translation=\"0,0,0\" _rotation=\"0,0,0,1\" _scale=\"1,1,1\" /></SkeletonSocket></_listSocket></Skeleton>";
    SW_EXPECT_LOG( "Skeleton.SocketNamesUnknownBone" );
    SW_EXPECT_FALSE( bad.parseXML( badXML, "test" ) );
}
