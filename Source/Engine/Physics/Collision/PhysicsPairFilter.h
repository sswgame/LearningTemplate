/**
 * @file PhysicsPairFilter.h
 * @brief 바디 쌍 단위로 충돌을 끄는 표입니다 — 관절로 이은 이웃 뼈 · 물리 에셋이 적은 쌍(언리얼 Physics Asset 의 Disable Collision).
 * @details 레이어 표(`PhysicsSettings`)는 레이어끼리를, 이 표는 특정 바디 둘을 봅니다. 두 백엔드가 같은 표를 씁니다 — Jolt 는 접촉 검증
 *          (`OnContactValidate`), Box2D 는 사용자 거름 콜백에서 묻습니다. 바꾸는 것은 step 밖(게임 스레드)에서만 하고, 읽기는 step 중 여러
 *          스레드가 동시에 합니다(잠그지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /** @class PhysicsPairFilter @brief 충돌을 끈 바디 쌍의 표입니다. 파일 머리말 참고. */
    class SW_API PhysicsPairFilter
    {
    public:
        PhysicsPairFilter();

        /** @brief 두 바디의 충돌을 켜거나 끕니다(대칭). 같은 바디 둘이면 무시합니다. */
        void setPairCollision( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bCollide );
        /** @brief 두 바디가 부딪혀도 되는지입니다. 표가 비면 바로 true 입니다. */
        bool canCollide( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const;
        /** @brief 사라진 바디들의 쌍을 지웁니다. */
        void removeBodies( span<const PhysicsBodyHandle> listBody );
        /** @brief 표를 비웁니다. */
        void clear() { _mapBodyToDisabled.clear(); }
        /** @brief 쌍이 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _mapBodyToDisabled.empty(); }

    private:
        unordered_map<uint64, vector<uint64>> _mapBodyToDisabled; ///< 바디 → 충돌을 끈 상대들(양쪽에 적는다)
    };
} // namespace sw
