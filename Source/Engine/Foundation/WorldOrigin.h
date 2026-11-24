/**
 * @file WorldOrigin.h
 * @brief 월드 원점과, 그 원점 기준 로컬 자리 ↔ 절대 자리 옮기기입니다.
 * @details 오브젝트의 자리는 원점 기준 로컬이고(절대 = 로컬 + 원점), 쿠킹본 · 서버 · 세이브 · 셀 격자 · 내비메시는 절대다(언리얼 World Origin Rebasing 의 `UWorld::OriginLocation`).
 *          두 공간을 넘는 곳은 모두 이 형식을 거친다 — 원점 더하기를 손으로 적지 않는다. 더하기는 배정밀도로 하고 끝에서 한 번만 float 로 자른다
 *          (행렬은 이동 칸만 옮긴다 — 원점을 옮긴 씬의 정점이 옮기지 않은 씬과 같은 값이 나오게).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @struct AbsolutePosition
     * @brief 절대 자리(월드 원점 0 기준)임을 형식으로 드러낸 값입니다 — 씬 자리(`float3`)를 받는 함수에 그대로 넣을 수 없고 `WorldOrigin::toLocal` 을 거친다.
     * @details 디렉터가 뷰 · AI 에 내는 시뮬레이션 자리와 절대 격자(`LandRegistry`)가 받는 자리의 형식이다. 시뮬레이션 안의 계산은 `_value` 로 한다 — 값 하나라 비용이 없다.
     */
    struct AbsolutePosition
    {
        float3 _value{};

        AbsolutePosition() = default;
        /** @brief @p value 가 절대 자리라고 적습니다. 명시 생성자라 형식 이름을 적지 않은 `float3` · 중괄호(집합체 초기화의 중괄호 생략 포함)는 들어오지 못한다. */
        explicit AbsolutePosition( const float3& value )
            : _value{ value }
        {
        }

        AbsolutePosition operator+( const float3& offset ) const { return AbsolutePosition{ _value + offset }; }
    };

    /** @struct WorldOrigin @brief 파일 머리말 참고. 주인은 씬의 오브젝트 매니저 하나다(`GameObjectManager::getWorldOrigin`). */
    struct WorldOrigin
    {
        double3 _position{}; ///< 원점의 절대 자리

        /** @brief 원점이 0 이면(옮긴 적이 없거나 되돌렸다) true 입니다 — 로컬이 곧 절대다. */
        bool isZero() const { return _position._x == 0.0 && _position._y == 0.0 && _position._z == 0.0; }

        /** @brief 로컬 자리를 절대 자리로 옮깁니다. */
        float3 toAbsolute( const float3& local ) const
        {
            return float3{ static_cast<float32>( static_cast<float64>( local._x ) + _position._x ), static_cast<float32>( static_cast<float64>( local._y ) + _position._y ),
                           static_cast<float32>( static_cast<float64>( local._z ) + _position._z ) };
        }
        /** @brief 절대 자리를 로컬 자리로 옮깁니다. */
        float3 toLocal( const float3& absolute ) const
        {
            return float3{ static_cast<float32>( static_cast<float64>( absolute._x ) - _position._x ), static_cast<float32>( static_cast<float64>( absolute._y ) - _position._y ),
                           static_cast<float32>( static_cast<float64>( absolute._z ) - _position._z ) };
        }
        /** @brief 절대 자리를 로컬 자리로 옮깁니다. */
        float3 toLocal( const AbsolutePosition& absolute ) const { return toLocal( absolute._value ); }
        /** @brief 로컬 상자를 절대 상자로 옮깁니다. 유효하지 않은 상자(`AABB::empty`)는 그대로입니다. */
        AABB toAbsolute( const AABB& local ) const { return local.isValid() ? AABB{ toAbsolute( local._min ), toAbsolute( local._max ) } : local; }
        /** @brief 로컬 월드 행렬의 이동을 절대로 옮깁니다. */
        float4x4 toAbsolute( const float4x4& localWorld ) const
        {
            float4x4 absoluteWorld = localWorld;
            absoluteWorld.setTranslation( toAbsolute( localWorld.getTranslation() ) );
            return absoluteWorld;
        }
    };
} // namespace sw
