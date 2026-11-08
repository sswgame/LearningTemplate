#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"

using namespace sw;

/**
 * @brief [GPUBreadcrumbsTest] DX12 꼴 [시작 칸, 끝 칸] — 끝 칸이 마지막으로 끝난 패스, 그와 다른 시작 칸이 돌던 패스다
 */
SW_TEST_CASE( GPUBreadcrumbsTest, ParseSeparatesCompletedAndInFlight )
{
    const uint32                 arrMarker[] = { 7u, 6u | GPUBreadcrumbs::kEndMarkerBit };
    const GPUBreadcrumbs::Report report      = GPUBreadcrumbs::parse( arrMarker, 2 );
    SW_EXPECT_EQUAL( 6u, report._lastCompletedID );
    SW_EXPECT_EQUAL( 1u, report._inFlightCount );
    SW_EXPECT_EQUAL( 7u, report._arrInFlightID[0] );
}

/**
 * @brief [GPUBreadcrumbsTest] 시작 칸과 끝 칸이 같은 패스면 돌던 패스가 없다(패스 사이에서 잃었다)
 */
SW_TEST_CASE( GPUBreadcrumbsTest, ParseCompletedPassIsNotInFlight )
{
    const uint32                 arrMarker[] = { 9u, 9u | GPUBreadcrumbs::kEndMarkerBit };
    const GPUBreadcrumbs::Report report      = GPUBreadcrumbs::parse( arrMarker, 2 );
    SW_EXPECT_EQUAL( 9u, report._lastCompletedID );
    SW_EXPECT_EQUAL( 0u, report._inFlightCount );
}

/**
 * @brief [GPUBreadcrumbsTest] Vulkan 꼴(단계마다 하나, 같은 값이 겹친다) — 0 과 중복은 건너뛰고, 끝 표지가 없으면 끝난 패스가 없다
 */
SW_TEST_CASE( GPUBreadcrumbsTest, ParseSkipsZeroAndDuplicates )
{
    const uint32                 arrMarker[] = { 0u, 3u, 3u, 4u };
    const GPUBreadcrumbs::Report report      = GPUBreadcrumbs::parse( arrMarker, 4 );
    SW_EXPECT_EQUAL( 0u, report._lastCompletedID );
    SW_EXPECT_EQUAL( 2u, report._inFlightCount );
    SW_EXPECT_EQUAL( 3u, report._arrInFlightID[0] );
    SW_EXPECT_EQUAL( 4u, report._arrInFlightID[1] );

    const GPUBreadcrumbs::Report empty = GPUBreadcrumbs::parse( nullptr, 0 );
    SW_EXPECT_EQUAL( 0u, empty._lastCompletedID );
    SW_EXPECT_EQUAL( 0u, empty._inFlightCount );
}
