#include "pch.h"

#include "Core/Common/Macros.h"

#include "TestFramework/TestFramework.h"

// 단언 대화상자는 Debug 의 멈추는 단언에만 있다(`Core/Common/Macros.h` 9 절). 대화상자 자체(Windows MessageBoxW)는 App 이 걸고, 여기서는
// 대화상자 자리에 시험 함수를 걸어 세 가지 답(이번만 · 늘 무시 · 멈춤)과 가로채기가 먼저인지를 본다.
#if defined( SW_DEBUG )
namespace
{
    struct AssertDialogTestInternal
    {
        inline static uint32 _s_callCount = 0;

        static sw::internal::AssertAction answerIgnoreOnce( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return sw::internal::AssertAction::IgnoreOnce;
        }

        static sw::internal::AssertAction answerIgnoreAlways( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return sw::internal::AssertAction::IgnoreAlways;
        }

        static sw::internal::AssertAction answerBreak( const utf8*, const utf8*, const utf8*, int32 )
        {
            ++_s_callCount;
            return sw::internal::AssertAction::Break;
        }

        /** @brief 같은 자리(이 함수의 한 줄)에서 단언이 어긋난다. */
        static void failAtOneSite( bool bCondition ) { SW_ASSERT( bCondition ); }
    };

    /** @brief 시험이 건 대화상자를 끝에 뗀다(다른 시험이 멈추는 단언을 기대한다). */
    struct ScopedAssertDialog
    {
        explicit ScopedAssertDialog( sw::internal::AssertDialogFunc pfnDialog )
        {
            AssertDialogTestInternal::_s_callCount = 0;
            sw::internal::setAssertDialog( pfnDialog );
        }
        ~ScopedAssertDialog() { sw::internal::setAssertDialog( nullptr ); }
        ScopedAssertDialog( const ScopedAssertDialog& )            = delete;
        ScopedAssertDialog& operator=( const ScopedAssertDialog& ) = delete;
    };
} // namespace

/**
 * @brief [AssertDialogTest] "이 자리 늘 무시" 를 고르면 같은 자리의 다음 단언은 묻지도 멈추지도 않는다
 */
SW_TEST_CASE( AssertDialogTest, IgnoreAlwaysAsksOncePerSite )
{
    const ScopedAssertDialog dialog( &AssertDialogTestInternal::answerIgnoreAlways );
    AssertDialogTestInternal::failAtOneSite( false );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( AssertDialogTestInternal::_s_callCount, 1u );
    SW_EXPECT_TRUE( sw::internal::hasAssertDialog() );
}

/**
 * @brief [AssertDialogTest] "이번만" 이면 같은 자리라도 다시 묻는다
 */
SW_TEST_CASE( AssertDialogTest, IgnoreOnceAsksEveryTime )
{
    const ScopedAssertDialog dialog( &AssertDialogTestInternal::answerIgnoreOnce );
    AssertDialogTestInternal::failAtOneSite( false );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( AssertDialogTestInternal::_s_callCount, 2u );
}

/**
 * @brief [AssertDialogTest] 대화상자가 없으면 멈춰야 한다고 답하고(지금 동작), "멈춤" 을 고르면 멈춰야 한다고 답한다
 */
SW_TEST_CASE( AssertDialogTest, BreakWithoutDialogOrWhenChosen )
{
    sw::internal::setAssertDialog( nullptr );
    SW_EXPECT_FALSE( sw::internal::hasAssertDialog() );
    SW_EXPECT_TRUE( sw::internal::shouldBreakOnAssert( "false", nullptr, __FILE__, __LINE__ ) );

    const ScopedAssertDialog dialog( &AssertDialogTestInternal::answerBreak );
    SW_EXPECT_TRUE( sw::internal::shouldBreakOnAssert( "false", "message", __FILE__, __LINE__ ) );
    SW_EXPECT_EQUAL( AssertDialogTestInternal::_s_callCount, 1u );
}

/**
 * @brief [AssertDialogTest] 시험의 단언 가로채기가 먼저다 — 걸려 있으면 대화상자를 부르지 않는다
 */
SW_TEST_CASE( AssertDialogTest, CaptureComesBeforeTheDialog )
{
    const ScopedAssertDialog        dialog( &AssertDialogTestInternal::answerBreak );
    const test::ScopedAssertCapture capture;
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( capture.getCount(), 1u );
    SW_EXPECT_EQUAL( AssertDialogTestInternal::_s_callCount, 0u );
}
#endif
