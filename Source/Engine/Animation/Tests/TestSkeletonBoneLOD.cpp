#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

using namespace sw;

// 본 LOD — 메시 LOD 에서 지은 마스크(가중치 없는 본 빼기), 핵심 본과 부모 사슬 보존, 손 파일의 핵심 본 제거 오류, 부모 파일 상속. 디바이스 없음(nogpu).

namespace
{
    class SkeletonBoneLODTestSource final : public IDataInheritanceSource
    {
    public:
        void add( const utf8* pID, const utf8* pText ) { _mapText[pID] = pText; }

        [[nodiscard]] bool readText( string_view assetID, string& outText ) override
        {
            const auto it = _mapText.find( string{ assetID } );
            if ( it == _mapText.end() )
                return false;
            outText = it->second;
            return true;
        }

    private:
        unordered_map<string, string> _mapText;
    };

    struct SkeletonBoneLODTestUtil
    {
        enum Bone : uint32
        {
            kRoot = 0,
            kSpine,
            kHead,
            kHand,
            kFinger,
            kKneeIK,
            kBoneCount
        };

        /** @brief root → spine → (head, hand → finger), root → kneeIK(가중치 없는 IK 대상) 입니다. */
        static Skeleton makeSkeleton()
        {
            Skeleton skeleton;
            (void)skeleton.addBone( hashed_string( "root" ), -1, BoneTransform{} );
            (void)skeleton.addBone( hashed_string( "spine" ), static_cast<int32>( kRoot ), BoneTransform{} );
            (void)skeleton.addBone( hashed_string( "head" ), static_cast<int32>( kSpine ), BoneTransform{} );
            (void)skeleton.addBone( hashed_string( "hand" ), static_cast<int32>( kSpine ), BoneTransform{} );
            (void)skeleton.addBone( hashed_string( "finger" ), static_cast<int32>( kHand ), BoneTransform{} );
            (void)skeleton.addBone( hashed_string( "kneeIK" ), static_cast<int32>( kRoot ), BoneTransform{} );
            return skeleton;
        }

        static MeshSkinVertex makeSkin( uint16 joint )
        {
            MeshSkinVertex skin{};
            skin._arrJoint[0]  = joint;
            skin._arrWeight[0] = 1.0f;
            return skin;
        }

        /** @brief LOD 0 은 spine · head · hand · finger 를, LOD 1 은 손가락 없이 spine · head · hand 만 가중치로 쓰는 스킨드 메시입니다. */
        static shared_ptr<Mesh> makeMesh()
        {
            vector<RHIVertex> listVertex( 6u );
            for ( size_t index = 0; index < listVertex.size(); ++index )
            {
                listVertex[index]._arrPosition[0] = static_cast<float>( index % 3u );
                listVertex[index]._arrPosition[1] = static_cast<float>( index / 3u );
                listVertex[index]._arrNormal[2]   = 1.0f;
            }
            vector<uint32>   listIndex{ 0, 1, 2, 3, 4, 5 };
            shared_ptr<Mesh> mesh = Mesh::create();
            SW_EXPECT_TRUE( mesh->setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) ) );
            mesh->setSkin( { makeSkin( kSpine ), makeSkin( kHead ), makeSkin( kHand ), makeSkin( kFinger ), makeSkin( kFinger ), makeSkin( kFinger ) }, MeshSkinBinding::makeIdentity( kBoneCount ) );
            MeshLODLevel level{};
            level._listVertex.resize( 3u );
            level._listIndex      = { 0, 1, 2 };
            level._screenSize     = 0.3f;
            level._listSkinVertex = { makeSkin( kSpine ), makeSkin( kHead ), makeSkin( kHand ) };
            SW_EXPECT_TRUE( mesh->setLODs( { level } ) );
            return mesh;
        }
    };
} // namespace

/**
 * @brief [SkeletonBoneLODTest] 메시 LOD 1 이 가중치로 쓰지 않는 본(손가락 · IK 대상)이 빠지고, 쓰는 본과 그 부모 사슬(root)은 남는지 검증
 */
SW_TEST_CASE( SkeletonBoneLODTest, MeshMasksDropBonesTheLevelDoesNotWeight )
{
    using Util                      = SkeletonBoneLODTestUtil;
    const Skeleton         skeleton = Util::makeSkeleton();
    const shared_ptr<Mesh> mesh     = Util::makeMesh();
    vector<uint8>          requiredMask;
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( skeleton, nullptr, {}, requiredMask, "test" ) );
    vector<vector<uint8>> listMask;
    SkeletonBoneLOD::computeMeshMasks( skeleton, *mesh, requiredMask, listMask );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listMask.size() ) );
    const vector<uint8>& mask = listMask[0];
    SW_EXPECT_TRUE( mask[Util::kRoot] == SW_TRUE ); // 쓰는 본의 부모 사슬
    SW_EXPECT_TRUE( mask[Util::kSpine] == SW_TRUE );
    SW_EXPECT_TRUE( mask[Util::kHead] == SW_TRUE );
    SW_EXPECT_TRUE( mask[Util::kHand] == SW_TRUE );
    SW_EXPECT_TRUE( mask[Util::kFinger] == SW_FALSE );
    SW_EXPECT_TRUE( mask[Util::kKneeIK] == SW_FALSE );
}

/**
 * @brief [SkeletonBoneLODTest] 선언한 핵심 본(kneeIK) · 사용처의 본(finger) · 스켈레톤 소켓의 부모 본과 그 부모 사슬이 메시 마스크에서도 빠지지 않는지 검증
 */
SW_TEST_CASE( SkeletonBoneLODTest, CoreBonesSocketsAndTheirParentsAreKept )
{
    using Util          = SkeletonBoneLODTestUtil;
    const Skeleton base = Util::makeSkeleton();
    // 소켓은 스켈레톤 데이터에서 바로 나온다 — 변형이 더한 소켓의 부모 본도 지킨다.
    SkeletonVariant variant{};
    variant._base = "base.skeleton.xml";
    SkeletonSocket socket{};
    socket._name       = hashed_string( "Muzzle" );
    socket._parentBone = hashed_string( "finger" );
    variant._listSocket.push_back( socket );
    Skeleton skeleton;
    SW_REQUIRE_TRUE( skeleton.applyVariant( variant, base, "test" ) );

    SkeletonBoneLOD boneLOD;
    SW_REQUIRE_TRUE( boneLOD.loadFromXMLText( R"(<SkeletonBoneLOD><coreBones><bone name="kneeIK"/></coreBones></SkeletonBoneLOD>)", "test" ) );
    SW_EXPECT_EQUAL( 0u, boneLOD.getLevelCount() ); // 단계가 없으면 메시 LOD 에서 짓는다
    vector<uint8> requiredMask;
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( skeleton, &boneLOD, {}, requiredMask, "test" ) );
    SW_EXPECT_TRUE( requiredMask[Util::kKneeIK] == SW_TRUE );
    SW_EXPECT_TRUE( requiredMask[Util::kFinger] == SW_TRUE ); // 소켓 부모
    SW_EXPECT_TRUE( requiredMask[Util::kHand] == SW_TRUE );   // 그 부모 사슬
    SW_EXPECT_TRUE( requiredMask[Util::kHead] == SW_FALSE );

    vector<vector<uint8>> listMask;
    SkeletonBoneLOD::computeMeshMasks( skeleton, *Util::makeMesh(), requiredMask, listMask );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listMask.size() ) );
    SW_EXPECT_TRUE( listMask[0][Util::kFinger] == SW_TRUE );
    SW_EXPECT_TRUE( listMask[0][Util::kKneeIK] == SW_TRUE );

    // 사용처가 알린 본(`SkeletalMeshComponent::requireBones` — 리그 · 래그돌 · 알림)도 같은 길이다.
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( base, nullptr, { hashed_string( "finger" ), hashed_string( "notInSkeleton" ) }, requiredMask, "test" ) );
    SW_EXPECT_TRUE( requiredMask[Util::kFinger] == SW_TRUE );
    SW_EXPECT_TRUE( requiredMask[Util::kSpine] == SW_TRUE );
}

/**
 * @brief [SkeletonBoneLODTest] 손 파일이 같은 파일의 핵심 본을 빼면 읽기 오류이고, 사용처의 핵심 본의 부모를 빼면(자손으로 빠짐) 마스크 짓기 오류로 마스크가 없는지 검증
 */
SW_TEST_CASE( SkeletonBoneLODTest, HandWrittenLevelsMustNotRemoveCoreBones )
{
    using Util              = SkeletonBoneLODTestUtil;
    const Skeleton skeleton = Util::makeSkeleton();
    {
        SW_EXPECT_LOG( "SkeletonBoneLOD.CoreBoneRemoved" );
        SkeletonBoneLOD boneLOD;
        SW_EXPECT_FALSE( boneLOD.loadFromXMLText(
            R"(<SkeletonBoneLOD><coreBones><bone name="kneeIK"/></coreBones><levels><level max_screen_size="0.2"><remove><item>kneeIK</item></remove></level></levels></SkeletonBoneLOD>)", "test" ) );
        SW_EXPECT_EQUAL( 0u, boneLOD.getLevelCount() );
    }
    SkeletonBoneLOD boneLOD;
    SW_REQUIRE_TRUE( boneLOD.loadFromXMLText( R"(<SkeletonBoneLOD><levels><level max_screen_size="0.2"><remove><item>hand</item></remove></level></levels></SkeletonBoneLOD>)", "test" ) );
    vector<uint8>         requiredMask;
    vector<vector<uint8>> listMask;
    // 핵심 본이 없으면 손 표 그대로다.
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( skeleton, &boneLOD, {}, requiredMask, "test" ) );
    SW_REQUIRE_TRUE( boneLOD.computeMasks( skeleton, requiredMask, listMask, "test" ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listMask.size() ) );
    SW_EXPECT_TRUE( listMask[0][Util::kFinger] == SW_FALSE );
    // 사용처가 손가락을 쓰면(소켓) 손을 빼는 표는 오류다 — 조용히 손가락을 굳히지 않는다.
    SW_EXPECT_LOG( "SkeletonBoneLOD.CoreBoneRemoved" );
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( skeleton, &boneLOD, { hashed_string( "finger" ) }, requiredMask, "test" ) );
    SW_EXPECT_FALSE( boneLOD.computeMasks( skeleton, requiredMask, listMask, "test" ) );
    SW_EXPECT_TRUE( listMask.empty() );
}

/**
 * @brief [SkeletonBoneLODTest] 변형의 본 LOD 파일이 `_base` 로 부모의 핵심 본을 물려받고, 더하기와 `_op="Remove"` 빼기만 적는지 검증
 */
SW_TEST_CASE( SkeletonBoneLODTest, VariantInheritsParentCoreBones )
{
    SkeletonBoneLODTestSource source;
    source.add( "a/base.bonelod.xml", R"(<SkeletonBoneLOD><coreBones><bone name="kneeIK"/><bone name="head"/></coreBones></SkeletonBoneLOD>)" );
    SkeletonBoneLOD boneLOD;
    SW_REQUIRE_TRUE( boneLOD.loadFromXMLText( R"(<SkeletonBoneLOD _base="a/base.bonelod.xml"><coreBones><bone name="cape"/><bone name="head" _op="Remove"/></coreBones></SkeletonBoneLOD>)",
                                              "a/variant.bonelod.xml", source ) );
    const vector<hashed_string>& listCore = boneLOD.getCoreBones();
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listCore.size() ) );
    SW_EXPECT_TRUE( listCore[0] == hashed_string( "kneeIK" ) );
    SW_EXPECT_TRUE( listCore[1] == hashed_string( "cape" ) );

    // 변형이 뒤에 붙인 본(망토)은 부모 파일 이름으로 지켜진다 — 이름으로 찾으므로 부모 본 번호와 무관하다.
    const Skeleton  base = SkeletonBoneLODTestUtil::makeSkeleton();
    SkeletonVariant variant{};
    variant._base = "base.skeleton.xml";
    SkeletonAddedBone cape{};
    cape._name       = hashed_string( "cape" );
    cape._parentBone = hashed_string( "spine" );
    variant._listAddedBone.push_back( cape );
    Skeleton skeleton;
    SW_REQUIRE_TRUE( skeleton.applyVariant( variant, base, "test" ) );
    vector<uint8> requiredMask;
    SW_REQUIRE_TRUE( SkeletonBoneLOD::computeRequiredMask( skeleton, &boneLOD, {}, requiredMask, "test" ) );
    SW_EXPECT_TRUE( requiredMask[skeleton.findBoneIndex( hashed_string( "cape" ) ).value()] == SW_TRUE );
    SW_EXPECT_TRUE( requiredMask[SkeletonBoneLODTestUtil::kHead] == SW_FALSE );
}
