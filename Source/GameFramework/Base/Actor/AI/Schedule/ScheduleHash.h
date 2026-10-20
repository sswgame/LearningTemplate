/**
 * @file ScheduleHash.h
 * @brief 일정 시스템의 해시 도우미입니다 — 칸의 정체 키 · 이름 키 · 키 섞기를 한 곳에 둡니다.
 * @details 계획(`ScheduleSystemPlan`) · 실행(`ScheduleSystemRuntime`) · 저장(`ScheduleSystemSave`) · 디버그가 같은 칸에 같은 키를 얻어야 합니다
 *          (사건 · 경로 캐시 · 계획 비교 · 저장 해시). 파일마다 따로 적으면 한쪽만 바뀌어 키가 갈라지므로 여기서만 정의합니다.
 *          바이트를 섞는 규칙과 상수는 `HashUtil` 입니다. 저장 · 네트워크로 가는 값이라 순서를 바꾸지 않습니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    /**
     * @struct ScheduleHash
     * @brief 일정 시스템이 쓰는 해시 함수 모음입니다(전부 static).
     */
    struct ScheduleHash
    {
        /** @brief 이름의 글자 바이트를 @p hash 에 이어 섞습니다. */
        [[nodiscard]] static uint64 appendName( const uint64 hash, const hashed_string& name ) { return HashUtil::appendBytes64( hash, name.c_str(), name.size() ); }

        /** @brief 자리의 x · y · z 를 @p hash 에 이어 섞습니다. */
        [[nodiscard]] static uint64 appendPosition( uint64 hash, const float3& position )
        {
            hash = HashUtil::appendValue64( hash, position._x );
            hash = HashUtil::appendValue64( hash, position._y );
            return HashUtil::appendValue64( hash, position._z );
        }

        /** @brief 이름 하나의 32 비트 키입니다(64 비트 해시의 아래 32 비트). */
        [[nodiscard]] static uint32 computeNameKey( const hashed_string& name ) { return static_cast<uint32>( appendName( HashUtil::kFnvOffset64, name ) ); }

        /** @brief 두 32 비트 키를 섞습니다(순서가 있다). */
        [[nodiscard]] static uint32 mixKey( const uint32 lhs, const uint32 rhs ) { return GameHash::mix32( lhs * 0x9e3779b1u ^ GameHash::mix32( rhs + 0x7f4a7c15u ) ); }

        /** @brief 칸의 정체 — 같은 출처 · 칸 · 시간 · 자리 · 출발이면 같은 값입니다(사건 · 경로 캐시 · 계획 비교). 0 은 쓰지 않습니다. */
        [[nodiscard]] static uint64 computeSegmentKey( const ScheduleSegment& segment )
        {
            uint64 hash = HashUtil::kFnvOffset64;
            hash        = appendName( hash, segment._sourceID );
            hash        = appendName( hash, segment._activity );
            hash        = HashUtil::appendValue64( hash, segment._startMinute );
            hash        = HashUtil::appendValue64( hash, segment._endMinute );
            hash        = HashUtil::appendValue64( hash, segment._blockIndex );
            hash        = HashUtil::appendValue64( hash, segment._ownerIndex );
            hash        = HashUtil::appendValue64( hash, segment._source );
            hash        = appendPosition( hash, segment._target._position );
            hash        = HashUtil::appendValue64( hash, segment._departMinute );
            hash        = HashUtil::appendValue64( hash, segment._travelMinutes );
            return hash == 0 ? 1 : hash;
        }
    };
} // namespace sw
