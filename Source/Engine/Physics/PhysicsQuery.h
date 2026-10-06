/**
 * @file PhysicsQuery.h
 * @brief 레이캐스트 · 셰이프 캐스트 · 겹침 질의의 거름 조건과 결과입니다. 2D · 3D 가 같은 템플릿을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /**
     * @brief 질의가 무엇을 볼지입니다. 레이어는 비트 마스크(비트 n = 레이어 n)이고, 기본은 모든 레이어 · 트리거 제외입니다.
     * @details 레이어 표(`PhysicsSettings`)는 바디끼리의 충돌만 정합니다 — 질의는 이 마스크만 봅니다(유니티 `layerMask` 와 같다).
     */
    struct PhysicsQueryFilter
    {
        PhysicsBodyHandle _ignoreBody{};        ///< 이 바디는 보지 않는다(쏘는 쪽 자신)
        uint64            _ignoreUserData{ 0 }; ///< 0 이 아니면 사용자 값이 이것인 바디는 모두 보지 않는다(쏘는 오브젝트의 래그돌 뼈 · 무기 전부)
        uint32            _layerMask{ MathUtil::kMaxUInt32 };
        bool              _bIncludeTriggers{ false };

        /** @brief 레이어가 마스크에 드는지입니다. */
        bool acceptsLayer( uint8 layer ) const { return layer < 32 && ( _layerMask & ( 1u << layer ) ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 레이 · 셰이프 캐스트가 처음 닿은 곳입니다. */
    template <typename TDimension>
    struct PhysicsCastHit
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle _body{};
        uint64            _userData{ 0 };
        Vector            _point{};          ///< 닿은 점(월드)
        Vector            _normal{};         ///< 닿은 면의 법선(월드, 쏜 쪽을 향한다)
        float32           _fraction{ 0.0f }; ///< 쏜 길이에 대한 비(0..1)
        float32           _distance{ 0.0f }; ///< 출발점에서 닿은 곳까지(미터)
        hashed_string     _material{};       ///< 닿은 셰이프의 물리 재질 이름(발소리 · 탄흔 — 레이캐스트만 채운다)
    };

    using PhysicsCastHit3D = PhysicsCastHit<PhysicsDimension3D>;
    using PhysicsCastHit2D = PhysicsCastHit<PhysicsDimension2D>;
} // namespace sw
