/**
 * @file gimmick_surface.hlsl
 * @brief 장치 · 기믹 표면의 마스터 셰이더 — 상호작용 강조, 네온, 켜진 화면, 용암, 얼음(불투명), 홀로그램 · 역장(반투명), 녹아 사라짐 · 나타남(알파 컷오프).
 * @details 효과 종류(`_effect`)는 머티리얼 값 분기다(environment_surface.hlsl 의 무늬와 같은 까닭 — 프리셋마다 셰이더가 늘지 않는다). 렌더 상태가 다른 셋은 부모가 셋이다:
 *          gimmicksurface.material(불투명) · gimmicksurface_translucent.material(반투명, MATERIAL_BLEND_TRANSLUCENT) · gimmicksurface_dissolve.material(alphaMode Mask).
 *          자체 발광은 `SW_MATERIAL_EMISSIVE` 훅으로 더한다 — G버퍼에 칸이 없어 세 부모 모두 MATERIAL_FORWARD_ONLY 를 적는다(툰 · 그래프 발광과 같은 길).
 *          표면 훅과 발광 훅이 같은 평가(`evaluateGimmick`)를 부른다(material_graph_main.hlsli 와 같은 모양 — 인라인 뒤 컴파일러가 겹친 식을 합친다).
 *          외곽선은 이 셰이더가 아니라 하이라이트 컴포넌트의 외곽선 패스(post_outline · highlight_outline)가 그린다 — 강조 프리셋은 그 위의 테두리 맥동만 낸다
 *          (언리얼은 CustomDepth 외곽선 + 머티리얼 프레넬을 함께 쓰는 것과 같다).
 *          시간은 `g_SwMaterialTime`(머티리얼 시계, 초)이다.
 */
#include "lighting.hlsli"

// 효과 종류(`_effect`) — gimmicksurface.material 의 표와 같다.
static const float kEffectEmissive  = 0.0f;
static const float kEffectHighlight = 1.0f;
static const float kEffectDissolve  = 2.0f;
static const float kEffectHologram  = 3.0f;
static const float kEffectForce     = 4.0f;
static const float kEffectLava      = 5.0f;
static const float kEffectIce       = 6.0f;
static const float kEffectScreen    = 7.0f;
// 무늬 해시 씨앗 · 얼음 반짝이 칸 밀도(칸/m)와 비율.
static const uint  kGimmickSeed       = 13u;
static const float kIceSparkleDensity = 80.0f;
static const float kIceSparkle        = 0.03f;

// gimmicksurface.material 의 프로퍼티와 이름 · 타입이 같다.
SW_MATERIAL_BEGIN
{
    float4 _baseColor;     // 밑 색, a 는 반투명 부모의 알파
    float4 _emissiveColor; // 발광 색(HDR)
    float4 _patternScale;  // 효과마다의 무늬: x = 크기(m) 또는 밀도, y = 둘째 크기, z = 경계 폭, w = 0(예비)
    float  _effect;        // 0 발광 · 1 강조 · 2 녹아 사라짐 · 3 홀로그램 · 4 역장 · 5 용암 · 6 얼음 · 7 화면
    float  _emissiveStrength;
    float  _roughness;
    float  _metallic;
    float  _fresnelPower;
    float  _pulseSpeed;  // 강조 맥동 속도(라디안/초)
    float  _flowSpeed;   // 흐름 · 주사선 속도
    float  _dissolve;    // 녹아 사라짐 문턱(0 = 다 보인다, 1 = 다 사라졌다)
    float  _alphaCutoff; // MATERIAL_ALPHA_CUTOFF(녹아 사라짐 부모)일 때 이 값보다 작은 알파를 버린다
    // 원소 크기를 16 의 배수로 — DX 의 촘촘한 간격과 GL std430 배열 간격이 같아야 한다(ShaderBindingValidator::checkStructuredElementLayouts). 머티리얼 속성이 아니다
    uint _swPad0;
    uint _swPad1;
    uint _swPad2;
}
SW_MATERIAL_END

#define SW_MATERIAL_DEFAULT_LIGHTING
#define SW_MATERIAL_DEFAULT_VARYINGS
#define SW_MATERIAL_EMISSIVE
#include "material_shader.hlsli"
#include "material_functions.hlsli"

/** @brief 기믹 한 점의 결과 — 표면 값과 자체 발광. */
struct GimmickResult
{
    float3 _albedo;
    float  _alpha;
    float  _roughness;
    float  _metallic;
    float3 _normal;
    float3 _emissive;
};

/** @brief 효과 종류 @p kind 가 @p effect 인지(값 분기 — 머티리얼 값은 실수다). */
bool isEffect( float kind, float effect )
{
    return abs( kind - effect ) < 0.5f;
}

/** @brief 효과 하나를 평가한다. 표면 훅과 발광 훅이 함께 부른다. 이른 반환 없이 출구가 하나다(material_shader.hlsli 규칙). */
GimmickResult evaluateGimmick( PixelContext context )
{
    MaterialData material = context._material;
    const float3 position = context._worldPosition;
    const float3 normal   = context._normal;
    const float3 toView   = View::computeToView( position );
    const float  time     = g_SwMaterialTime;
    const float  fresnel  = SurfaceFunction::computeFresnel( normal, toView, material._fresnelPower );
    const float  kind     = material._effect;
    const float4 scale    = material._patternScale;
    const float3 glow     = material._emissiveColor.rgb * material._emissiveStrength;

    GimmickResult result;
    result._albedo    = material._baseColor.rgb;
    result._alpha     = 1.0f;
    result._roughness = material._roughness;
    result._metallic  = material._metallic;
    result._normal    = normal;
    result._emissive  = float3( 0.0f, 0.0f, 0.0f );

    if ( isEffect( kind, kEffectEmissive ) )
    {
        result._emissive = glow;
    }
    else if ( isEffect( kind, kEffectHighlight ) )
    {
        const float pulse = 0.6f + 0.4f * sin( time * material._pulseSpeed );
        result._emissive  = glow * fresnel * pulse;
    }
    else if ( isEffect( kind, kEffectDissolve ) )
    {
        // 노이즈가 문턱보다 낮은 곳은 알파 0.5 아래라 컷오프가 오리고, 문턱 바로 위 띠(폭 z)가 탄다.
        const float noiseValue = SurfaceFunction::computeFbm( position / max( scale.x, 1.0e-4f ), 4u );
        const float margin     = noiseValue - material._dissolve;
        result._alpha          = saturate( margin * 8.0f + 0.5f );
        result._emissive       = glow * ( 1.0f - smoothstep( 0.0f, max( scale.z, 1.0e-4f ), margin ) );
    }
    else if ( isEffect( kind, kEffectHologram ) )
    {
        const float scan    = SurfaceFunction::computeScanlines( position, scale.x, material._flowSpeed, time );
        const float flicker = 0.85f + 0.15f * SurfaceFunction::computeValueNoise( float3( time * 12.0f, 0.0f, 0.0f ) );
        result._albedo      = material._baseColor.rgb * 0.2f;
        result._alpha       = saturate( material._baseColor.a * ( 0.4f + scan + fresnel ) * flicker );
        result._emissive    = glow * ( 0.3f + scan * 0.7f + fresnel ) * flicker;
    }
    else if ( isEffect( kind, kEffectForce ) )
    {
        const float2 hexUv  = SurfaceFunction::projectDominantAxis( position, normal ) / max( scale.x, 1.0e-4f );
        const float  edge   = 1.0f - smoothstep( 0.0f, max( scale.z, 1.0e-4f ), SurfaceFunction::computeHexEdge( hexUv ) );
        const float  ripple = 0.5f + 0.5f * sin( position.y * 8.0f - time * material._flowSpeed * 6.0f );
        result._albedo      = material._baseColor.rgb * 0.2f;
        result._alpha       = saturate( material._baseColor.a + fresnel + edge * 0.4f * ripple );
        result._emissive    = glow * ( fresnel + edge * ( 0.3f + 0.7f * ripple ) );
    }
    else if ( isEffect( kind, kEffectLava ) )
    {
        // 식은 껍질 판(보로노이 칸)이 천천히 흐르고 판 사이 틈이 빛난다. 틈 밝기는 느린 노이즈로 숨 쉰다.
        const float2 flowUv = SurfaceFunction::projectDominantAxis( position, normal ) / max( scale.x, 1.0e-4f ) + float2( time * material._flowSpeed, time * material._flowSpeed * 0.37f );
        const float2 cells  = SurfaceFunction::computeCellDistance( flowUv, kGimmickSeed );
        const float  crack  = 1.0f - smoothstep( 0.0f, max( scale.z, 1.0e-4f ), cells.y );
        const float  heat   = 0.6f + 0.4f * SurfaceFunction::computeValueNoise( float3( flowUv * 0.7f, time * 0.3f ) );
        result._albedo      = lerp( material._baseColor.rgb, material._emissiveColor.rgb * 0.2f, crack );
        result._roughness   = lerp( material._roughness, 0.4f, crack );
        result._emissive    = glow * crack * heat;
    }
    else if ( isEffect( kind, kEffectIce ) )
    {
        // 안쪽 균열(칸 무늬)은 밝은 흰 선, 가장자리는 프레넬로 차가운 빛, 표면은 작은 거울 칸이 반짝인다.
        const MaterialPattern cracks = SurfaceFunction::computeCellPattern( SurfaceFunction::projectDominantAxis( position, normal ), scale.x, scale.z, kGimmickSeed );
        result._albedo               = lerp( material._baseColor.rgb, float3( 0.95f, 0.98f, 1.0f ), cracks._groove * 0.7f );
        result._roughness            = lerp( material._roughness, 0.3f, cracks._groove );
        SurfaceFunction::applySparkle( result._roughness, result._normal, position, kIceSparkleDensity, kIceSparkle );
        result._emissive = glow * fresnel;
    }
    else if ( isEffect( kind, kEffectScreen ) )
    {
        // 화소 격자(가로 줄)와 흐르는 주사선. 메시 UV 기준이다(화면 판의 UV 0..1).
        const float pixelRows = SurfaceFunction::computeStripes( float2( context._uv.y, 0.0f ), scale.y, 0.0f );
        const float scan      = SurfaceFunction::computeScanlines( float3( 0.0f, context._uv.y, 0.0f ), 1.0f, material._flowSpeed, time );
        result._emissive      = glow * ( 0.7f + 0.3f * pixelRows ) * ( 0.85f + 0.3f * scan );
    }
    return result;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    const GimmickResult gimmick = evaluateGimmick( context );
    MaterialSurface     surface;
    surface._albedo    = float4( gimmick._albedo, gimmick._alpha );
    surface._normal    = gimmick._normal;
    surface._roughness = saturate( gimmick._roughness );
    surface._metallic  = saturate( gimmick._metallic );
    surface.setAlphaCutoff( context._material._alphaCutoff ); // 녹아 사라짐 부모(알파 컷오프)만 싣는다 — 깊이 · 그림자 패스가 같은 값으로 오린다
    return surface;
}

float3 computeMaterialEmissive( PixelContext context )
{
    return evaluateGimmick( context )._emissive;
}

#include "material_shader_main.hlsli"
