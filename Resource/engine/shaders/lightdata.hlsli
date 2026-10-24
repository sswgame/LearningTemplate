/**
 * lightdata.hlsli — 라이트 목록 원소 하나(LightData)의 정의. 그래픽스(lighting.hlsli 의 g_SwLights)와 컴퓨트(lightgrid.hlsl 의 g_LightGridLights)가 이 파일 하나를 쓴다.
 *
 * C++ `GPULight`(Engine/Graphics/Shader/Binding/GPULight.h)와 바이트 배치가 같아야 한다(float4 다섯, 80 바이트). 구조체를 셰이더마다 베끼면 칸을 더할 때 한 곳만 고쳐
 * 원소 1 부터 어긋난다 — 쿠킹한 바이너리의 stride · 오프셋은 ShaderBindingValidatorTest.LightElementLayoutMatchesCPUStruct 가 C++ 구조체와 대조한다.
 */

#ifndef SW_ENGINE_LIGHTDATA_HLSLI
#define SW_ENGINE_LIGHTDATA_HLSLI

struct LightData
{
    float4 _positionRadius; // xyz 월드 위치(점광·스폿), w 반경 — 방향광은 0
    float4 _colorIntensity; // rgb 빛 색, a 세기
    float4 _directionType;  // xyz 빛이 나아가는 방향(방향광·스폿), w 타입 (SW_LIGHT_TYPE_*)
    float4 _params;         // x 그림자를 받는가(0/1), y cos(바깥 원뿔각), z cos(안쪽 원뿔각), w 점광 · 스폿 그림자의 첫 타일 원소(g_SwLightCount 기준 상대 번호)
    float4 _extra;          // 종류마다의 덧칸(C++ GPULight::_extra) — 2D 빛은 x 그림자 원천 반경, y 섞는 방식, z 뒤 원소 수, w 쿠키 아틀라스 칸. 3D 빛은 0

    // 멤버 함수는 원소 레이아웃을 바꾸지 않는다. 정의는 3D 빛이 lighting.hlsli, 2D 빛이 lighting2d.hlsli 다(이 원소만 쓰는 컴퓨트는 부르지 않는다).
    bool computeIncidence( float3 worldPosition, out float3 outToLight, out float outAttenuation );
    float sampleLocalShadow( float3 worldPosition, float3 worldNormal );
    float computeShadow( float3 worldPosition, float3 worldNormal, float directionalShadow );
    float4 sampleProbeVolume( float3 worldPosition, float3 normal );
    float4 sampleReflectionProbe( float3 worldPosition, float3 reflectDirection, float roughness );
    float computeLight2dAttenuation( float2 pixel );
    float computeFreeformLight2dAttenuation( uint lightIndex, uint lightCount, float2 pixel );
    float3 sampleLight2dCookie( float2 pixel );
};

#endif // SW_ENGINE_LIGHTDATA_HLSLI
