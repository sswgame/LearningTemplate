/**
 * gbuffer.hlsl — 머티리얼 셰이더가 없는 배치의 G버퍼 패스. 정점 색이 알베도, 거친 유전체다.
 *
 * 머티리얼 셰이더 뼈대(materialshader.hlsli · materialshadermain.hlsli)의 **머티리얼 없는 사용자**(SW_MATERIAL_NONE)다 — 머티리얼 구조체가 없고 표면 훅만 정의한다.
 * 정점 로드(VAT · 모프 · 스킨) · 위치 · 노멀 · G버퍼 서명 · 보기 모드는 뼈대가 한다(머티리얼 셰이더의 G버퍼 패스와 같은 코드다).
 */
#include "lighting.hlsli"

// 머티리얼 셰이더가 없는 배치의 표면 — 거친 유전체다(terrain · foliage 와 같은 값).
static const float kSurfaceRoughness = 1.0f;
static const float kSurfaceMetallic  = 0.0f;

// 훅 선택(materialshader.hlsli) — 머티리얼 없음. G버퍼 패스는 조명을 계산하지 않으므로(디퍼드 조명 패스가 한다) 조명 훅은 "조명 없음"을 고른다.
#define SW_MATERIAL_NONE
#define SW_MATERIAL_UNLIT
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "materialshader.hlsli"

MaterialSurface computeMaterialSurface( PixelContext context )
{
    MaterialSurface surface;
    surface._albedo    = context._color;
    surface._normal    = context._normal;
    surface._roughness = kSurfaceRoughness;
    surface._metallic  = kSurfaceMetallic;
    return surface;
}

#include "materialshadermain.hlsli"
