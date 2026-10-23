/**
 * @file cctvmonitor.hlsl
 * @brief Shooter3D 의 CCTV 모니터 화면 — 렌더 텍스처를 빛 없이(스스로 빛나는 화면) 내고 가로 주사선을 겹친다.
 * @details 게임 도메인의 첫 머티리얼 셰이더다(계획 MaterialShaderHooks G1). 엔진 셰이더와 같은 공개 계약(materialshader.hlsli 머리말)을 쓴다 —
 *          include 는 engine · common 헤더뿐이고, 이 파일은 표면 훅 하나만 정의한다. 나머지 네 훅은 기본을 명시해 고른다
 *          (조명 없음 · 사용자 보간 칸 없음, 월드 위치 오프셋 · 외곽선은 그 퍼뮤테이션이 없어 부르지 않는다).
 *          주사선의 줄 수와 세기는 머티리얼(cctvmonitor.material)이 정한다. 셰이더는 형식만 가진다.
 */
#include "lighting.hlsli"

// 보기 모드 Roughness · Metallic 이 보이는 값 — 화면은 PBR 칸이 없는 거친 유전체로 본다(sprite2d 와 같은 선택).
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

// cctvmonitor.material 의 프로퍼티와 이름 · 순서 · 타입이 1:1 이다(어긋나면 머티리얼 로드가 오류로 멈춘다).
SW_MATERIAL_BEGIN
{
    float4 color;
    uint   albedoMap;
    float  scanlineCount;    ///< 화면 높이(UV 0 ~ 1) 안의 주사선 줄 수
    float  scanlineStrength; ///< 0 이면 주사선 없음, 1 이면 줄 사이가 검다
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "cctvmonitor.hlsl has no alpha cutoff property (SwMaterialSurface.alphaCutoff)"
#endif

#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "materialshader.hlsli"

SwMaterialSurface computeMaterialSurface( SwPixelContext context )
{
    const float4 screenTexture = swSampleMaterialTexture( context.material.albedoMap, context.uv );
    // 주사선 — UV 세로를 따라 cos 한 주기가 한 줄이다. 밝은 줄 가운데는 1, 줄 사이는 1 - 세기.
    const float scanlineWave = 0.5f + 0.5f * cos( context.uv.y * context.material.scanlineCount * 2.0f * kPi );
    const float scanline     = lerp( 1.0f, scanlineWave, saturate( context.material.scanlineStrength ) );

    SwMaterialSurface surface;
    surface.albedo    = float4( context.color.rgb * context.material.color.rgb * screenTexture.rgb * scanline, 1.0f );
    surface.normal    = context.normal;
    surface.roughness = kSurfaceRoughness;
    surface.metallic  = kSurfaceMetallic;
    return surface;
}

#include "materialshadermain.hlsli"
