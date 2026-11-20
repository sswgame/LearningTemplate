// TypeChain 범위 검사 — 담긴 수(`size()`)를 넘는 색인이 개발 구성에서 단언으로 잡히고, 경계 안은 잡히지 않는지(음성 대조).
#include "pch.h"

#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/TypeInfo.h"

using namespace sw;

/**
 * @brief [TypeChainBoundsTest] 담긴 수를 넘는 색인
 * @details 고정 배열 칸은 상한까지 있으므로 담긴 수 밖 색인도 배열 안을 읽는다 — 그래서 크기로 막지 않으면 빈 칸(nullptr)이 조용히 나왔다.
 */
SW_TEST_CASE( TypeChainBoundsTest, IndexPastCountIsChecked )
{
    static_assert( constant::reflection::kMaxParentChainDepth >= 2 );
    TypeChain chain;
    chain._count = 1; // 칸 [0] 만 담았다(값은 nullptr — 색인 검사만 본다)
    {
        const test::ScopedAssertCapture assertCapture;
        SW_EXPECT_TRUE( chain[0] == nullptr );
        SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    }
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
    {
        const test::ScopedAssertCapture assertCapture;
        (void)chain[1];
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
}
