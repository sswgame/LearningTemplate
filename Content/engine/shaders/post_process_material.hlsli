/**
 * post_process_material.hlsli — 후처리 머티리얼(PostProcess 영역) 셰이더 뼈대의 1단: 픽셀 문맥과 화면 입력. 언리얼 Post Process 머티리얼 영역 · 유니티 Full Screen Pass 셰이더의 자리다.
 *
 * 셰이더 파일의 순서(머티리얼 셰이더 뼈대와 같은 계약)
 *   1. `#include "lighting.hlsli"`
 *   2. `SW_MATERIAL_BEGIN { … } SW_MATERIAL_END`   머티리얼 칸(.material 의 프로퍼티와 이름 · 순서가 같다, 원소 크기 16 의 배수)
 *   3. `#include "post_process_material.hlsli"`   이 파일
 *   4. 훅 `float4 computePostProcessMaterial( PixelContext context )` — rgb 결과 색, a 덮임(무게에 곱한다)
 *   5. `#include "post_process_material_main.hlsli"`  풀스크린 진입점
 * 머티리얼 그래프의 `PostProcessOutput` 이 같은 길로 낸다(post_process_material_graph.hlsli).
 *
 * 입력 — 파이프라인의 `PostProcessMaterial` 패스가 선언한 역할만 걸린다(걸리지 않은 역할은 kInvalidIndex 라 0 · +z 노멀로 읽힌다):
 *   context._color         원본 색(SourceColor, 필수) — 자리가 톤맵 앞이면 HDR, 뒤면 LDR, 톤맵 대신이면 HDR 원본
 *   context._worldPosition 장면 깊이(SceneDepth, 선택)에서 복원한 월드 위치
 *   context._normal        G버퍼 노멀(GBufferNormal, 선택 — 디퍼드) · 없으면 +z
 *   PostProcessInput::*    깊이 · 선형 깊이 · G버퍼 알베도 · 머티리얼 칸
 * 머티리얼은 원소 하나짜리 버퍼(g_SwMaterials 0 번)로 걸린다(`FramePostProcessMaterials` — 머티리얼마다 렌더 스레드가 올린다).
 */

#ifndef SW_ENGINE_POSTPROCESSMATERIAL_HLSLI
#define SW_ENGINE_POSTPROCESSMATERIAL_HLSLI

#include "fullscreen.hlsli"

// 머티리얼 그래프 식이 쓰는 탄젠트 — 화면에는 정점 탄젠트가 없다(노멀 맵 노드는 +x 탄젠트로 읽는다).
#define SW_MATERIAL_GRAPH_TANGENT( pixelContext ) float4( 1.0f, 0.0f, 0.0f, 1.0f )

/** @brief 후처리 픽셀 문맥 — 표면 머티리얼의 PixelContext 와 같은 칸 이름이라 그래프 노드(UV · WorldPosition · WorldNormal · VertexColor)가 그대로 읽는다. */
struct PixelContext
{
    float4       _position;      ///< SV_POSITION(화면 픽셀 좌표)
    float4       _color;         ///< 원본 색(SourceColor)
    float2       _uv;            ///< 화면 UV(위가 0)
    float3       _normal;        ///< G버퍼 노멀(없으면 +z)
    float3       _worldPosition; ///< 장면 깊이에서 복원한 월드 위치
    MaterialData _material;      ///< 이 머티리얼의 칸(원소 0)

    static PixelContext make( PSInput input );
};

/** @brief 화면 입력 — 패스가 선언한 역할을 읽는다(그래프 "Post Process" 묶음 노드가 부른다). */
struct PostProcessInput
{
    static float3 sampleSceneColor( float2 uv );
    static float sampleDeviceDepth( float2 uv );
    static float computeLinearDepth( float2 uv ); // 카메라에서의 뷰 깊이(m)
    static float3 sampleGbufferAlbedo( float2 uv );
    static float3 sampleGbufferNormal( float2 uv );
    static float sampleGbufferRoughness( float2 uv );
    static float sampleGbufferMetallic( float2 uv );
};

static PixelContext PixelContext::make( PSInput input )
{
    PixelContext context;
    context._position      = input._position;
    context._uv            = input._uv;
    context._color         = PassTexture::sampleSourcePoint( input._uv );
    context._normal        = PassTexture::sampleNormal( input._uv );
    context._worldPosition = Transform::computeWorldPositionFromDepth( input._uv, PostProcessInput::sampleDeviceDepth( input._uv ) );
    context._material      = SW_MATERIAL( 0u );
    return context;
}

static float3 PostProcessInput::sampleSceneColor( float2 uv )
{
    return PassTexture::sampleSource( uv ).rgb;
}

static float PostProcessInput::sampleDeviceDepth( float2 uv )
{
    return PassTexture::sampleDepthPoint( uv ).r;
}

static float PostProcessInput::computeLinearDepth( float2 uv )
{
    const float3 worldPosition = Transform::computeWorldPositionFromDepth( uv, PostProcessInput::sampleDeviceDepth( uv ) );
    return Transform::computeClipPosition( float4( worldPosition, 1.0f ), g_ViewProj ).w;
}

static float3 PostProcessInput::sampleGbufferAlbedo( float2 uv )
{
    return PassTexture::sampleAlbedo( uv ).rgb;
}

static float3 PostProcessInput::sampleGbufferNormal( float2 uv )
{
    return PassTexture::sampleNormal( uv );
}

static float PostProcessInput::sampleGbufferRoughness( float2 uv )
{
    float roughness;
    float metallic;
    float specular;
    float occlusion;
    PassTexture::loadGbufferMaterial( uv, roughness, metallic, specular, occlusion );
    return roughness;
}

static float PostProcessInput::sampleGbufferMetallic( float2 uv )
{
    float roughness;
    float metallic;
    float specular;
    float occlusion;
    PassTexture::loadGbufferMaterial( uv, roughness, metallic, specular, occlusion );
    return metallic;
}

#endif // SW_ENGINE_POSTPROCESSMATERIAL_HLSLI
