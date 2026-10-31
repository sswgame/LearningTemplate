/**
 * @file FlowFieldCache.h
 * @brief 목적지 셀마다 흐름장 하나를 여럿이 나눠 쓰는 캐시입니다. 같은 곳으로 가는 무리가 흐름장 하나를 빌리고, 다 쓰면 돌려줍니다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Navigation/FlowField.h"

namespace sw
{
    class NavGrid;

    /**
     * @class FlowFieldCache
     * @brief 목적지 셀을 키로 흐름장을 빌려 주고(`acquire`) 쓰는 수를 셉니다. 아무도 쓰지 않는 슬롯은 다음 목적지가 다시 씁니다.
     * @details 슬롯은 deque 라서 움직이지 않는다 — 빌린 쪽(명령 · 행위자)이 포인터를 쥐어도 된다. 격자가 바뀌면 `refreshStale` 이 쓰는 흐름장만 다시 구한다.
     *          RTS 월드에서 떼었다 — 무리 이동을 쓰는 다른 키트(타워 디펜스 · 군중)도 같은 캐시를 쓴다(Supreme Commander · SC2 의 목적지별 흐름장 공유).
     */
    class SW_GF_API FlowFieldCache
    {
    public:
        /** @brief @p goal 로 가는 흐름장을 빌립니다. 같은 목적지를 쓰는 흐름장이 있으면 그것을, 없으면 빈 슬롯에 새로 구합니다. */
        const FlowField* acquire( const NavGrid& grid, const int2& goal );
        /** @brief 빌린 흐름장을 돌려줍니다. 이 캐시의 것이 아니거나 이미 다 돌려받았으면 아무것도 하지 않습니다. */
        void release( const FlowField* pField );
        /** @brief 누군가 쓰는 흐름장 가운데 @p grid 가 바뀌어 낡은 것을 다시 구합니다. */
        void refreshStale( const NavGrid& grid );
        /** @brief 슬롯을 모두 버립니다. 빌려 간 포인터는 모두 무효가 됩니다. */
        void clear() { _listSlot.clear(); }

    private:
        struct Slot
        {
            FlowField _field{};
            int2      _goal{ -1, -1 };
            int32     _userCount{ 0 };
        };

        deque<Slot> _listSlot;
    };
} // namespace sw
