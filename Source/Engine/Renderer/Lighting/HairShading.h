/**
 * @file HairShading.h
 * @brief 머리카락 셰이딩 모델(Marschner 근사: R · TT · TRT + 다중 산란 확산)의 C++ 사본입니다. 셰이더 `Content/engine/shaders/hair_shading.hlsli` 와 같은 식입니다.
 * @details HLSL 을 C++ 로 컴파일할 수 없어 둔 사본이다 — 단위 테스트(TestHairShading)가 갈래 에너지 · 큐티클 이동 · 탄젠트 이동을 CPU 에서 본다.
 *          식을 바꾸면 두 파일을 함께 고친다(형식으로 막지 못하는 틈). 고른 까닭과 언리얼 대응은 셰이더 머리말에 있다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct HairSurface
     * @brief 머리카락 셰이딩의 표면 값과 그 값으로 빛 하나를 받는 식입니다(셰이더 `HairSurface` 와 같은 칸 · 이름). 방향은 모두 단위 벡터입니다.
     */
    struct SW_API HairSurface
    {
        /** @brief 머리카락 큐티클(굴절률 1.55)의 수직 입사 반사율입니다. */
        static constexpr float32 kF0 = 0.04652f;
        /** @brief 거칠기 하한입니다(가우스 폭 0 의 나눗셈을 막는다). */
        static constexpr float32 kMinRoughness = 0.05f;

        float3  _baseColor{ 1.0f, 1.0f, 1.0f }; ///< 흡수 색(TT · TRT 가 이 색으로 물든다)
        float32 _roughness{ 0.3f };             ///< 세로 거칠기 0..1
        float32 _shift{ 0.035f };               ///< 큐티클 기울기(라디안)
        float32 _specular{ 1.0f };              ///< R 갈래 배율
        float32 _scatter{ 1.0f };               ///< 다중 산란 확산의 세기
        float3  _tangent{ 0.0f, 1.0f, 0.0f };   ///< 가닥 방향(단위)

        /** @brief 세로 갈래(넓이 1 인 가우스)입니다. */
        static float32 computeLongitudinal( float32 width, float32 offset );
        /** @brief 슐릭 프레넬(머리카락 F0)입니다. */
        static float32 computeFresnel( float32 cosine );
        /** @brief 탄젠트를 노멀 쪽으로 @p shift 만큼 기울인 단위 벡터입니다(Kajiya-Kay 이동 탄젠트). */
        static float3 shiftTangent( const float3& tangent, const float3& normal, float32 shift );

        /** @brief R · TT · TRT 의 합입니다(빛 하나, 빛 색 · 그림자를 곱하기 전). */
        float3 evaluateLobes( const float3& toView, const float3& toLight ) const;
        /** @brief R 갈래만입니다(테스트가 하이라이트 자리를 본다). */
        float32 evaluateReflection( const float3& toView, const float3& toLight ) const;
        /** @brief 다중 산란 확산 근사입니다. */
        float3 evaluateScatter( const float3& normal, const float3& toView, const float3& toLight ) const;
    };
} // namespace sw
