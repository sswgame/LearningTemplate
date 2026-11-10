#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Animation/Model/BoneMaskHierarchy.h"

#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct BoneMaskHierarchyTestUtil
    {
        /** @brief root → spine → (arm → hand), (head) 스켈레톤입니다. */
        static Skeleton makeSkeleton()
        {
            Skeleton            skeleton;
            const BoneTransform local{};
            const int32         root  = skeleton.addBone( hashed_string( "root" ), -1, local ).value();
            const int32         spine = skeleton.addBone( hashed_string( "spine" ), root, local ).value();
            const int32         arm   = skeleton.addBone( hashed_string( "arm" ), spine, local ).value();
            (void)skeleton.addBone( hashed_string( "hand" ), arm, local );
            (void)skeleton.addBone( hashed_string( "head" ), spine, local );
            return skeleton;
        }

        static AnimationBoneMaskEntry makeEntry( const utf8* pBone, float32 weight, bool bPropagate )
        {
            AnimationBoneMaskEntry entry{};
            entry._bone       = hashed_string( pBone );
            entry._weight     = weight;
            entry._bPropagate = bPropagate ? SW_TRUE : SW_FALSE;
            return entry;
        }

        static const BoneMaskBoneRow& findRow( const vector<BoneMaskBoneRow>& listRow, const utf8* pBone )
        {
            for ( const BoneMaskBoneRow& row : listRow )
            {
                if ( row._bone == hashed_string( pBone ) )
                    return row;
            }
            SW_ASSERT( false );
            return listRow[0];
        }
    };
    using Util = BoneMaskHierarchyTestUtil;
} // namespace

// 보기의 가중치는 런타임 규칙과 같고, 출처는 이 본의 항목 · 조상이 퍼뜨린 값 · 없음으로 갈린다.
SW_TEST_CASE( BoneMaskHierarchyTest, RowsShowRuntimeWeightsAndSources )
{
    const Skeleton       skeleton = Util::makeSkeleton();
    AnimationBoneMaskDef mask{};
    mask._listEntry.push_back( Util::makeEntry( "spine", 0.5f, true ) );
    mask._listEntry.push_back( Util::makeEntry( "arm", 1.0f, false ) );

    vector<BoneMaskBoneRow> listRow;
    vector<hashed_string>   listMissing;
    BoneMaskHierarchy::build( skeleton, mask, listRow, listMissing );
    SW_REQUIRE_EQUAL( 5u, static_cast<uint32>( listRow.size() ) );
    SW_EXPECT_TRUE( listMissing.empty() );

    vector<float32> listRuntime;
    SW_REQUIRE_TRUE( mask.computeBoneWeights( skeleton, listRuntime ) );
    for ( uint32 boneIndex = 0; boneIndex < 5; ++boneIndex )
    {
        SW_EXPECT_EQUAL( listRuntime[boneIndex], listRow[boneIndex]._weight );
    }
    SW_EXPECT_TRUE( Util::findRow( listRow, "root" )._source == BoneMaskWeightSource::None );
    SW_EXPECT_TRUE( Util::findRow( listRow, "spine" )._source == BoneMaskWeightSource::Subtree );
    SW_EXPECT_TRUE( Util::findRow( listRow, "arm" )._source == BoneMaskWeightSource::Bone );
    // 팔의 "bones" 항목은 퍼지지 않는다 — 손은 척추의 값을 물려받는다.
    SW_EXPECT_TRUE( Util::findRow( listRow, "hand" )._source == BoneMaskWeightSource::Inherited );
    SW_EXPECT_EQUAL( 0.5f, Util::findRow( listRow, "hand" )._weight );
    SW_EXPECT_EQUAL( 3u, Util::findRow( listRow, "hand" )._depth );
}

// 스켈레톤에 없는 본은 조용히 지우지 않고 이름으로 알린다. 나머지 항목의 가중치는 그대로 보인다.
SW_TEST_CASE( BoneMaskHierarchyTest, MissingBonesAreListedNotDropped )
{
    const Skeleton       skeleton = Util::makeSkeleton();
    AnimationBoneMaskDef mask{};
    mask._listEntry.push_back( Util::makeEntry( "tail", 1.0f, true ) );
    mask._listEntry.push_back( Util::makeEntry( "tail", 0.5f, false ) );
    mask._listEntry.push_back( Util::makeEntry( "head", 0.25f, true ) );

    vector<BoneMaskBoneRow> listRow;
    vector<hashed_string>   listMissing;
    BoneMaskHierarchy::build( skeleton, mask, listRow, listMissing );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listMissing.size() ) );
    SW_EXPECT_TRUE( listMissing[0] == hashed_string( "tail" ) );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( mask._listEntry.size() ) );
    SW_EXPECT_EQUAL( 0.25f, Util::findRow( listRow, "head" )._weight );
}

// 자손까지 칠하면 그 본의 "bones" 항목을 지우고 "roots" 하나를 둔다. 같은 값을 다시 칠하면 바뀐 것이 없다.
SW_TEST_CASE( BoneMaskHierarchyTest, PaintSubtreeReplacesTheBoneEntry )
{
    AnimationBoneMaskDef mask{};
    mask._listEntry.push_back( Util::makeEntry( "arm", 0.2f, false ) );
    SW_EXPECT_TRUE( BoneMaskHierarchy::paintWeight( mask, hashed_string( "arm" ), 0.8f, true ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( mask._listEntry.size() ) );
    SW_EXPECT_TRUE( mask._listEntry[0]._bPropagate == SW_TRUE );
    SW_EXPECT_EQUAL( 0.8f, mask._listEntry[0]._weight );
    SW_EXPECT_FALSE( BoneMaskHierarchy::paintWeight( mask, hashed_string( "arm" ), 0.8f, true ) );
    // 범위 밖 값은 묶는다(읽기가 거절하는 값을 저장하지 않는다).
    SW_EXPECT_TRUE( BoneMaskHierarchy::paintWeight( mask, hashed_string( "hand" ), 3.0f, false ) );
    SW_EXPECT_EQUAL( 1.0f, mask._listEntry[1]._weight );
}

// 초기화는 그 본의 항목을 모두 지운다. 항목이 하나도 남지 않게 되면 거절한다(로드 오류).
SW_TEST_CASE( BoneMaskHierarchyTest, ResetKeepsAtLeastOneEntry )
{
    AnimationBoneMaskDef mask{};
    mask._listEntry.push_back( Util::makeEntry( "spine", 1.0f, true ) );
    mask._listEntry.push_back( Util::makeEntry( "arm", 0.0f, true ) );
    mask._listEntry.push_back( Util::makeEntry( "arm", 0.5f, false ) );
    SW_EXPECT_FALSE( BoneMaskHierarchy::resetBone( mask, hashed_string( "head" ) ) );
    SW_EXPECT_TRUE( BoneMaskHierarchy::resetBone( mask, hashed_string( "arm" ) ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( mask._listEntry.size() ) );
    SW_EXPECT_FALSE( BoneMaskHierarchy::resetBone( mask, hashed_string( "spine" ) ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( mask._listEntry.size() ) );
}

// 칠한 마스크는 그래프 형식으로 쓰고 다시 읽어도 같은 가중치다(저장 왕복).
SW_TEST_CASE( BoneMaskHierarchyTest, PaintedMaskRoundTripsThroughXML )
{
    const Skeleton       skeleton = Util::makeSkeleton();
    AnimationBoneMaskDef mask{};
    mask._listEntry.push_back( Util::makeEntry( "spine", 1.0f, true ) );
    SW_REQUIRE_TRUE( BoneMaskHierarchy::paintWeight( mask, hashed_string( "head" ), 0.0f, true ) );
    SW_REQUIRE_TRUE( BoneMaskHierarchy::paintWeight( mask, hashed_string( "arm" ), 0.5f, false ) );

    XMLDocument   doc;
    const XMLNode root = doc.appendRoot( "AnimationBoneMaskDef" );
    mask.writeObject( root );
    XMLDocument reread;
    SW_REQUIRE_TRUE( reread.parse( doc.saveToString(), "test" ) );
    AnimationBoneMaskDef loaded{};
    SW_REQUIRE_TRUE( loaded.parseObject( reread.getRoot( "AnimationBoneMaskDef" ), "test" ) );

    vector<float32> listBefore;
    vector<float32> listAfter;
    SW_REQUIRE_TRUE( mask.computeBoneWeights( skeleton, listBefore ) );
    SW_REQUIRE_TRUE( loaded.computeBoneWeights( skeleton, listAfter ) );
    SW_EXPECT_TRUE( listBefore == listAfter );
}
