// 테스트 틀 — 케이스 쓰기 경로는 한 케이스 안에서 같은 이름이면 같고 이름마다 다르며, 단언 가로채기(`test::ScopedAssertCapture`)는 멈추지 않고 세고 겹쳐 걸 수 있다.
// 거르개 · 몫 · 스냅숏 · 로그 선언 · 픽스처 · 매개변수 케이스는 `TestTestRunner.cpp` 가 본다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief 같은 이름을 다시 물으면 같은 경로다(쓴 것을 읽는다), 다른 이름은 다른 경로이고 이름 · 확장자는 그대로 남는다(로더가 확장자로 형식을 고른다).
 */
SW_TEST_CASE( TestFrameworkTest, ScratchPathIsStableWithinACaseAndDistinctPerName )
{
    const string path = test::makeScratchPath( "sample.wav" );
    SW_EXPECT_TRUE( StringUtil::endsWith( path, "sample.wav" ) );
    SW_EXPECT_EQUAL( path, test::makeScratchPath( "sample.wav" ) );
    SW_EXPECT_TRUE( path != test::makeScratchPath( "other.wav" ) );
    SW_EXPECT_TRUE( FileUtil::isDirectory( FileUtil::getDirectoryPart( path ) ) ); // 폴더는 처음 부를 때 선다
}

#if defined( SW_DEBUG )
/**
 * @brief 단언 가로채기 안에서는 SW_ASSERT 가 멈추지 않고 세어지며 그 뒤 줄이 돈다 — 겹쳐 건 가로채기가 풀려도 바깥은 그대로다(Debug 만 단언이 있다).
 */
SW_TEST_CASE( TestFrameworkTest, AssertCaptureCountsInsteadOfBreaking )
{
    test::ScopedAssertCapture outer;
    bool                      bReachedAfterAssert = false;
    SW_ASSERT( 1 + 1 == 3 );
    bReachedAfterAssert = true;
    SW_EXPECT_TRUE( bReachedAfterAssert );
    SW_EXPECT_EQUAL( 1u, outer.getCount() );

    {
        test::ScopedAssertCapture inner;
        SW_ASSERT( false );
        SW_EXPECT_EQUAL( 1u, inner.getCount() );
    }
    SW_ASSERT( false ); // 안쪽이 풀려도 바깥 가로채기는 그대로다
    SW_EXPECT_EQUAL( 3u, outer.getCount() );
}
#endif
