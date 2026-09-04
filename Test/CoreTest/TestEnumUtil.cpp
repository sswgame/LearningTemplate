#include "pch.h"

#include "Core/Common/EnumUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_EnumUtil — hasFlag / hasAnyFlag / setFlag / clearFlag
// ------------------------------------------------------------------------------

namespace
{
    /** @brief REFLECT()/ENUM() 매크로도, operator|/&도 없는 평범한 enum class — EnumUtil이
     *         리플렉션·코드젠·손으로 짠 연산자 없이도 완전히 동작함을 증명하기 위한 픽스처입니다. */
    enum class PlainFlag : uint32
    {
        None    = 0,
        Read    = 1 << 0,
        Write   = 1 << 1,
        Execute = 1 << 2,
    };
} // namespace

/**
 * @brief [Core_EnumUtil] hasFlag/hasAnyFlag — 비트 존재 여부 검사
 */
SW_TEST_CASE( Core_EnumUtil, HasFlagAndHasAnyFlag )
{
    const PlainFlag flag = sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Write );

    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, PlainFlag::Read ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, PlainFlag::Write ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flag, sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Write ) ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Execute ) ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, PlainFlag::Execute ) );

    SW_EXPECT_TRUE( sw::EnumUtil::hasAnyFlag( flag, sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Execute ) ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasAnyFlag( flag, PlainFlag::Execute ) );
}

/**
 * @brief [Core_EnumUtil] setFlag/clearFlag — 값을 반환하는 비-변경(순수) 함수
 */
SW_TEST_CASE( Core_EnumUtil, SetFlagAndClearFlag )
{
    const PlainFlag flag = sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Write );

    const PlainFlag withExecute = sw::EnumUtil::setFlag( flag, PlainFlag::Execute );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( withExecute, PlainFlag::Execute ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flag, PlainFlag::Execute ) ); // flag 자체는 불변

    const PlainFlag withoutRead = sw::EnumUtil::clearFlag( withExecute, PlainFlag::Read );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( withoutRead, PlainFlag::Read ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( withoutRead, PlainFlag::Write ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( withoutRead, PlainFlag::Execute ) );
}

/**
 * @brief [Core_EnumUtil] constexpr 컴파일 타임 완전 평가 — 런타임 비용 0을 증명합니다.
 */
SW_TEST_CASE( Core_EnumUtil, CompileTimeEvaluation )
{
    constexpr PlainFlag kCombined = sw::EnumUtil::setFlag( PlainFlag::Read, PlainFlag::Write );
    static_assert( sw::EnumUtil::hasFlag( kCombined, PlainFlag::Read ) );
    static_assert( sw::EnumUtil::hasAnyFlag( kCombined, sw::EnumUtil::setFlag( PlainFlag::Execute, PlainFlag::Read ) ) );
    static_assert( sw::EnumUtil::hasFlag( sw::EnumUtil::setFlag( kCombined, PlainFlag::Execute ), PlainFlag::Execute ) );
    static_assert( sw::EnumUtil::hasFlag( sw::EnumUtil::clearFlag( kCombined, PlainFlag::Read ), PlainFlag::Write ) );

    SW_EXPECT_TRUE( true ); // 위 static_assert 전부가 실질적인 검증입니다.
}
