// 단언 대화상자 — 대화상자 자리에 테스트 함수를 걸어 세 가지 답(이번만 · 늘 무시 · 멈춤)과 단언 가로채기가 먼저인지를 본다(Debug 의 멈추는 단언에만 있다).
#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

#if defined( SW_DEBUG )
namespace
{
    struct AssertDialogTestInternal
    {
        inline static uint32 _s_callCount = 0;

        static internal::AssertAction answerIgnoreOnce( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return internal::AssertAction::IgnoreOnce;
        }

        static internal::AssertAction answerIgnoreAlways( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return internal::AssertAction::IgnoreAlways;
        }

        static internal::AssertAction answerBreak( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return internal::AssertAction::Break;
        }

        /** @brief 같은 자리(이 함수의 한 줄)에서 단언이 어긋난다. */
        static void failAtOneSite( bool bCondition ) { SW_ASSERT( bCondition ); }
    };

    /** @brief 테스트가 건 대화상자를 끝에 뗀다 — `--test` 실행은 대화상자가 없으므로(App 은 대화형 에디터 실행에서만 건다) 떼면 처음 상태다. */
    struct ScopedAssertDialogForTest
    {
        explicit ScopedAssertDialogForTest( internal::AssertDialogFunc pfnDialog )
        {
            AssertDialogTestInternal::_s_callCount = 0;
            internal::setAssertDialog( pfnDialog );
        }
        ~ScopedAssertDialogForTest() { internal::setAssertDialog( nullptr ); }
        ScopedAssertDialogForTest( const ScopedAssertDialogForTest& )            = delete;
        ScopedAssertDialogForTest& operator=( const ScopedAssertDialogForTest& ) = delete;
    };
} // namespace

SW_TEST_CASE( AssertDialogTest, IgnoreAlwaysAsksOncePerSite )
{
    const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerIgnoreAlways );
    AssertDialogTestInternal::failAtOneSite( false );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( 1u, AssertDialogTestInternal::_s_callCount );
    SW_EXPECT_TRUE( internal::hasAssertDialog() );
}

SW_TEST_CASE( AssertDialogTest, IgnoreOnceAsksEveryTime )
{
    const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerIgnoreOnce );
    AssertDialogTestInternal::failAtOneSite( false );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( 2u, AssertDialogTestInternal::_s_callCount );
}

/**
 * @brief 대화상자가 없으면 멈춰야 한다고 답하고, "멈춤" 을 고르면 멈춰야 한다고 답한다(`shouldBreakOnAssert` 를 직접 묻는다 — 실제로 멈추지는 않는다).
 */
SW_TEST_CASE( AssertDialogTest, BreakWithoutDialogOrWhenChosen )
{
    internal::setAssertDialog( nullptr );
    SW_EXPECT_FALSE( internal::hasAssertDialog() );
    SW_EXPECT_TRUE( internal::shouldBreakOnAssert( "false", nullptr, __FILE__, __LINE__ ) );

    const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerBreak );
    SW_EXPECT_TRUE( internal::shouldBreakOnAssert( "false", "message", __FILE__, __LINE__ ) );
    SW_EXPECT_EQUAL( 1u, AssertDialogTestInternal::_s_callCount );
}

/**
 * @brief 테스트의 단언 가로채기가 먼저다 — 걸려 있으면 대화상자("멈춤" 을 고른다)를 부르지 않는다.
 */
SW_TEST_CASE( AssertDialogTest, CaptureComesBeforeTheDialog )
{
    const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerBreak );
    const test::ScopedAssertCapture capture;
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( 1u, capture.getCount() );
    SW_EXPECT_EQUAL( 0u, AssertDialogTestInternal::_s_callCount );
}
#endif
