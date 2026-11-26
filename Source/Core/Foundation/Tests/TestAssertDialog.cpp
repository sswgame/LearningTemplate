// 단언 대화상자 — 대화상자 자리에 테스트 함수를 걸어 세 가지 답(이번만 · 늘 무시 · 멈춤)과 단언 가로채기가 먼저인지를 본다(Debug 의 멈추는 단언에만 있다).
#include "pch.h"

#include "Core/Foundation/AssertDialogSlot.h"
#include "Core/Foundation/Macros.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

#if defined( SW_DO_CHECKS )
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
        internal::AssertDialogSubscription _registration; ///< 범위가 끝나면 대화상자를 뗀다

        explicit ScopedAssertDialogForTest( internal::AssertDialogFunc pfnDialog )
            : _registration{ internal::bindAssertDialog( pfnDialog ) }
        {
            AssertDialogTestInternal::_s_callCount = 0;
        }
        ~ScopedAssertDialogForTest()                                             = default;
        ScopedAssertDialogForTest( const ScopedAssertDialogForTest& )            = delete;
        ScopedAssertDialogForTest& operator=( const ScopedAssertDialogForTest& ) = delete;
    };

    /** @brief 설정(에디터 환경설정 Editor/Diagnostics)을 입힌 뒤 기본값(둘 다 켬)으로 되돌린다 — 다른 케이스가 기본 설정을 본다. */
    struct ScopedAssertDialogOptionsForTest
    {
        ScopedAssertDialogOptionsForTest( bool bShowDialog, bool bRememberIgnoredSites ) { internal::setAssertDialogOptions( bShowDialog, bRememberIgnoredSites ); }
        ~ScopedAssertDialogOptionsForTest() { internal::setAssertDialogOptions( true, true ); }
        ScopedAssertDialogOptionsForTest( const ScopedAssertDialogOptionsForTest& )            = delete;
        ScopedAssertDialogOptionsForTest& operator=( const ScopedAssertDialogOptionsForTest& ) = delete;
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

/** @brief 대화상자를 끄는 설정이면 묻지 않고 멈춰야 한다고 답한다(대화상자를 걸지 않은 실행과 같다). */
SW_TEST_CASE( AssertDialogTest, HiddenDialogBreaksWithoutAsking )
{
    const ScopedAssertDialogForTest        dialog( &AssertDialogTestInternal::answerIgnoreOnce );
    const ScopedAssertDialogOptionsForTest options( false, true );
    SW_EXPECT_FALSE( internal::isAssertDialogShown() );
    SW_EXPECT_TRUE( internal::shouldBreakOnAssert( "false", nullptr, __FILE__, __LINE__ ) );
    SW_EXPECT_EQUAL( 0u, AssertDialogTestInternal::_s_callCount );
}

/** @brief "늘 무시" 를 기억하지 않는 설정이면 같은 자리에서 다시 묻고, 기억한 자리는 지우면 다시 묻는다. */
SW_TEST_CASE( AssertDialogTest, IgnoreAlwaysRespectsRememberOptionAndClear )
{
    const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerIgnoreAlways );
    {
        const ScopedAssertDialogOptionsForTest options( true, false );
        AssertDialogTestInternal::failAtOneSite( false );
        AssertDialogTestInternal::failAtOneSite( false );
        SW_EXPECT_EQUAL( 2u, AssertDialogTestInternal::_s_callCount );
        SW_EXPECT_EQUAL( 0u, internal::getIgnoredAssertSiteCount() );
    }
    AssertDialogTestInternal::failAtOneSite( false );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( 3u, AssertDialogTestInternal::_s_callCount );
    SW_EXPECT_EQUAL( 1u, internal::getIgnoredAssertSiteCount() );
    internal::clearIgnoredAssertSites();
    SW_EXPECT_EQUAL( 0u, internal::getIgnoredAssertSiteCount() );
    AssertDialogTestInternal::failAtOneSite( false );
    SW_EXPECT_EQUAL( 4u, AssertDialogTestInternal::_s_callCount );
}
/** @brief 대화상자는 하나다 — 걸린 동안 둘째 등록은 단언(가로채기로 센다)하고 앞 대화상자를 덮지 않으며, 토큰이 사라지면 떨어진다. */
SW_TEST_CASE( AssertDialogTest, SecondDialogBindAssertsAndKeepsTheFirst )
{
    {
        const ScopedAssertDialogForTest dialog( &AssertDialogTestInternal::answerIgnoreOnce );
        {
            test::ScopedAssertCapture capture;
            SW_EXPECT_LOG( SW_ASSERT_LOG_ID ); // 이중 등록 단언은 이 테스트가 보려는 대상이다
            const internal::AssertDialogSubscription second = internal::bindAssertDialog( &AssertDialogTestInternal::answerBreak );
            SW_EXPECT_FALSE( second.isActive() );
            SW_EXPECT_EQUAL( 1u, capture.getCount() );
        }
        SW_EXPECT_TRUE( internal::hasAssertDialog() );
        SW_EXPECT_FALSE( internal::shouldBreakOnAssert( "false", nullptr, __FILE__, __LINE__ ) ); // 앞 대화상자(이번만 무시)가 답한다
        SW_EXPECT_EQUAL( 1u, AssertDialogTestInternal::_s_callCount );
    }
    SW_EXPECT_FALSE( internal::hasAssertDialog() );
}

#endif
