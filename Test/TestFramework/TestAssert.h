/**
 * @file Test/TestFramework/TestAssert.h
 * @brief 단언 — `SW_EXPECT_*`(기록하고 계속) · `SW_ASSERT_*`(기록하고 그 케이스를 끝낸다)와 실패를 기록하는 바깥 함수
 * @details 실패는 지금 스레드의 레지스트리 케이스(또는 `ScopedFailureCapture`)에 남는다. 작업 스레드에서 불러도 된다 — 기록은 락으로 한 줄씩 선다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include <sstream>

namespace test
{
    // ------------------------------------------------------------------------------
    // 실패 보고 — 단언 매크로가 부르는 **바깥** 함수들
    //
    // 단언 자리에는 비교와 이 호출 하나만 남긴다. 실패 경로(문자열 둘 · `ostringstream` · 서식)를 단언마다 인라인으로
    // 펼치면 단언이 수천 곳이라 그것이 테스트 바이너리의 대부분이 되고, 최적화 빌드는 그것을 더 펼쳐 **Release 바이너리가
    // Debug 보다 커진다.** 실패는 드물고 느려도 되는 경로다.
    // ------------------------------------------------------------------------------
    /** @brief 단언 실패 하나를 기록합니다. @param pMessage 덧붙일 말(널이면 없음). */
    SW_NOINLINE void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const utf8* pMessage );
    /** @brief 단언 실패 하나를 덧붙일 말과 함께 기록합니다. */
    SW_NOINLINE void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const sw::string& message );

    /** @brief 같아야 할 두 값이 다를 때 — 두 값을 찍어 실패를 기록합니다. */
    template <typename TExpected, typename TActual>
    SW_NOINLINE void reportNotEqual( const utf8* pCondition, const utf8* pFile, int32 line, const TExpected& expected, const TActual& actual )
    {
        std::ostringstream oss;
        oss << "Expected [" << expected << "], Actual [" << actual << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /** @brief 달라야 할 두 값이 같을 때 — 그 값을 찍어 실패를 기록합니다. */
    template <typename TValue>
    SW_NOINLINE void reportEqual( const utf8* pCondition, const utf8* pFile, int32 line, const TValue& value )
    {
        std::ostringstream oss;
        oss << "Expected not equal to [" << value << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /** @brief 허용 오차를 넘었을 때 — 차이와 오차를 찍어 실패를 기록합니다. */
    template <typename TDifference, typename TTolerance>
    SW_NOINLINE void reportNotNear( const utf8* pCondition, const utf8* pFile, int32 line, const TDifference& difference, const TTolerance& tolerance )
    {
        std::ostringstream oss;
        oss << "Diff [" << difference << "] exceeds tolerance [" << tolerance << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /**
     * @brief `SW_EXPECT_STREQ` 가 비교하는 글 — 복사하지 않는 뷰와, 그것이 널 포인터였는지.
     * @details 널 `const utf8*` 로 `sw::string` 을 만들면 그 자리에서 죽는다. 그러면 실패를 찍어 보기도 전에
     *          **테스트 바이너리 전체가 내려가고**, 같은 파일의 뒤쪽 케이스가 통째로 사라진다. 그런데 "널을 돌려주기
     *          시작한 회귀" 야말로 이 매크로가 가장 잡아야 할 것이다. 그리고 널을 `"<null>"` 로 찍는 것만으로는
     *          부족하다 — 진짜 `"<null>"` 문자열과 널이 같다고 나온다. 그래서 널 여부를 따로 든다.
     */
    struct ComparableText
    {
        sw::string_view _text;
        bool            _bNull{ false };
    };
} // namespace test

namespace test
{
    /** @brief 널에도 안전하게 비교용 글을 만듭니다. 받는 형태는 이 셋이다 — 나머지는 이 중 하나로 바뀌어 들어온다. */
    inline ComparableText toComparableText( const utf8* pText )
    {
        return pText != nullptr ? ComparableText{ pText, false } : ComparableText{ {}, true };
    }
    inline ComparableText toComparableText( const sw::string& text )
    {
        return ComparableText{ text, false };
    }
    inline ComparableText toComparableText( sw::string_view text )
    {
        return ComparableText{ text, false };
    }

    /** @brief 두 글이 같은지 보고, 다르면 두 글을 찍어 실패를 기록합니다(`SW_EXPECT_STREQ`). */
    void expectSameText( const utf8* pCondition, const utf8* pFile, int32 line, const ComparableText& expected, const ComparableText& actual );
} // namespace test

// 단언은 전부 아래 둘 중 하나의 뼈대다 — `onFail` 이 `(void)0` 이면 EXPECT(기록하고 계속), `return` 이면 ASSERT(기록하고
// 그 케이스를 끝낸다). 조건 글(`#cond`)은 **바깥 매크로에서**
// 만든다 — 안쪽 뼈대로 넘어간 인자는 이미 매크로가 풀린 뒤라, 거기서 만들면 `SW_TRUE` 가 `1` 로 찍힌다.

/** @brief 조건 단언의 뼈대 — `bPassed` 가 거짓이면 실패를 기록하고 `onFail` 을 실행합니다. */
#define SW_TEST_CHECK_IMPL( bPassed, pCondition, message, onFail )          \
    do                                                                      \
    {                                                                       \
        if ( !( bPassed ) )                                                 \
        {                                                                   \
            test::reportFailure( pCondition, __FILE__, __LINE__, message ); \
            onFail;                                                         \
        }                                                                   \
    } while ( 0 )

/** @brief 값 비교 단언의 뼈대 — 두 값을 한 번씩만 평가하고, 다르면 두 값을 찍어 기록한 뒤 `onFail` 을 실행합니다. */
#define SW_TEST_CHECK_EQUAL_IMPL( expected, actual, pCondition, onFail )                    \
    do                                                                                      \
    {                                                                                       \
        const auto _swExpected = ( expected );                                              \
        const auto _swActual   = ( actual );                                                \
        if ( !( _swExpected == _swActual ) )                                                \
        {                                                                                   \
            test::reportNotEqual( pCondition, __FILE__, __LINE__, _swExpected, _swActual ); \
            onFail;                                                                         \
        }                                                                                   \
    } while ( 0 )

/** @brief 약한 기대 — 실패를 기록하고 계속합니다. */
#define SW_EXPECT_TRUE( cond ) SW_TEST_CHECK_IMPL( cond, #cond, nullptr, (void)0 )

/** @brief 약한 기대 — 실패 시 메시지를 함께 기록합니다. */
#define SW_EXPECT_TRUE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( cond, #cond, msg, (void)0 )

/** @brief 조건이 거짓이어야 합니다. */
#define SW_EXPECT_FALSE( cond ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", nullptr, (void)0 )

/**
 * @brief 조건이 거짓이어야 하며, 실패 시 메시지를 함께 기록합니다.
 * @details **실패했을 때 가장 설명이 필요한 쪽이 부정 단언**이다(무엇이 열려 있으면 안 되는지). 그래서
 *          `SW_EXPECT_TRUE_MSG( x == false, ... )` 로 뒤집어 쓰지 않게 짝을 맞춘다.
 */
#define SW_EXPECT_FALSE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", msg, (void)0 )

/** @brief 두 값이 같아야 합니다. */
#define SW_EXPECT_EQUAL( expected, actual ) SW_TEST_CHECK_EQUAL_IMPL( expected, actual, #actual " == " #expected, (void)0 )

/** @brief 두 값이 달라야 합니다. */
#define SW_EXPECT_NOT_EQUAL( expected, actual )                                             \
    do                                                                                      \
    {                                                                                       \
        const auto _swExpected = ( expected );                                              \
        const auto _swActual   = ( actual );                                                \
        if ( _swExpected == _swActual )                                                     \
            test::reportEqual( #actual " != " #expected, __FILE__, __LINE__, _swExpected ); \
    } while ( 0 )

/** @brief 허용 오차 안에서 두 값이 가까워야 합니다. */
#define SW_EXPECT_NEAR_EQUAL( expected, actual, tolerance )                                                                         \
    do                                                                                                                              \
    {                                                                                                                               \
        const auto _swTolerance  = ( tolerance );                                                                                   \
        const auto _swDifference = sw::MathUtil::abs( ( expected ) - ( actual ) );                                                  \
        if ( _swDifference > _swTolerance )                                                                                         \
            test::reportNotNear( "|" #actual " - " #expected "| <= " #tolerance, __FILE__, __LINE__, _swDifference, _swTolerance ); \
    } while ( 0 )

/** @brief 포인터가 null 이 아니어야 합니다. */
#define SW_EXPECT_NOT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) != nullptr, #ptr " != nullptr", nullptr, (void)0 )

/** @brief 포인터가 null 이어야 합니다. */
#define SW_EXPECT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) == nullptr, #ptr " == nullptr", nullptr, (void)0 )

/** @brief null 종료/문자열 유사 값을 비교합니다 — 널은 널끼리만 같습니다. */
#define SW_EXPECT_STREQ( expected, actual ) \
    test::expectSameText( #actual " == " #expected, __FILE__, __LINE__, test::toComparableText( expected ), test::toComparableText( actual ) )

/** @brief 컨테이너/문자열이 비어 있어야 합니다. */
#define SW_EXPECT_EMPTY( value ) SW_TEST_CHECK_IMPL( ( value ).empty(), #value ".empty()", nullptr, (void)0 )

/** @brief 강한 어서션 — 실패를 기록하고 현재 테스트를 중단합니다. */
#define SW_ASSERT_TRUE( cond ) SW_TEST_CHECK_IMPL( cond, #cond, nullptr, return )

/** @brief 강한 어서션 — 실패 시 메시지(대개 실제로 받은 텍스트)를 함께 기록하고 중단합니다. */
#define SW_ASSERT_TRUE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( cond, #cond, msg, return )

/** @brief 조건이 거짓이어야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_FALSE( cond ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", nullptr, return )

/** @brief 두 값이 같아야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_EQUAL( expected, actual ) SW_TEST_CHECK_EQUAL_IMPL( expected, actual, #actual " == " #expected, return )

/** @brief 포인터가 null 이 아니어야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_NOT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) != nullptr, #ptr " != nullptr", nullptr, return )

// `SW_ASSERT_NULL` 은 **일부러 없다.** 짝을 맞추려고 만들 수는 있지만 부를 자리가 없다
// (`SW_EXPECT_NULL` 을 쓰는 자리는 실패해도 계속 가는 게 맞는 자리들이다).
// 쓰는 곳이 생기면 그때 위 뼈대로 한 줄 넣는다.
