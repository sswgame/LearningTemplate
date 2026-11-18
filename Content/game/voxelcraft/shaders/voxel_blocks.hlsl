/**
 * @file voxel_blocks.hlsl
 * @brief VoxelCraft 청크의 블록 셰이더 — forward_lit 와 같은 PBR(정점 색 × 머티리얼 색 × 알베도)에, 합친 면이 아틀라스 타일 하나를 블록마다 되풀이하게 UV 를 푼다.
 * @details 메셔(`VoxelMesher`)는 같은 블록 면을 사각형 하나로 합친다(그리디 메싱). 그래서 정점 UV 는 아틀라스 좌표가 아니라
 *          `타일 칸(열, 행) × kVoxelTileUVStride + 면 안 블록 좌표` 다 — 블록 좌표는 합친 면이면 1 을 넘고, 셰이더가 frac 으로 타일 안 자리를 낸다.
 *          되풀이 경계에서 암시 미분이 튀지 않게 연속인 블록 좌표의 미분으로 읽는다(`MaterialTexture::sampleGrad`).
 *          0fps 의 "Meshing in a Minecraft Game"(그리디 메싱 + 타일 되풀이)와 같은 자리 — 텍스처 배열 대신 아틀라스를 그대로 쓴다.
 *          머티리얼 define: `MATERIAL_BLEND_TRANSLUCENT`(물 · 유리, 머티리얼 alphaMode="Blend" 와 짝).
 */
#include "lighting.hlsli"

// blocks.material 의 프로퍼티와 이름 · 순서 · 타입이 1:1 이다(어긋나면 머티리얼 로드가 오류로 멈춘다).
SW_MATERIAL_BEGIN
{
    float4 _color;
    float4 _atlasGrid; ///< x 열 수, y 행 수, z 타일 한 변 텍셀 — 블록 카탈로그(blocks.xml)의 atlasColumns · atlasRows · tileTexels 와 같다(VoxelGameModeComponent 가 월드당 한 번 견주어 다르면 그리지 않는다)
    float  _roughness;
    float  _metallic;
    uint   _albedoMap;
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::validateStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
}
SW_MATERIAL_END

#if defined( MATERIAL_ALPHA_CUTOFF )
    #error "voxel_blocks.hlsl has no alpha cutoff property (MaterialSurface._alphaCutoff)"
#endif

#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_RIM_LIGHT
#define SW_MATERIAL_DEFAULT_VARYINGS
#include "material_shader.hlsli"

// VoxelMesher.cpp 의 kTileUVStride 와 같다 — 면 안 블록 좌표(최대 청크 높이 64)보다 커야 칸과 좌표가 섞이지 않는다.
static const float kVoxelTileUVStride = 128.0f;

float4 sampleVoxelTile( PixelContext context )
{
    const float2 grid    = context._material._atlasGrid.xy;
    const float2 tile    = floor( context._uv / kVoxelTileUVStride );
    const float2 local   = context._uv - tile * kVoxelTileUVStride;
    const float  inset   = 0.5f / context._material._atlasGrid.z; // 반 텍셀 안쪽으로 — 이웃 타일 색이 번지지 않게
    const float2 inTile  = clamp( frac( local ), inset, 1.0f - inset );
    const float2 atlasUV = ( tile + inTile ) / grid;
    // 칸은 삼각형 안에서 같으므로 UV 의 미분이 곧 블록 좌표의 미분이다.
    return MaterialTexture::sampleGrad( context._material._albedoMap, atlasUV, ddx( context._uv ) / grid, ddy( context._uv ) / grid );
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    MaterialSurface surface;
    surface._albedo    = context._color * context._material._color * sampleVoxelTile( context );
    surface._normal    = context._normal;
    surface._roughness = context._material._roughness;
    surface._metallic  = context._material._metallic;
    return surface;
}

#include "material_shader_main.hlsli"
