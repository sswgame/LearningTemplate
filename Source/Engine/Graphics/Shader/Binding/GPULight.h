/**
 * @file GPULight.h
 * @brief 라이트 하나를 셰이더 레이아웃 그대로 묶은 80바이트입니다(`lighting.hlsli` 의 `SwLightData`).
 * @details 라이트 컴포넌트(Object 층)가 자기 슬롯을 직접 채우고(`LightComponent::writeGPULight`), 렌더러가 모아 구조 버퍼로 업로드합니다
 *          (`GPULightBuffer`). 컴포넌트 층이 렌더러를 include 할 수 없어 이 구조체만 렌더러 아래의 계약 폴더에 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct GPULight
     * @brief 라이트 하나입니다. 셰이더의 `SwLightData`(lighting.hlsli)와 레이아웃이 같아야 합니다.
     * @details **`float4` 다섯입니다.** 원소를 `float3` 과 섞으면 std430 은 vec4 를 16 바이트 경계에
     *          맞추는 반면 DX · Vulkan 은 DXC 가 명시 오프셋을 적어 넘어가, **OpenGL 에서만** 값이
     *          어긋납니다. 증상이 "빛 하나가 조용히 엉뚱한 위치에 있다"라 찾기 어렵습니다.
     *          `float4` 는 그 자체가 16 바이트 정렬 단위라 이 함정이 없습니다.
     */
    struct GPULight
    {
        float4 _positionRadius{}; ///< xyz 월드 위치, w 반경 (점광과 스폿광. 방향광은 0)
        float4 _colorIntensity{}; ///< rgb 빛 색, a 세기
        float4 _directionType{};  ///< xyz 빛이 나아가는 방향(방향광과 스폿광), w 타입 (shaderslot::kLightType*)
        float4 _params{};         ///< x 그림자 플래그(그림자 아틀라스 슬롯을 받은 빛이면 1), y 스폿 바깥 원뿔 cos, z 스폿 안쪽 원뿔 cos, w 점광과 스폿의 첫 그림자 타일 원소(빛 수 기준 상대 번호)
        /**
         * @brief 종류마다의 덧칸입니다. 3D 빛은 0 입니다.
         * @details 2D 빛(점 · 자유 모양)은 x 그림자 원천 반경(m, 0 이면 딱딱한 그림자), y 섞는 방식(`shaderslot::kLight2DBlend*`), z 자유 모양의 변 수(바로 뒤 원소 수)입니다.
         *          셰이더는 구조 버퍼의 멤버를 따로 읽으므로 이 칸을 읽지 않는 3D 조명 루프에는 대역폭이 들지 않는다.
         */
        float4 _extra{};
    };

    static_assert( sizeof( GPULight ) == 80, "GPULight 는 float4 다섯(80바이트)이어야 한다 — 셰이더 SwLightData(lightdata.hlsli) 와 같은 크기 — 쿠킹한 stride 는 ShaderBindingValidatorTest.LightElementLayoutMatchesCPUStruct 가 본다" );
} // namespace sw
