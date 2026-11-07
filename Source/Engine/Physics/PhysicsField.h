/**
 * @file PhysicsField.h
 * @brief 물리 필드 표본입니다 — 한 바디가 이번 프레임에 받는 환경의 물리(중력 배율 · 가속 · 흐름 · 유체 저항 · 부력 유체 · 마찰 · 잠긴 비율)를 한 값으로 둡니다.
 * @details 무중력 · 저중력 · 바다 · 바람 · 소용돌이 · 빙판이 같은 칸의 다른 값이다. 표본을 만드는 쪽(GameFramework 지역 효과의 `PhysicsField` 모듈)은 바디를 모르고,
 *          받는 쪽(`CharacterControllerComponent` · `RigidBodyComponent`)은 지역을 모른다 — 엔진 층은 값만 안다. 언리얼 `APhysicsVolume`(유체 저항 · 물 · 중력)과
 *          캐릭터 중력 방향, 유니티 `AreaEffector` · `BuoyancyEffector` 의 자리다(기억).
 *          여러 지역이 겹치면 `combine` 이 합친다: 중력 배율은 작은 쪽(무중력이 이긴다), 가속 · 흐름은 합, 저항 · 유체 밀도 · 잠긴 비율은 큰 쪽, 마찰은 작은 쪽(빙판이 이긴다).
 *          받는 쪽은 프레임마다 다시 받아야 이어진다(`applyPhysicsField`) — 지역을 나가면 다음 물리 프레임부터 중립이다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 캐릭터 이동기가 필드로 고른 이동 방식입니다(진단 · 애니메이션이 읽는다). */
    ENUM()
    enum class CharacterMovementMode : uint8
    {
        Walking = 0, ///< 딛고 걷는다
        Falling,     ///< 떠 있고 중력을 받는다
        Swimming,    ///< 유체에 잠겨 부력 · 저항을 받는다(수직 속도가 수면 쪽으로 간다)
        Floating     ///< 중력이 거의 없다(무중력) — 수직 속도는 가속 · 흐름만 바꾼다
    };

    /** @struct PhysicsFieldSample @brief 물리 필드 표본 하나입니다(파일 머리말 참고). 기본값이 중립(아무것도 바꾸지 않음)입니다. */
    struct PhysicsFieldSample
    {
        static constexpr float32 kFloatingGravityScale = 0.05f; ///< 이보다 작은 중력 배율은 무중력으로 본다(이동 방식 Floating)
        static constexpr float32 kSwimmingImmersion    = 0.5f;  ///< 유체에 이만큼 잠기면 헤엄친다
        static constexpr float32 kSwimmingFloatShare   = 0.75f; ///< 뜬 평형 잠김(몸 밀도 / 유체 밀도)의 이만큼이면 헤엄친다 — 진한 유체에서 떠 있는 몸

        float32 _gravityScale{ 1.0f }; ///< 장면 중력에 곱한다(0 = 무중력)
        float3  _acceleration{};       ///< 더하는 가속(m/s², 월드) — 당김 · 바람 · 소용돌이
        float3  _flowVelocity{};       ///< 유체가 흐르는 속도(m/s, 월드) — 저항이 바디 속도를 이 값으로 끈다(해류 · 강물 · 모래 흐름)
        float32 _drag{ 0.0f };         ///< 유체 저항(1/s) — 흐름과의 속도 차를 줄인다
        float32 _fluidDensity{ 0.0f }; ///< 유체 밀도(물 = 1). 0 이면 부력이 없다. 바디는 자기 밀도와 견줘 뜨거나 가라앉는다
        float32 _friction{ 1.0f };     ///< 디딘 면의 접지(1 = 보통, 0 에 가까우면 빙판 — 걷는 속도가 천천히 바뀐다)
        float32 _immersion{ 0.0f };    ///< 유체에 잠긴 비율(0..1)

        /** @brief 다른 지역의 표본을 얹습니다(규칙은 파일 머리말). */
        void combine( const PhysicsFieldSample& other )
        {
            _gravityScale = MathUtil::min( _gravityScale, other._gravityScale );
            _acceleration = _acceleration + other._acceleration;
            _flowVelocity = _flowVelocity + other._flowVelocity;
            _drag         = MathUtil::max( _drag, other._drag );
            _fluidDensity = MathUtil::max( _fluidDensity, other._fluidDensity );
            _friction     = MathUtil::min( _friction, other._friction );
            _immersion    = MathUtil::max( _immersion, other._immersion );
        }

        /** @brief 중립(아무것도 바꾸지 않음)이 아니면 true 입니다 — 받는 쪽이 힘 계산 · 바디 깨우기를 건너뛴다. */
        bool hasEffect() const
        {
            const bool bGravity = _gravityScale != 1.0f;
            const bool bMotion  = _acceleration.getLengthSquared() > 0.0f || _flowVelocity.getLengthSquared() > 0.0f || _drag > 0.0f;
            return bGravity || bMotion || ( _fluidDensity > 0.0f && _immersion > 0.0f ) || _friction < 1.0f;
        }

        /** @brief 부력 가속의 크기입니다(중력 크기 @p gravityLength, 바디 밀도 @p bodyDensity — 물 = 1). 잠긴 부피만큼 유체가 민다. */
        float32 computeBuoyancy( float32 gravityLength, float32 bodyDensity ) const
        {
            if ( _fluidDensity <= 0.0f || _immersion <= 0.0f || bodyDensity <= 0.0f )
                return 0.0f;
            return gravityLength * _immersion * _fluidDensity / bodyDensity;
        }

        /**
         * @brief 이 표본에서 캐릭터(밀도 @p bodyDensity — 물 = 1)가 고를 이동 방식입니다.
         * @details 헤엄 문턱은 `kSwimmingImmersion` 과 뜬 평형 잠김(몸 밀도 / 유체 밀도)의 `kSwimmingFloatShare` 중 작은 쪽이다 — 진한 유체(용암 3)에서는 몸이 1/3 만 잠겨 떠 있어
         *          고정 문턱(절반)에 닿지 못하고 떨어짐으로 남았다.
         */
        CharacterMovementMode selectMovementMode( bool bGrounded, float32 bodyDensity ) const
        {
            const float32 swimImmersion = _fluidDensity > 0.0f ? MathUtil::min( kSwimmingImmersion, kSwimmingFloatShare * bodyDensity / _fluidDensity ) : kSwimmingImmersion;
            if ( _fluidDensity > 0.0f && _immersion >= swimImmersion )
                return CharacterMovementMode::Swimming;
            if ( _gravityScale < kFloatingGravityScale && bGrounded == false )
                return CharacterMovementMode::Floating;
            return bGrounded ? CharacterMovementMode::Walking : CharacterMovementMode::Falling;
        }
    };
} // namespace sw
