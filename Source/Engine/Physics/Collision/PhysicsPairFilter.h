/**
 * @file PhysicsPairFilter.h
 * @brief 바디 쌍 단위로 충돌을 끄는 테이블입니다. 관절로 연결된 이웃 뼈와 물리 에셋이 지정한 쌍을 다룹니다(언리얼 Physics Asset 의 Disable Collision).
 * @details 레이어 테이블(`PhysicsSettings`)은 레이어끼리를, 이 테이블은 특정 바디 둘을 본다. 두 백엔드가 같은 테이블을 쓴다. Jolt 는 접촉 검증
 *          (`OnContactValidate`)에서, Box2D 는 사용자 필터 콜백에서 조회한다. 바꾸는 것은 step 밖(게임 스레드)에서만 하고, 읽기는 step 중 여러
 *          스레드가 동시에 한다(잠그지 않는다).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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
        /** @brief 두 바디가 부딪혀도 되는지 반환합니다. 테이블이 비어 있으면 바로 true 입니다. */
        bool canCollide( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB ) const;
        /** @brief 사라진 바디들의 쌍을 지웁니다. */
        void removeBodies( span<const PhysicsBodyHandle> listBody );
        /** @brief 테이블을 비웁니다. */
        void clear() { _mapBodyToDisabled.clear(); }
        /** @brief 쌍이 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _mapBodyToDisabled.empty(); }

    private:
        unordered_map<uint64, vector<uint64>> _mapBodyToDisabled; ///< 바디 → 충돌을 끈 상대들(양쪽에 적는다)
    };
} // namespace sw
