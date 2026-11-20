/**
 * @file TestFrameBufferTable.cpp
 * @brief 프레임 그래프 자원 표(`FrameBufferTable`) 테스트 — 등록 · 찾기, 같은 이름 · 칸 두 번과 칸마다 다른 처음 접근 거절, 패스 문맥이 선언한 접근 핸들로만 핸들을 주는지(GPU 없음).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

using namespace sw;

namespace
{
    /** @brief 핸들 @p handle 의 구조 버퍼(원소 8 바이트 · 16 개)입니다 — 표는 크기 있는 등록만 받는다. */
    RHIStructuredBuffer makeBuffer( uint64 handle )
    {
        RHIStructuredBuffer buffer{};
        buffer._buffer           = RHIBufferHandle{ handle };
        buffer._elementSize      = 8;
        buffer._capacityElements = 16;
        return buffer;
    }
} // namespace

/**
 * @brief [FrameBufferTableTest] 등록한 핸들을 이름 · 칸 · 종류로 찾는다 — 다른 칸 · 종류 · 이름은 0 이다
 * @details 결함이면 레벨 콜백이 다른 뷰의 버퍼를 옮기거나(칸을 보지 않는다) 같은 이름의 텍스처를 버퍼로 옮긴다.
 */
SW_TEST_CASE( FrameBufferTableTest, RegisteredHandlesAreFoundByNameAndSlot )
{
    FrameBufferTable    table;
    const hashed_string hzb( "HZB" );
    const hashed_string history( "DepthHistory" );
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead ) );
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 2, makeBuffer( 12 ), RenderGraphAccess::ShaderRead ) );
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
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hzb, 0, makeBuffer( 99 ), RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hashed_string( "LightGrid" ), 0, makeBuffer( 0 ), RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_FALSE( table.registerBuffer( hzb, 1, makeBuffer( 13 ), RenderGraphAccess::UnorderedAccess ) );
    SW_EXPECT_FALSE( table.registerTexture( hzb, 1, RHITextureHandle{ 14 }, RenderGraphAccess::ShaderRead ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 11 }, table.findBuffer( hzb, 0 ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, table.getEntries().size() );
}

/**
 * @brief [FrameBufferTableTest] 패스 몸통은 선언한 접근 핸들로만 핸들을 받는다 — 다른 접근 · 선언 안 한 이름은 오류와 0, UAV 배리어는 선언한 UAV 의 등록 칸에만 나간다
 * @details 결함이면 SRV 로 선언한 버퍼를 UAV 로 묶거나(그래프가 낸 상태와 어긋나 DX12 · Vulkan 검증 오류), 선언하지 않은 버퍼를 전이 없이 묶는다(지난 내용을 읽는다).
 */
SW_TEST_CASE( FrameBufferTableTest, PassContextHandsOutOnlyDeclaredAccesses )
{
    SW_EXPECT_LOG( "Renderer.GraphUndeclaredAccess" );
    SW_EXPECT_LOG( "Renderer.GraphUnregisteredResource" );
    const hashed_string hzb( "HZB" );
    const hashed_string grid( "LightGrid" );
    FrameBufferTable    table;
    SW_REQUIRE_TRUE( table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead ) );
    SW_REQUIRE_TRUE( table.registerBuffer( grid, 0, makeBuffer( 31 ), RenderGraphAccess::ShaderRead ) );

    const vector<RenderGraphResourceAccess> listAccess{ RenderGraphBufferUAV{ hzb }, RenderGraphBufferSRV{ grid } };
    test::FakeRHICommandList                cmd( 0 );
    RenderGraphPassContext                  context;
    context._passName       = hashed_string( "HZBBuild" );
    context._pListAccess    = &listAccess;
    context._pCmdList       = &cmd;
    context._pResourceTable = &table;

    SW_EXPECT_EQUAL( RHIBufferHandle{ 11 }, context.getBuffer( RenderGraphBufferUAV{ hzb } ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 31 }, context.getBuffer( RenderGraphBufferSRV{ grid } ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, context.getBuffer( RenderGraphBufferUAV{ grid } ) );                     // SRV 로 선언했다
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, context.getBuffer( RenderGraphBufferSRV{ hashed_string( "Other" ) } ) ); // 선언하지 않았다
    SW_EXPECT_EQUAL( RHITextureHandle{ 0 }, context.getTexture( RenderGraphTextureUAV{ hzb } ) );                   // 종류가 다르다
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, context.getBuffer( RenderGraphBufferUAV{ hzb }, 3 ) );                   // 등록하지 않은 칸

    context.uavBarrier( RenderGraphBufferUAV{ hzb } );
    SW_EXPECT_EQUAL( uint32{ 1 }, cmd._uavBarrierCount );
    context.uavBarrier( RenderGraphBufferUAV{ grid } ); // SRV 로 선언한 버퍼 — 배리어가 나가지 않는다
    SW_EXPECT_EQUAL( uint32{ 1 }, cmd._uavBarrierCount );

    // 표가 없는 문맥(뷰 그래프)은 선언했어도 핸들이 없다.
    context._pResourceTable = nullptr;
    SW_EXPECT_EQUAL( RHIBufferHandle{ 0 }, context.getBuffer( RenderGraphBufferUAV{ hzb } ) );
}

/**
 * @brief [FrameBufferTableTest] 등록은 크기(원소 바이트 · 용량)를 함께 적고, 크기 없는 버퍼는 거절한다
 * @details 결함이면 버퍼 보기(`RenderBufferInspector`)가 크기를 모르는 버퍼를 아무 길이로 복사해 남의 메모리 끝을 넘는다.
 */
SW_TEST_CASE( FrameBufferTableTest, StructuredRegistrationCarriesSize )
{
    SW_EXPECT_LOG( "FrameBufferTable.SizeMissing" );
    FrameBufferTable    table;
    RHIStructuredBuffer pyramid{};
    pyramid._buffer           = RHIBufferHandle{ 31 };
    pyramid._elementSize      = 8;
    pyramid._capacityElements = 100;
    SW_REQUIRE_TRUE( table.registerBuffer( hashed_string( "HZB" ), 0, pyramid, RenderGraphAccess::ShaderRead ) );
    RHIStructuredBuffer emptyBuffer{};
    emptyBuffer._buffer = RHIBufferHandle{ 32 };
    SW_EXPECT_FALSE( table.registerBuffer( hashed_string( "Raw" ), 0, emptyBuffer, RenderGraphAccess::ShaderRead ) ); // 크기 없는 버퍼는 거절
    SW_REQUIRE_EQUAL( size_t{ 1 }, table.getEntries().size() );
    SW_EXPECT_EQUAL( 8u, table.getEntries()[0]._elementSize );
    SW_EXPECT_EQUAL( 100u, table.getEntries()[0]._elementCount );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 31 }, table.findBuffer( hashed_string( "HZB" ), 0 ) );
}
