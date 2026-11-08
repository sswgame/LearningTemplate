/**
 * appearance_eye.hlsli — 외형 머티리얼(appearance_lit.hlsl)의 눈(MATERIAL_EYE): 공막 · 각막 아래 홍채(굴절 시차) · 윤부 고리 · 동공(모양 · 확장) · 각막 젖음 · 홍채 집광 · 반사판(눈빛) · 아래 눈꺼풀 눈물 띠.
 *
 * 메시: 오브젝트 원점을 중심으로 한 구(엔진 내장 Sphere 그대로)이고 눈은 오브젝트 +Z 를 본다. 반지름은 상관없다(방향만 쓴다). 각막은 화면 공간이 아니라
 * 구 위의 점에서 굴절한 시선을 홍채 평면까지 따라가 홍채 좌표를 구한다 — 언리얼 Eye 셰이딩 모델 예제의 EyeRefraction 과 같은 생각(기억에 의존).
 * 홍채 좌표는 홍채 반지름이 1 이다. `_albedoMap` 을 주면 홍채 좌표(0..1)로 읽는다 — 홍채 무늬 텍스처.
 *
 * 머티리얼 칸(눈에서의 뜻): `_color` 공막 색 · `_layerColor` 홍채 색 · `_secondaryColor` 윤부 고리 색 · `_glowColor` 반사판 색(HDR, 0 이면 없음) ·
 * `_subsurfaceColor` · `_subsurfaceRadius` 공막의 얕은 산란 · `_coatWeight` · `_coatRoughness` 각막 젖음(물고기는 0) · `_ior` 각막 굴절률 · `_corneaHeight` 각막 꼭대기에서
 * 홍채 평면까지(구 반지름 비) · `_irisRadius` 홍채 반지름(구 반지름 비) · `_limbalWidth` 윤부 고리 폭(홍채 반지름 비) · `_pupilShape` 모양(shading_math.hlsli kPupilShape*) ·
 * `_pupilMinRadius` · `_pupilMaxRadius` 확장 0 · 1 의 동공 크기(홍채 반지름 비, 틈 모양은 긴 축) · `_patternAmount` 홍채 집광 세기 · `_wetMeniscus` 눈물 띠 세기 ·
 * `_tearLineHeight` 눈물 띠 높이(오브젝트 Y, 구 반지름 비 — 아래 눈꺼풀이 닿는 자리).
 * 인스턴스 값(런타임 — 배치를 가르지 않는다, MeshComponent `_customData0` · `_customData1`):
 *   `_customData0.x` 동공 확장 0..1 — 매 프레임 W16 의 환경 밝기 구동기가 쓴다(ShadingMath::computeDilationFromLuminance 가 밝기 → 확장 식). 에셋에 굽지 않는다.
 *   `_customData1.rgb` 이 눈의 홍채 색, `.a` 그 색의 무게(0 이면 머티리얼 색 — 짝짝이 눈은 한쪽만 1).
 */

#ifndef SW_ENGINE_APPEARANCEEYE_HLSLI
#define SW_ENGINE_APPEARANCEEYE_HLSLI

static const float kEyePupilAlbedo = 0.01f; // 동공은 거의 빛을 돌려보내지 않는다(망막의 흡수)

struct AppearanceEye
{
    static void apply( inout AppearanceSurface surface, PixelContext context );
};

#if defined( MATERIAL_EYE )
static void AppearanceEye::apply( inout AppearanceSurface surface, PixelContext context )
{
    const MaterialData material   = context._material;
    const float3       axisX      = normalize( context._user._axisX );
    const float3       axisY      = normalize( context._user._axisY );
    const float3       axisZ      = normalize( context._user._axisZ );
    const float3       direction  = normalize( context._user._localPosition );
    const float3       toView     = View::computeToView( context._worldPosition );
    const float3       viewLocal  = float3( dot( toView, axisX ), dot( toView, axisY ), dot( toView, axisZ ) );
    const float        irisRadius = max( material._irisRadius, 0.05f );

    // 각막 아래: 구 위의 점에서 굴절한 시선을 홍채 평면(z = 1 - 각막 높이, 윤부 평면보다 얕지 않다)까지 따라간다.
    const float  limbusZ       = sqrt( saturate( 1.0f - irisRadius * irisRadius ) );
    const float  planeZ        = min( 1.0f - max( material._corneaHeight, 0.0f ), limbusZ );
    const float3 refracted     = refract( -viewLocal, direction, 1.0f / max( material._ior, 1.0f ) );
    const float  travel        = ( planeZ - direction.z ) / min( refracted.z, -1e-3f );
    const float2 throughCornea = direction.xy + refracted.xy * max( travel, 0.0f );
    const float  radial        = length( direction.xy ) / irisRadius;
    const float  corneaMask    = step( 0.0f, direction.z ) * ( 1.0f - smoothstep( 1.0f, 1.0f + max( material._limbalWidth, 0.01f ), radial ) );
    const float2 iris          = lerp( direction.xy, throughCornea, corneaMask ) / irisRadius;
    const float  irisRadial    = length( iris );
    const float  edgeWidth     = max( fwidth( irisRadial ), 1e-3f );
    const float  irisMask      = corneaMask * ( 1.0f - smoothstep( 1.0f - edgeWidth, 1.0f + edgeWidth, irisRadial ) );

    // 동공 — 확장은 인스턴스 값(매 프레임 바뀐다), 모양 · 크기는 머티리얼 칸.
    const float dilation    = saturate( context._user._instanceData0.x );
    const float pupilSigned = ShadingMath::computePupilDistance( material._pupilShape, iris.x, iris.y, material._pupilMinRadius, material._pupilMaxRadius, dilation );
    const float pupilEdge   = max( fwidth( pupilSigned ), 1e-3f );
    const float pupilMask   = irisMask * ( 1.0f - smoothstep( -pupilEdge, pupilEdge, pupilSigned ) );

    // 홍채 — 바큇살 섬유(각도 방향 잡음) · 동공 둘레 밝은 띠 · 윤부 고리. 짝짝이는 인스턴스 색.
    const float  angle       = atan2( iris.y, iris.x );
    const float  fibers      = AppearanceNoise::computeFractal( float2( angle * 9.0f, irisRadial * 2.0f ) );
    const float  collarette  = 1.0f - smoothstep( 0.0f, 0.25f, abs( irisRadial - ( material._pupilMaxRadius + 0.1f ) ) );
    const float3 irisBase    = lerp( material._layerColor.rgb, context._user._instanceData1.rgb, saturate( context._user._instanceData1.a ) );
    const float3 irisTexture = MaterialTexture::sampleIndex( material._albedoMap, iris * 0.5f + 0.5f ).rgb;
    const float  limbal      = smoothstep( 1.0f - max( material._limbalWidth, 0.01f ) * 2.0f, 1.0f, irisRadial );
    float3       irisColor   = irisBase * irisTexture * ( 0.55f + 0.7f * fibers + 0.35f * collarette );
    irisColor                = lerp( irisColor, material._secondaryColor.rgb, limbal );

    // 공막은 surface._albedo(머티리얼 색 × 정점 색)다 — 홍채 텍스처를 공막에 쓰지 않게 바탕 텍스처 곱은 make 에서 빠진다(아래 AppearanceSurface::make).
    surface._albedo.rgb = lerp( surface._albedo.rgb, irisColor, irisMask );
    surface._albedo.rgb = lerp( surface._albedo.rgb, float3( kEyePupilAlbedo, kEyePupilAlbedo, kEyePupilAlbedo ), pupilMask );
    // 홍채는 산란하지 않는다(공막만) — 각막 쪽 감쌈을 끈다.
    surface._subsurfaceWrap *= 1.0f - irisMask;

    // 아래 눈꺼풀의 눈물 띠 — 앞쪽 반구의 얇은 띠가 하이라이트를 잡는다.
    const float tearBand   = step( 0.0f, direction.z ) * ( 1.0f - smoothstep( 0.0f, 0.04f, abs( direction.y - material._tearLineHeight ) ) ) * saturate( material._wetMeniscus );
    surface._coatWeight    = max( surface._coatWeight, tearBand );
    surface._coatRoughness = lerp( surface._coatRoughness, 0.02f, tearBand );

    // 홍채 집광 — 빛 반대쪽 홍채 가장자리에 각막이 모은 빛(키 라이트만, 값싼 판).
    const float3 toKeyLight    = normalize( -g_KeyLightDirIntensity.xyz );
    const float3 keyLocal      = float3( dot( toKeyLight, axisX ), dot( toKeyLight, axisY ), dot( toKeyLight, axisZ ) );
    const float  causticFacing = saturate( dot( normalize( float3( -iris, 0.6f ) ), keyLocal ) );
    const float  caustic       = causticFacing * causticFacing * causticFacing * causticFacing * material._patternAmount * irisMask * ( 1.0f - pupilMask );
    // 반사판(눈빛) — 빛이 시선 쪽에서 올 때 동공이 되비친다(되반사). 반사판이 없는 눈은 _glowColor 가 0 이다.
    const float retro        = saturate( dot( toView, toKeyLight ) );
    const float retroSquared = retro * retro;
    const float eyeShine     = retroSquared * retroSquared * retroSquared * retroSquared * pupilMask;
    surface._emissive += ( irisColor * caustic + material._glowColor.rgb * eyeShine ) * g_KeyLightColor.rgb * g_KeyLightDirIntensity.w;
}
#else
static void AppearanceEye::apply( inout AppearanceSurface surface, PixelContext context )
{
}
#endif

#endif // SW_ENGINE_APPEARANCEEYE_HLSLI
