/**
 * @file TestFrameBufferTable.cpp
 * @brief 프레임 그래프 자원 표(`FrameBufferTable`) 테스트 — 등록 · 찾기, 같은 이름 · 칸 두 번과 칸마다 다른 처음 접근 거절, 패스 문맥이 선언한 접근 핸들로만 핸들을 주는지,
 *        그래프가 끝에 등록 접근으로 되돌리는지(GPU 없음).
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
    table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( hzb, 2, makeBuffer( 12 ), RenderGraphAccess::ShaderRead );
    table.registerTexture( history, 0, RHITextureHandle{ 21 }, RenderGraphAccess::UnorderedAccess );

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
    table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( hzb, 0, makeBuffer( 99 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( hashed_string( "LightGrid" ), 0, makeBuffer( 0 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( hzb, 1, makeBuffer( 13 ), RenderGraphAccess::UnorderedAccess );
    table.registerTexture( hzb, 1, RHITextureHandle{ 14 }, RenderGraphAccess::ShaderRead );
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
    table.registerBuffer( hzb, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( grid, 0, makeBuffer( 31 ), RenderGraphAccess::ShaderRead );

    const vector<RenderGraphResourceAccess> listAccess{ RenderGraphBufferUAV{ hzb }, RenderGraphBufferSRV{ grid } };
    test::FakeRHICommandList                cmd( 0 );
    RenderGraphPassContext                  context;
    context._passName    = hashed_string( "HZBBuild" );
    context._pListAccess = &listAccess;
    context.setCommandList( &cmd );
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

    // 표가 없는 문맥은 선언했어도 핸들이 없다.
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
    table.registerBuffer( hashed_string( "HZB" ), 0, pyramid, RenderGraphAccess::ShaderRead );
    RHIStructuredBuffer emptyBuffer{};
    emptyBuffer._buffer = RHIBufferHandle{ 32 };
    table.registerBuffer( hashed_string( "Raw" ), 0, emptyBuffer, RenderGraphAccess::ShaderRead ); // 크기 없는 버퍼는 거절
    SW_REQUIRE_EQUAL( size_t{ 1 }, table.getEntries().size() );
    SW_EXPECT_EQUAL( 8u, table.getEntries()[0]._elementSize );
    SW_EXPECT_EQUAL( 100u, table.getEntries()[0]._elementCount );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 31 }, table.findBuffer( hashed_string( "HZB" ), 0 ) );
}

/**
 * @brief [FrameBufferTableTest] 그래프는 등록 접근과 다른 접근에 둔 이름만 끝에 되돌린다(이름마다 전이 하나 — 레벨 콜백이 모든 칸에 낸다) — 같은 접근으로 끝난 이름 · 그래프가 보지 않은 이름은 아니다
 * @details 결함이면 정렬 · 메시 쓰기가 셰이더 읽기로 남긴 입자 인자를 투명 패스가 간접 인자로 읽거나(DX12 · Vulkan 검증 오류), 이미 맞는 버퍼에 전이를 다시 낸다.
 *          입자 패스 셋(시뮬레이션 UAV → 정렬 · 메시 쓰기 셰이더 읽기)의 모양을 그대로 쓴다.
 */
SW_TEST_CASE( FrameBufferTableTest, ChangedAccessesReturnToTheirInitialAccess )
{
    const hashed_string particles( "Particles" );
    const hashed_string args( "ParticleArgs" );
    const hashed_string state( "ParticleState" );
    const hashed_string order( "ParticleOrder" );
    FrameBufferTable    table;
    table.registerBuffer( state, 0, makeBuffer( 10 ), RenderGraphAccess::UnorderedAccess );
    table.registerBuffer( particles, 0, makeBuffer( 11 ), RenderGraphAccess::ShaderRead );
    table.registerBuffer( args, 0, makeBuffer( 12 ), RenderGraphAccess::IndirectArgs );
    table.registerBuffer( args, 1, makeBuffer( 13 ), RenderGraphAccess::IndirectArgs );
    table.registerBuffer( order, 0, makeBuffer( 14 ), RenderGraphAccess::ShaderRead ); // 이 그래프의 어느 패스도 선언하지 않는다

    RenderGraph graph;
    graph.importResources( table );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ParticleSimulation" ), { RenderGraphBufferUAV{ state }, RenderGraphBufferUAV{ particles }, RenderGraphBufferUAV{ args } } ) );
    SW_REQUIRE_TRUE( graph.addPass( hashed_string( "ParticleSort" ), { RenderGraphBufferSRV{ particles }, RenderGraphBufferSRV{ args } } ) );
    SW_REQUIRE_TRUE( graph.compile() );
    vector<RenderGraphBarrier> listLastBarrier;
    graph.setLevelPrologue( SW_DELEGATE_LAMBDA( RenderGraphLevelPrologueFn, [&listLastBarrier]( const RenderGraphLevelContext& levelContext )
    { listLastBarrier = *levelContext._pListBarrier; } ) );
    RenderGraphExecutionContext context;
    SW_REQUIRE_TRUE( graph.execute( context ) );

    // 상태(UAV 로 시작 · 끝) · 렌더 입력(셰이더 읽기로 끝) · 순서(그래프가 보지 않았다)는 그대로, 인자만 간접 인자로 되돌린다.
    SW_REQUIRE_EQUAL( size_t{ 1 }, listLastBarrier.size() );
    SW_EXPECT_TRUE( listLastBarrier[0]._resource == args );
    SW_EXPECT_TRUE( listLastBarrier[0]._access == RenderGraphAccess::IndirectArgs );
    SW_EXPECT_TRUE( context.findAccess( args ) == RenderGraphAccess::IndirectArgs );
}

/**
 * @brief [FrameBufferTableTest] 패스 안 전이(`transition`)는 쓰기로 선언한 등록 자원만 이 패스의 리스트에 낸다 — 선언하지 않은 이름은 오류이고 아무것도 내지 않는다
 * @details 패스 기록 면(`IRHIPassCommandList`)에는 텍스처 전이가 없어 이 길 하나다. 읽기 접근 핸들로 부르는 코드는 컴파일되지 않는다(static_assert — 그래프가 레벨 앞에서 옮긴다).
 *          결함이면 병렬 레벨의 워커가 선언하지 않은 자원의 추적 상태를 바꾼다(앞 리스트가 배리어 없이 먼저 실행된다 — RHI 검토 S1).
 *          변이: 선언 검사를 빼면 선언 안 한 전이가 리스트에 들어가 개수 단언이, 버퍼 판을 텍스처로 내면 버퍼 단언이 진다.
 */
SW_TEST_CASE( FrameBufferTableTest, PassTransitionsOnlyDeclaredWrites )
{
    SW_EXPECT_LOG( "Renderer.GraphUndeclaredAccess" );
    SW_EXPECT_LOG( "Renderer.GraphTransitionWithoutState" );
    const hashed_string target( "AutoExposureTarget" );
    const hashed_string hzb( "HZB" );
    FrameBufferTable    table;
    table.registerTexture( target, 0, RHITextureHandle{ 41 }, RenderGraphAccess::ShaderRead );
    table.registerBuffer( hzb, 0, makeBuffer( 42 ), RenderGraphAccess::ShaderRead );

    const vector<RenderGraphResourceAccess> listAccess{ RenderGraphTextureUAV{ target }, RenderGraphBufferUAV{ hzb } };
    test::FakeRHICommandList                cmd( 0 );
    RenderGraphPassContext                  context;
    context._passName    = hashed_string( "Tonemap" );
    context._pListAccess = &listAccess;
    context.setCommandList( &cmd );
    context._pResourceTable = &table;

    context.transition( RenderGraphTextureUAV{ target }, RenderGraphAccess::ShaderRead );
    SW_REQUIRE_EQUAL( size_t{ 1 }, cmd._listTextureTransition.size() );
    SW_EXPECT_EQUAL( RHITextureHandle{ 41 }, cmd._listTextureTransition[0]._texture );
    SW_EXPECT_TRUE( cmd._listTextureTransition[0]._state == RHITextureState::ShaderResource );

    context.transition( RenderGraphBufferUAV{ hzb }, RenderGraphAccess::ShaderRead );
    SW_REQUIRE_EQUAL( size_t{ 1 }, cmd._listBufferTransition.size() );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 42 }, cmd._listBufferTransition[0]._buffer );
    SW_EXPECT_TRUE( cmd._listBufferTransition[0]._state == RHIBufferState::ShaderResource );

    context.transition( RenderGraphTextureUAV{ hashed_string( "Undeclared" ) }, RenderGraphAccess::ShaderRead ); // 선언하지 않았다 — 오류, 전이 없음
    context.transition( RenderGraphRenderTarget{ target }, RenderGraphAccess::ShaderRead );                      // 다른 접근으로 선언했다 — 오류, 전이 없음
    context.transition( RenderGraphTextureUAV{ target }, RenderGraphAccess::IndirectArgs );                      // 텍스처에 없는 접근 — 오류, 전이 없음
    SW_EXPECT_EQUAL( size_t{ 1 }, cmd._listTextureTransition.size() );
}

/**
 * @brief [FrameBufferTableTest] `isRegistered` 는 선언했고 이번 프레임 표에 등록된 칸만 참이다 — 오류를 남기지 않는다(등록 조건이 곧 실행 조건)
 * @details 결함이면 쉬는 프레임(볼륨 안개 · 프로브 · 마커 · 2 단계 가림)마다 "등록 없음" 오류가 남거나, 몸통이 등록 판정을 다시 하며 두 판정이 갈린다.
 *          변이: 선언 검사를 빼면 선언하지 않은 이름이 참이 되어, 표를 보지 않으면 등록하지 않은 칸이 참이 되어 진다.
 */
SW_TEST_CASE( FrameBufferTableTest, IsRegisteredIsQuietAndNeedsBothDeclarationAndRegistration )
{
    const hashed_string orders( "OccludedOrders" );
    const hashed_string args( "CullArgs" );
    FrameBufferTable    table;
    table.registerBuffer( orders, 0, makeBuffer( 51 ), RenderGraphAccess::UnorderedAccess );

    const vector<RenderGraphResourceAccess> listAccess{ RenderGraphBufferUAV{ orders }, RenderGraphBufferUAV{ args } };
    RenderGraphPassContext                  context;
    context._passName       = hashed_string( "LateOcclusion" );
    context._pListAccess    = &listAccess;
    context._pResourceTable = &table;

    SW_EXPECT_TRUE( context.isRegistered( RenderGraphBufferUAV{ orders } ) );
    SW_EXPECT_FALSE( context.isRegistered( RenderGraphBufferUAV{ orders }, 1 ) );                     // 등록하지 않은 칸
    SW_EXPECT_FALSE( context.isRegistered( RenderGraphBufferUAV{ args } ) );                          // 선언했지만 이번 프레임에 등록하지 않았다
    SW_EXPECT_FALSE( context.isRegistered( RenderGraphBufferSRV{ orders } ) );                        // 다른 접근
    SW_EXPECT_FALSE( context.isRegistered( RenderGraphBufferUAV{ hashed_string( "Undeclared" ) } ) ); // 선언하지 않았다
}
