/**
 * @file MaterialPreviewShading.h
 * @brief 머티리얼 패널의 구 미리보기 셰이딩입니다 — 머티리얼 값(기본색 · 거칠기 · 금속성 · 방출)으로 구의 한 점 색을 CPU 에서 셈합니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct MaterialProperty;
} // namespace sw

namespace sw::editor
{
    /** @brief 미리보기가 읽는 머티리얼 값입니다. 머티리얼에 없는 값은 기본값입니다. */
    struct MaterialPreviewInputs
    {
        float3  _baseColor{ 1.0f, 1.0f, 1.0f }; ///< 선형 색(sRGB 로 적힌 값은 풀어 둔다)
        float3  _emissive{ 0.0f, 0.0f, 0.0f };  ///< 선형 색
        float32 _roughness{ 0.5f };
        float32 _metallic{ 0.0f };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct MaterialPreviewShading
     * @brief 머티리얼 패널의 구 미리보기를 셈합니다.
     * @details GPU 미리보기 창(로드맵 9 의 다중 월드 툴 창)이 오기 전까지의 자리입니다. 셰이더 코드는 보지 않고 관례 이름의 값만 읽어
     *          빛 하나(왼쪽 위) + 주변광으로 셰이딩합니다 — 셰이더가 값을 다르게 쓰면 미리보기와 화면이 다르다.
     *          값 이름: 기본색은 `color` · `baseColor` · `albedo`(없으면 첫 Color 값), 거칠기 `roughness`, 금속성 `metallic`, 방출 `emissive` · `emission`.
     */
    struct MaterialPreviewShading
    {
        /** @brief 글(`"1 0 0 1"` · `"1,0,0,1"`)에서 실수를 @p count 개까지 읽고 읽은 수를 돌려줍니다. 못 읽은 칸은 0 입니다. */
        static uint32 parseFloats( string_view text, float32* pOut, uint32 count );
        /** @brief 머티리얼 값 목록에서 미리보기 값을 모읍니다(지금 값, 비었으면 기본값). */
        static MaterialPreviewInputs readInputs( const vector<MaterialProperty>& listProperty );
        /**
         * @brief 구 위의 한 점의 화면 색(sRGB, 0..1)입니다.
         * @param nx 구를 정면에서 본 단위 원 안의 가로 자리(오른쪽이 +). 원 밖이면 검정입니다.
         * @param ny 세로 자리(아래가 +)
         */
        static float3 shadePoint( const MaterialPreviewInputs& inputs, float32 nx, float32 ny );
        /** @brief 원 안을 고르게 짚은 점들의 평균 (R - B) 입니다(0..255 — 탐침이 머티리얼 색을 따르는지 본다). */
        static float32 computeMeanRedMinusBlue( const MaterialPreviewInputs& inputs );
    };
} // namespace sw::editor
