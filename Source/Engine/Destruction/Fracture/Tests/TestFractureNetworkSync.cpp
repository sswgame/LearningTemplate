#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Destruction/Fracture/FractureNetworkSync.h"

// FractureNetworkSyncTest — 파괴 컴포넌트의 네트워크 쪽 상태(최근 해시 · 자세 목표 대기열 · 스냅샷 바이트 형식)는 엔진 없이 정해진다.

namespace
{
    struct TestFractureNetworkSyncInternal
    {
        static sw::FractureGroupPose makePose( uint32 groupID, float32 x, bool bGone )
        {
            sw::FractureGroupPose pose;
            pose._groupID  = groupID;
            pose._position = sw::float3{ x, 2.0f, -3.0f };
            pose._rotation = sw::quaternion::makeFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f }, 0.5f );
            pose._bGone    = bGone ? SW_TRUE : SW_FALSE;
            return pose;
        }

        static sw::vector<uint8> makeStateBytes()
        {
            sw::vector<uint8> stateBytes;
            for ( uint32 index = 0; index < 37; ++index )
            {
                stateBytes.push_back( static_cast<uint8>( index * 7 + 1 ) );
            }
            return stateBytes;
        }
    };
} // namespace

/**
 * @brief [FractureNetworkSyncTest] 쓴 스냅샷을 읽으면 상태 바이트와 그룹 자세가 그대로 돌아오고, 사라진 그룹은 번호와 사라짐만 실린다
 */
SW_TEST_CASE( FractureNetworkSyncTest, SnapshotRoundTripsStateBytesAndPoses )
{
    using Internal                               = TestFractureNetworkSyncInternal;
    const sw::vector<uint8>           stateBytes = Internal::makeStateBytes();
    sw::vector<sw::FractureGroupPose> listPose;
    listPose.push_back( Internal::makePose( 3, 1.25f, false ) );
    listPose.push_back( Internal::makePose( 9, 4.0f, true ) );
    sw::vector<uint8> bytes;
    sw::FractureNetworkSync::writeSnapshot( stateBytes, listPose, bytes );

    sw::vector<uint8>                                 readStateBytes;
    sw::vector<sw::FractureGroupPose>                 listReadPose;
    const sw::FractureNetworkSync::SnapshotReadResult result = sw::FractureNetworkSync::readSnapshot( bytes, readStateBytes, listReadPose );
    SW_REQUIRE_TRUE( result == sw::FractureNetworkSync::SnapshotReadResult::Success );
    SW_EXPECT_TRUE( readStateBytes == stateBytes );
    SW_REQUIRE_EQUAL( static_cast<uint32>( listReadPose.size() ), 2u );
    SW_EXPECT_EQUAL( listReadPose[0]._groupID, 3u );
    SW_EXPECT_EQUAL( listReadPose[0]._bGone, static_cast<uint8>( SW_FALSE ) );
    SW_EXPECT_EQUAL( listReadPose[0]._position._x, listPose[0]._position._x );
    SW_EXPECT_EQUAL( listReadPose[0]._position._z, listPose[0]._position._z );
    SW_EXPECT_EQUAL( listReadPose[0]._rotation._z, listPose[0]._rotation._z );
    SW_EXPECT_EQUAL( listReadPose[0]._rotation._w, listPose[0]._rotation._w );
    SW_EXPECT_EQUAL( listReadPose[1]._groupID, 9u );
    SW_EXPECT_EQUAL( listReadPose[1]._bGone, static_cast<uint8>( SW_TRUE ) );
    SW_EXPECT_EQUAL( listReadPose[1]._position._x, 0.0f ); // 사라진 그룹은 자세를 싣지 않는다
}

/**
 * @brief [FractureNetworkSyncTest] 매직이 다르거나, 상태 바이트가 없거나, 자세가 잘린 스냅샷은 그 이유로 거절한다
 */
SW_TEST_CASE( FractureNetworkSyncTest, BrokenSnapshotReportsWhereItFailed )
{
    using Internal = TestFractureNetworkSyncInternal;
    sw::vector<sw::FractureGroupPose> listPose;
    listPose.push_back( Internal::makePose( 3, 1.25f, false ) );
    sw::vector<uint8> bytes;
    sw::FractureNetworkSync::writeSnapshot( Internal::makeStateBytes(), listPose, bytes );

    sw::vector<uint8>                 readStateBytes;
    sw::vector<sw::FractureGroupPose> listReadPose;
    sw::vector<uint8>                 wrongMagic = bytes;
    wrongMagic[0] ^= 0xFF;
    SW_EXPECT_TRUE( sw::FractureNetworkSync::readSnapshot( wrongMagic, readStateBytes, listReadPose ) == sw::FractureNetworkSync::SnapshotReadResult::WrongMagic );

    sw::vector<uint8> emptyState;
    sw::FractureNetworkSync::writeSnapshot( sw::vector<uint8>{}, listPose, emptyState );
    SW_EXPECT_TRUE( sw::FractureNetworkSync::readSnapshot( emptyState, readStateBytes, listReadPose ) == sw::FractureNetworkSync::SnapshotReadResult::StateTruncated );

    // 마지막 float 하나(32 비트)를 넘게 자른다. 상태 바이트는 앞에 온전히 남는다.
    sw::vector<uint8> cutPose = bytes;
    cutPose.resize( cutPose.size() - 5 );
    SW_EXPECT_TRUE( sw::FractureNetworkSync::readSnapshot( cutPose, readStateBytes, listReadPose ) == sw::FractureNetworkSync::SnapshotReadResult::PoseTruncated );
}

/**
 * @brief [FractureNetworkSyncTest] 같은 그룹의 자세 목표는 새 것이 자리를 지켜 바꾸고, 꺼내면 대기열이 빈다
 */
SW_TEST_CASE( FractureNetworkSyncTest, DriveTargetReplacesSameGroupInPlace )
{
    using Internal = TestFractureNetworkSyncInternal;
    sw::FractureNetworkSync sync;
    sync.queueDriveTarget( Internal::makePose( 4, 1.0f, false ) );
    sync.queueDriveTarget( Internal::makePose( 7, 2.0f, false ) );
    sync.queueDriveTarget( Internal::makePose( 4, 3.0f, false ) );

    sw::vector<sw::FractureGroupPose> listTarget;
    sync.takeDriveTargets( listTarget );
    SW_REQUIRE_EQUAL( static_cast<uint32>( listTarget.size() ), 2u );
    SW_EXPECT_EQUAL( listTarget[0]._groupID, 4u );
    SW_EXPECT_EQUAL( listTarget[0]._position._x, 3.0f );
    SW_EXPECT_EQUAL( listTarget[1]._groupID, 7u );
    sync.takeDriveTargets( listTarget );
    SW_EXPECT_TRUE( listTarget.empty() );
}

/**
 * @brief [FractureNetworkSyncTest] 기록하지 않은 사건 수의 해시는 없고, 기록한 것은 사건 수로 찾는다
 */
SW_TEST_CASE( FractureNetworkSyncTest, RecentStateHashIsFoundByEventCount )
{
    sw::FractureNetworkSync sync;
    uint64                  hash = 0;
    SW_EXPECT_FALSE( sync.findStateHash( 1, hash ) );
    sync.recordStateHash( 1, 0x1111u );
    sync.recordStateHash( 2, 0x2222u );
    SW_REQUIRE_TRUE( sync.findStateHash( 1, hash ) );
    SW_EXPECT_EQUAL( hash, static_cast<uint64>( 0x1111u ) );
    SW_REQUIRE_TRUE( sync.findStateHash( 2, hash ) );
    SW_EXPECT_EQUAL( hash, static_cast<uint64>( 0x2222u ) );
    SW_EXPECT_FALSE( sync.findStateHash( 3, hash ) );
}
