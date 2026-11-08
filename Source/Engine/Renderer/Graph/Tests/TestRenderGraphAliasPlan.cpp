/**
 * @file TestRenderGraphAliasPlan.cpp
 * @brief 트랜지언트 앨리어싱 계획(`RenderGraphAliasPlan::build`) · 대여 풀(`TransientLeasePool`) 테스트 — 레벨 구간 겹침, 서술 키, 대여 · 반납 · 쉰 칸 놓기(nogpu).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/Resource/TransientLeasePool.h"
#include "Engine/Renderer/Graph/RenderGraphAliasPlan.h"

using namespace sw;

/**
 * @brief [RenderGraphAliasPlanTest] 앞 수명이 끝난 레벨 뒤에 시작하는 같은 서술의 첨부가 텍스처를 나누고, 같은 레벨에서 맞닿거나 겹치면 나누지 않는다
 * @details 같은 레벨의 패스는 병렬로 기록되어 GPU 에서 겹칠 수 있다 — 끝 레벨 = 시작 레벨은 나누면 안 된다.
 */
SW_TEST_CASE( RenderGraphAliasPlanTest, NonOverlappingLevelsShareOneTexture )
{
    const vector<RenderGraphAliasCandidate> listCandidate = {
        {    hashed_string( "Lit" ), 7, 0, 2}, // 레벨 0 ~ 2
        {  hashed_string( "Bloom" ), 7, 3, 4}, // 3 ~ 4 — Lit 이 끝난 뒤라 Lit 의 텍스처를 쓴다
        {hashed_string( "Outline" ), 7, 4, 5}, // 4 ~ 5 — Bloom 과 레벨 4 를 함께 써 따로다
        {hashed_string( "Tonemap" ), 7, 6, 6}, // 6 — Lit 칸(마지막 4)이 먼저 비었다
    };
    const RenderGraphAliasPlan plan = RenderGraphAliasPlan::build( listCandidate );
    SW_EXPECT_EQUAL( string( "Lit" ), string( plan.findOwner( hashed_string( "Bloom" ) ).c_str() ) );
    SW_EXPECT_TRUE( plan.findOwner( hashed_string( "Outline" ) ).empty() );
    SW_EXPECT_EQUAL( string( "Lit" ), string( plan.findOwner( hashed_string( "Tonemap" ) ).c_str() ) );
    SW_EXPECT_TRUE( plan.findOwner( hashed_string( "Lit" ) ).empty() ); // 주인은 별칭이 아니다
    SW_EXPECT_EQUAL( uint32( 2 ), plan._ownerCount );                   // Lit · Outline 두 장
}

/**
 * @brief [RenderGraphAliasPlanTest] 힙에 겹쳐 놓을 수 있는 칸은 그 칸의 모든 이름이 놓을 수 있을 때만 들고, 레벨 구간은 칸을 쓴 이름들을 합친 것이다
 * @details 첫 쓰기가 지우기가 아닌 이름(Load · RW)이 한 칸에 끼면 그 텍스처의 바이트를 다른 서술과 나눌 수 없다 — 앞 텍스처가 남긴 바이트가 내용이 된다.
 */
SW_TEST_CASE( RenderGraphAliasPlanTest, PlaceableSlotsCarryTheMergedLifetime )
{
    const vector<RenderGraphAliasCandidate> listCandidate = {
        {    hashed_string( "Lit" ), 7, 0, 2, 1},
        {  hashed_string( "Bloom" ), 7, 3, 4, 1}, // Lit 의 칸 — 칸 구간이 0 ~ 4 가 된다
        {hashed_string( "Compute" ), 9, 1, 2, 0}, // RW — 놓을 수 없다
        {hashed_string( "Tonemap" ), 5, 5, 6, 1},
    };
    const RenderGraphAliasPlan plan = RenderGraphAliasPlan::build( listCandidate );
    SW_REQUIRE_EQUAL( size_t( 2 ), plan._listPlaceable.size() );
    for ( const RenderGraphAliasCandidate& placeable : plan._listPlaceable )
    {
        SW_EXPECT_FALSE( placeable._name == hashed_string( "Compute" ) );
        if ( placeable._name == hashed_string( "Lit" ) )
        {
            SW_EXPECT_EQUAL( uint32( 0 ), placeable._firstLevel );
            SW_EXPECT_EQUAL( uint32( 4 ), placeable._lastLevel );
        }
    }
}

/**
 * @brief [RenderGraphAliasPlanTest] 서술(형식 · 나눗수 · 용도 · 클리어 값)이 다르면 수명이 떨어져 있어도 나누지 않는다 — 텍스처 핸들을 그대로 넘기는 나눔이다
 */
SW_TEST_CASE( RenderGraphAliasPlanTest, DifferentDescriptionsNeverShare )
{
    const vector<RenderGraphAliasCandidate> listCandidate = {
        {hashed_string( "HalfBright" ), 1, 0, 1},
        { hashed_string( "FullColor" ), 2, 3, 4},
    };
    const RenderGraphAliasPlan plan = RenderGraphAliasPlan::build( listCandidate );
    SW_EXPECT_TRUE( plan._mapAliasToOwner.empty() );
    SW_EXPECT_EQUAL( uint32( 2 ), plan._ownerCount );
}

/**
 * @brief [RenderGraphAliasPlanTest] 계획은 입력 순서만으로 정해진다 — 같은 목록이면 같은 계획(뷰마다 · 프레임마다 같은 풀 모양)
 */
SW_TEST_CASE( RenderGraphAliasPlanTest, PlanIsDeterministic )
{
    const vector<RenderGraphAliasCandidate> listCandidate = {
        {hashed_string( "A" ), 3, 0, 0},
        {hashed_string( "B" ), 3, 1, 1},
        {hashed_string( "C" ), 3, 1, 2},
        {hashed_string( "D" ), 3, 3, 3},
    };
    const RenderGraphAliasPlan first  = RenderGraphAliasPlan::build( listCandidate );
    const RenderGraphAliasPlan second = RenderGraphAliasPlan::build( listCandidate );
    SW_REQUIRE_EQUAL( first._mapAliasToOwner.size(), second._mapAliasToOwner.size() );
    for ( const auto& [alias, owner] : first._mapAliasToOwner )
    {
        SW_EXPECT_TRUE( second.findOwner( alias ) == owner );
    }
    // B 는 A 칸, C 는 B 와 레벨 1 을 함께 써 새 칸, D 는 먼저 빈 A 칸(마지막 1)을 쓴다.
    SW_EXPECT_EQUAL( string( "A" ), string( first.findOwner( hashed_string( "B" ) ).c_str() ) );
    SW_EXPECT_TRUE( first.findOwner( hashed_string( "C" ) ).empty() );
    SW_EXPECT_EQUAL( string( "A" ), string( first.findOwner( hashed_string( "D" ) ).c_str() ) );
}

/**
 * @brief [TransientLeasePoolTest] 같은 서술은 반납한 칸을 다시 빌리고(다음 뷰 · 다음 프레임), 빌려 나간 칸은 같은 서술이어도 다른 대여에 나가지 않는다
 */
SW_TEST_CASE( TransientLeasePoolTest, ReturnedSlotsAreLeasedAgain )
{
    TransientLeasePool              pool;
    const TransientLeasePool::Lease first  = pool.lease( 11 );
    const TransientLeasePool::Lease second = pool.lease( 11 );
    SW_EXPECT_TRUE( first._bCreated != 0 && second._bCreated != 0 );
    SW_EXPECT_TRUE( first._slot != second._slot ); // 빌려 나간 칸은 겹치지 않는다
    SW_EXPECT_EQUAL( uint32( 2 ), pool.getLeasedSlotCount() );

    pool.returnLeases(); // 뷰 하나를 다 그렸다 — 다음 뷰가 같은 크기 · 형식이면 다시 쓴다
    const TransientLeasePool::Lease again = pool.lease( 11 );
    SW_EXPECT_TRUE( again._bCreated == 0 );
    SW_EXPECT_TRUE( again._slot == first._slot || again._slot == second._slot );

    const TransientLeasePool::Lease other = pool.lease( 22 ); // 서술이 다르면 새 칸
    SW_EXPECT_TRUE( other._bCreated != 0 );
    SW_EXPECT_EQUAL( uint32( 3 ), pool.getLiveSlotCount() );
}

/**
 * @brief [TransientLeasePoolTest] 이어서 빌리지 않은 칸은 정한 프레임 뒤에 한 번 놓을 차례로 나오고, 그 칸 번호는 다음 대여가 새 칸으로 다시 쓴다
 */
SW_TEST_CASE( TransientLeasePoolTest, IdleSlotsAreReleasedOnce )
{
    TransientLeasePool              pool;
    const TransientLeasePool::Lease kept   = pool.lease( 1 );
    const TransientLeasePool::Lease unused = pool.lease( 2 );
    vector<uint32>                  listReleased;
    pool.advanceFrame( listReleased ); // 첫 프레임은 둘 다 썼다
    for ( uint32 frame = 0; frame < TransientLeasePool::kIdleFramesBeforeRelease; ++frame )
    {
        (void)pool.lease( 1 ); // 1 만 계속 쓴다
        pool.advanceFrame( listReleased );
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), listReleased.size() );
    SW_EXPECT_EQUAL( unused._slot, listReleased[0] );
    SW_EXPECT_EQUAL( uint32( 1 ), pool.getLiveSlotCount() );

    const TransientLeasePool::Lease reused = pool.lease( 3 ); // 놓은 칸 번호를 새 칸으로 쓴다
    SW_EXPECT_EQUAL( unused._slot, reused._slot );
    SW_EXPECT_TRUE( reused._bCreated != 0 );
    SW_EXPECT_TRUE( kept._slot != reused._slot );
}
