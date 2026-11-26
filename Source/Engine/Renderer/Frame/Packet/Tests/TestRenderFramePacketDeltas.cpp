#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"

// 패킷 차분 — 렌더 스레드는 패킷을 받는 즉시 차분 묶음(`RenderFramePacketDeltas`)을 통째로 옮겨 쌓는다(`RenderThread::executePacket` 첫 줄).
// 그리지 않고 끝난 패킷의 차분도 대기 묶음에 남아 다음 패킷의 것과 이어진다. 칸을 더하고 옮기기를 빠뜨리면 크기 검사(static_assert)가 컴파일에서 멈춘다(nogpu).

using namespace sw;

/**
 * @brief [RenderFramePacketDeltasTest] 그리지 않은 패킷의 차분은 대기 묶음에 남아 다음 패킷의 차분과 순서대로 이어지고, 옮긴 패킷의 차분은 비어 다시 쓰여도 두 번 받지 않는다
 * @details 변이: `appendFrom` 이 칸 하나(사라진 씬 키 · 글리프 업로드)를 빠뜨리면 그 칸의 개수 단언이 진다. 옮긴 뒤 원본을 비우지 않으면 마지막 단언이 진다.
 */
SW_TEST_CASE( RenderFramePacketDeltasTest, DeltasMoveWholeAndSurviveUndrawnPackets )
{
    RenderFramePacketDeltas pending;

    RenderFramePacket first; // 그리지 않고 끝나는 패킷(시점 없음)
    first._deltas._listRetiredSceneKey.push_back( 7u );
    first._deltas._listAtlasUpload.emplace_back()._sequence = 1;
    pending.appendFrom( first._deltas );
    SW_EXPECT_TRUE( first._deltas._listRetiredSceneKey.empty() );
    SW_EXPECT_TRUE( first._deltas._listAtlasUpload.empty() );

    RenderFramePacket second;
    second._deltas._listRetiredSceneKey.push_back( 9u );
    second._deltas._listAtlasUpload.emplace_back()._sequence = 2;
    pending.appendFrom( second._deltas );

    SW_REQUIRE_EQUAL( size_t{ 2 }, pending._listRetiredSceneKey.size() );
    SW_EXPECT_EQUAL( 7u, pending._listRetiredSceneKey[0] );
    SW_EXPECT_EQUAL( 9u, pending._listRetiredSceneKey[1] );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pending._listAtlasUpload.size() );
    SW_EXPECT_EQUAL( uint64{ 1 }, pending._listAtlasUpload[0]._sequence ); // 묶음 번호 순서가 남는다(거울이 이어진 묶음으로 받는다)
    SW_EXPECT_EQUAL( uint64{ 2 }, pending._listAtlasUpload[1]._sequence );

    pending.appendFrom( first._deltas ); // 이미 옮긴 패킷을 다시 받아도 늘지 않는다
    SW_EXPECT_EQUAL( size_t{ 2 }, pending._listRetiredSceneKey.size() );
}
