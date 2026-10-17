/**
 * @file OnlineMethodTableTestUtil.h
 * @brief 메서드가 없는 테스트 프로토콜 항목이 쓰는 빈 메서드 표입니다. 영역만 다른 항목 여럿을 만드는 호스트 · 레지스트리 테스트가 씁니다.
 * @details 열거자가 없으므로 등록 항목이 리플렉션에서 이름을 찾지 않습니다(`OnlineProtocolEntry::computeMethodHash`).
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

namespace test
{
    /** @brief 메서드가 없는 테스트 키트의 메서드 이름입니다. */
    enum class TestNoMethodName : uint8
    {
        Count,
    };

    /** @brief @p RangeBase 영역의 빈 메서드 표입니다. */
    template <uint16 RangeBase>
    struct TestEmptyMethodTable : sw::OnlineMethodTable<TestNoMethodName, RangeBase, sw::OnlineNoPushName>
    {
    };
} // namespace test
