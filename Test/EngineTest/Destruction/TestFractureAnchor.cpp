#include "pch.h"

#include "Engine/Destruction/FractureAnchor.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponentBase.h"
#include "Engine/Destruction/MeshFracture.h"

#include "EngineTest/DestructionTestUtil.h"

#include "TestFramework/TestFramework.h"

// FractureAnchorTest — 앵커 판정(FractureAnchorUtil)은 엔진 · 물리 없이 쿠킹한 에셋 하나로 정해진다: Bottom 은 배율로 나눈 허용 거리, 볼륨은 무게 중심, World 는 넘겨받은 닿음 표.

namespace
{
    struct TestFractureAnchorInternal
    {
        /** @brief 4 × 3 × 0.3 벽(바닥 y = 0)을 벽돌 4 × 3 으로 쪼갭니다. 줄마다 높이 1 입니다. */
        static bool makeWall( sw::FractureAsset& outAsset )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 2.0f, 1.5f, 0.15f }, sw::float3{ 0.0f, 1.5f, 0.0f } );
            sw::FractureSettings            settings;
            settings._pattern          = sw::FracturePattern::Slices;
            settings._arrSliceCount[0] = 4;
            settings._arrSliceCount[1] = 3;
            settings._arrSliceCount[2] = 1;
            settings._sliceJitter      = 0.0f;
            settings._listLevelCount   = { 3 };
            sw::string error;
            return sw::MeshFractureUtil::fracture( listBox, settings, outAsset, error );
        }

        /** @brief 바닥에서 @p height 안에서 시작하는 잎 수입니다(기대값을 판정 함수와 따로 셉니다). */
        static uint32 countLeafStartingBelow( const sw::FractureAsset& asset, float32 height )
        {
            uint32 count = 0;
            for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
            {
                count += asset._listPiece[leaf]._boundsMin._y < height ? 1u : 0u;
            }
            return count;
        }

        static uint32 countAnchor( const sw::vector<uint8>& listAnchor )
        {
            uint32 count = 0;
            for ( const uint8 anchor : listAnchor )
            {
                count += anchor != 0 ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [FractureAnchorTest] Bottom 은 가장 낮은 면에서 (허용 거리 / 배율) 안의 잎만 앵커로 본다 — 배율이 크면 같은 허용 거리가 메시 공간에서 줄어든다
 */
SW_TEST_CASE( FractureAnchorTest, BottomModeDividesToleranceByScale )
{
    using Internal = TestFractureAnchorInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    SW_ASSERT_EQUAL( asset.getPieceCount(), 12u );

    const sw::vector<sw::FractureAnchorVolume> listNoVolume;
    const sw::vector<uint8>                    listNoContact;
    sw::vector<uint8>                          listAnchor;
    // 허용 1.5 m, 배율 1: 아래 두 줄(시작 높이 0 과 1)이 앵커다.
    sw::FractureAnchorUtil::computeAnchors( asset, sw::FractureAnchorMode::Bottom, 1.5f, 1.0f, listNoVolume, listNoContact, listAnchor );
    SW_ASSERT_EQUAL( static_cast<uint32>( listAnchor.size() ), asset.getPieceCount() );
    SW_EXPECT_EQUAL( Internal::countAnchor( listAnchor ), Internal::countLeafStartingBelow( asset, 1.5f ) );
    SW_EXPECT_EQUAL( Internal::countAnchor( listAnchor ), 8u );
    // 같은 허용 거리에 배율 3: 메시 공간 0.5 m 라 맨 아래 줄만 남는다.
    sw::FractureAnchorUtil::computeAnchors( asset, sw::FractureAnchorMode::Bottom, 1.5f, 3.0f, listNoVolume, listNoContact, listAnchor );
    SW_EXPECT_EQUAL( Internal::countAnchor( listAnchor ), 4u );
    for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
    {
        SW_EXPECT_EQUAL( listAnchor[leaf] != 0, sw::FractureAnchorUtil::isBottomLeaf( asset, leaf, 1.5f, 3.0f ) );
    }
}

/**
 * @brief [FractureAnchorTest] 모드가 None 이어도 앵커 볼륨은 늘 더해지고, 판정은 잎의 무게 중심으로 한다
 */
SW_TEST_CASE( FractureAnchorTest, VolumeAnchorsLeafByCentroidInAnyMode )
{
    using Internal = TestFractureAnchorInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    const uint32 target = 5;

    sw::FractureAnchorVolume volume;
    volume._center      = asset._graph._listNode[target]._centroid;
    volume._halfExtents = sw::float3{ 0.01f, 0.01f, 0.01f };
    sw::vector<sw::FractureAnchorVolume> listVolume;
    listVolume.push_back( volume );
    const sw::vector<uint8> listNoContact;
    sw::vector<uint8>       listAnchor;
    sw::FractureAnchorUtil::computeAnchors( asset, sw::FractureAnchorMode::None, 0.05f, 1.0f, listVolume, listNoContact, listAnchor );
    SW_EXPECT_EQUAL( Internal::countAnchor( listAnchor ), 1u );
    SW_EXPECT_EQUAL( listAnchor[target], static_cast<uint8>( 1 ) );

    // 경계 위의 점은 안으로 친다.
    const sw::float3 corner = listVolume[0]._center + listVolume[0]._halfExtents;
    SW_EXPECT_TRUE( sw::FractureAnchorUtil::isInsideAnyVolume( corner, listVolume ) );
    SW_EXPECT_FALSE( sw::FractureAnchorUtil::isInsideAnyVolume( corner + sw::float3{ 0.01f, 0.0f, 0.0f }, listVolume ) );
}

/**
 * @brief [FractureAnchorTest] World 는 넘겨받은 닿음 표만 본다 — 표가 비면(물리를 물을 수 없으면) 앵커가 없다
 */
SW_TEST_CASE( FractureAnchorTest, WorldModeUsesTheContactTable )
{
    using Internal = TestFractureAnchorInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );

    const sw::vector<sw::FractureAnchorVolume> listNoVolume;
    sw::vector<uint8>                          listContact( asset.getPieceCount(), 0 );
    listContact[2] = 1;
    listContact[7] = 1;
    sw::vector<uint8> listAnchor;
    sw::FractureAnchorUtil::computeAnchors( asset, sw::FractureAnchorMode::World, 0.05f, 1.0f, listNoVolume, listContact, listAnchor );
    SW_EXPECT_TRUE( listAnchor == listContact );

    const sw::vector<uint8> listNoContact;
    sw::FractureAnchorUtil::computeAnchors( asset, sw::FractureAnchorMode::World, 0.05f, 1.0f, listNoVolume, listNoContact, listAnchor );
    SW_EXPECT_EQUAL( Internal::countAnchor( listAnchor ), 0u );
}
