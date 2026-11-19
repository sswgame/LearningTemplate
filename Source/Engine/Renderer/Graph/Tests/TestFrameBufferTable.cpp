/**
 * @file TestFrameBufferTable.cpp
 * @brief 프레임 그래프 자원 표(`FrameBufferTable`) 테스트 — 등록 · 찾기, 같은 이름 · 칸 두 번과 칸마다 다른 처음 접근 거절입니다(GPU 없음).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Graph/FrameBufferTable.h"

using namespace sw;

/**
 * @brief [FrameBufferTableTest] 등록한 핸들을 이름 · 칸 · 종류로 찾는다 — 다른 칸 · 종류 · 이름은 0 이다
 * @details 결함이면 레벨 콜백이 다른 뷰의 버퍼를 옮기거나(칸을 보지 않는다) 같은 이름의 텍스처를 버퍼로 옮긴다.
 */
SW_TEST_CASE( FrameBufferTableTest, RegisteredHandlesAreFoundByNameAndSlot )
{
    FrameBufferTable    table;
    const hashed_string hzb( "HZB" );
    const hashed_string history( "DepthHistory" );
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 0, RHIBufferHandle{ 11 }, RenderGraphAccess::ShaderRead ) );
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 2, RHIBufferHandle{ 12 }, RenderGraphAccess::ShaderRead ) );
    SW_REQUIRE_TRUE( table.registerTexture( history, 0, RHITextureHandle{ 21 }, RenderGraphAccess::UnorderedAccess ) );

    SW_EXPECT_EQUAL( RHIBufferHandle{ 11 }, table.findBuffer( hzb, 0 ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 12 }, table.findBuffer( hzb, 2 ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, table.findBuffer( hzb, 1 ) );     // 등록하지 않은 칸
    SW_EXPECT_EQUAL( RHITextureHandle{ 0 }, table.findTexture( hzb, 0 ) );   // 종류가 다르다
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, table.findBuffer( history, 0 ) ); // 텍스처를 버퍼로 찾지 않는다
    SW_EXPECT_EQUAL( RHITextureHandle{ 21 }, table.findTexture( history, 0 ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, table.getEntries().size() );

    // 프레임마다 비운다 — 지난 프레임의 핸들이 남지 않는다.
    table.reset();
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, table.findBuffer( hzb, 0 ) );
    SW_EXPECT_TRUE( table.getEntries().empty() );
}

/**
 * @brief [FrameBufferTableTest] 같은 이름 · 칸을 두 번, 핸들 0, 칸마다 다른 처음 접근 · 종류를 거절하고 앞 등록을 지킨다
 * @details 결함이면 뒤 등록이 앞 버퍼를 덮어 레벨 콜백이 엉뚱한 버퍼를 옮기거나, 그래프가 이름마다 하나 든 상태가 칸마다 달라 한 전이로 옮기지 못한다.
 */
SW_TEST_CASE( FrameBufferTableTest, DuplicateNamesAreRejected )
{
    SW_EXPECT_LOG( "FrameBufferTable.DuplicateName" );
    SW_EXPECT_LOG( "FrameBufferTable.InvalidHandle" );
    SW_EXPECT_LOG( "FrameBufferTable.SlotMismatch" );
    FrameBufferTable    table;
    const hashed_string hzb( "HZB" );
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 0, RHIBufferHandle{ 11 }, RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hzb, 0, RHIBufferHandle{ 99 }, RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hashed_string( "LightGrid" ), 0, RHIBufferHandle{ 0 }, RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hzb, 1, RHIBufferHandle{ 13 }, RenderGraphAccess::UnorderedAccess ) );
    SW_EXPECT_FALSE( table.registerTexture( hzb, 1, RHITextureHandle{ 14 }, RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 11 }, table.findBuffer( hzb, 0 ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, table.getEntries().size() );
}
