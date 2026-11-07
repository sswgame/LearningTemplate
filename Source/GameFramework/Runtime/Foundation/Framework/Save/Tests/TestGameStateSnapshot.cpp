// GameStateSnapshotTest — 상태 스냅샷 봉투: 왕복, 머리(SWST) · 판이 다른 버퍼, 잘린 버퍼, 실패한 읽기가 실어 둔 상태를 남기지 않는 것(nogpu, 활성 씬 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"

namespace
{
    /** @brief 씬 · 리플렉션 상태 없이 찍은 봉투입니다(커스텀 상태 없이도 지원하는 사용법). */
    sw::vector<uint8> captureEmptyEnvelope( sw::GameStateSnapshot& snapshot )
    {
        sw::vector<uint8> bytes;
        snapshot.beginCapture();
        if ( snapshot.writeEnvelope( sw::SerializePurpose::Snapshot, nullptr, nullptr, bytes ) == false )
            bytes.clear();
        return bytes;
    }
} // namespace

using namespace sw;

/** @brief [GameStateSnapshotTest] 같은 목적으로 쓴 봉투는 다시 읽히고, 읽은 뒤 저장소가 비어 있다 */
SW_TEST_CASE( GameStateSnapshotTest, EnvelopeRoundTrips )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 ); // 머리(4 + 4 + 8) 뒤에 섹션 셋
    SW_EXPECT_TRUE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), static_cast<uint32>( bytes.size() ) ) );
    snapshot.endRestore();
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
}

/** @brief [GameStateSnapshotTest] 머리가 없거나 판이 다른 버퍼는 이유를 대며 거절한다 — 옛 형식 리더를 두지 않는다 */
SW_TEST_CASE( GameStateSnapshotTest, RejectsForeignHeaderAndOtherVersion )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 );

    vector<uint8> foreign = bytes;
    foreign[0]            = static_cast<uint8>( foreign[0] ^ 0xFFu );
    SW_EXPECT_LOG( "GameStateSnapshot.StateSnapshotNoSWST" );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, foreign.data(), static_cast<uint32>( foreign.size() ) ) );

    vector<uint8> otherVersion = bytes;
    otherVersion[4]            = static_cast<uint8>( otherVersion[4] + 1u ); // 판 번호(머리 둘째 칸)의 낮은 바이트
    SW_EXPECT_LOG( "GameStateSnapshot.StateSnapshotVersionNot" );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, otherVersion.data(), static_cast<uint32>( otherVersion.size() ) ) );
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
}

/** @brief [GameStateSnapshotTest] 잘린 버퍼는 어디서 잘렸든 거절하고 실어 둔 상태를 남기지 않는다 */
SW_TEST_CASE( GameStateSnapshotTest, RejectsTruncatedBuffersAndKeepsTheStoreEmpty )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, nullptr, 0 ) );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), 8 ) ); // 머리만 반
    for ( uint32 size = 16; size < static_cast<uint32>( bytes.size() ); ++size )
    {
        SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), size ) );
        SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
    }
}
