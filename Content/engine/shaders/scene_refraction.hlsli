/**
 * scene_refraction.hlsli — 반투명이 장면 색 사본(SceneColorCopy)을 비껴 읽는 굴절 한 벌. 물(water.hlsl)과 유리(appearance_lit.hlsl 의 MATERIAL_REFRACTION)가 함께 쓴다.
 *
 * 사본은 반투명 전체 앞에 떠 둔 것이라(파이프라인의 SceneColorCopy 패스) 굴절 면보다 먼저 그려진 다른 반투명은 그 픽셀에서 지워진다 — 굴절 머티리얼은
 * renderQueue 를 얇은 반투명보다 작게 두어 먼저 그린다(라이브러리 규칙, MaterialLibraryTest 가 본다). 비껴 읽은 자리가 굴절 면보다 앞의 물체면 그 색을
 * 끌어오지 않는다(장면 깊이 사본으로 가린다). 언리얼의 Refraction(Pixel Normal Offset)과 같은 화면 공간 굴절이다(기억에 의존).
 * 병합 지점: W10 화면 공간 왜곡 버퍼가 들어오면 굴절의 비낌을 그 버퍼에 쓰는 길로 합친다 — 사본을 두 번 읽는 길을 두 벌 두지 않는다.
 */

#ifndef SW_ENGINE_SCENEREFRACTION_HLSLI
#define SW_ENGINE_SCENEREFRACTION_HLSLI

// 사본을 읽는 셰이더는 머티리얼에 MATERIAL_REFRACTION 을 단다 — 그 define 이 반투명 정렬의 첫 띠(굴절 먼저, Material::readsSceneColorCopy)와 반투명 목록 검사를 켠다.
// define 없이 사본을 읽으면 정렬이 그 머티리얼을 보통 반투명으로 다뤄 굴절 너머의 반투명이 사라진다.
// 머티리얼 define 없이 도는 리플렉션 컴파일(스키마 읽기)은 지나간다 — 어긋난 조합만 멈춘다(appearance_lit 의 기능 검사와 같은 모양).
#if defined( MATERIAL_BLEND_TRANSLUCENT ) != defined( MATERIAL_REFRACTION )
    #error "scene_refraction.hlsli reads the scene colour copy: a translucent material that includes it needs MATERIAL_REFRACTION, and MATERIAL_REFRACTION needs alphaMode=\"Blend\" (MATERIAL_BLEND_TRANSLUCENT)"
#endif

// 깊이 비교의 여유(장치 깊이) — 굴절 면과 같은 깊이의 장면(물가에 닿은 바닥)을 면 앞으로 오판하지 않는다.
static const float kSceneRefractionDepthBias = 1e-5f;
// 흐린 굴절(간유리)의 탭 수 — 보겔 원판. 거칠기 0 이면 가운데 한 탭만 읽는다.
static const uint kSceneRefractionBlurTapCount = 12u;

struct SceneRefraction
{
    static bool isAvailable();                                                     // 이 파이프라인이 장면 색 사본을 걸었는지(패스 상수라 분기가 고르다)
    static float2 computeScreenUv( float3 worldPosition );                         // Transform::computeWorldPositionFromDepth 의 역
    static float2 resolveUv( float2 screenUv, float2 offsetUv, float pixelDepth ); // 비껴 읽은 자리가 면 앞의 물체면 제자리
    static float3 sampleColor( float2 uv, float blurRadiusUv );                    // 사본 색(흐림 반경 0 이면 한 탭)
    static float3 computeRefractedColor( float3 worldPosition, float3 refractedDirection, float thickness, float pixelDepth, float2 screenUv, float blurRadiusUv );
};

static bool SceneRefraction::isAvailable()
{
    return g_SceneColorCopyIndex != kInvalidIndex;
}

static float2 SceneRefraction::computeScreenUv( float3 worldPosition )
{
    const float4 clip = mul( float4( worldPosition, 1.0f ), g_ViewProj );
    const float2 ndc  = clip.xy / max( abs( clip.w ), 1e-6f );
    return ndc * float2( 0.5f, -0.5f ) + float2( 0.5f, 0.5f );
}

static float2 SceneRefraction::resolveUv( float2 screenUv, float2 offsetUv, float pixelDepth )
{
    const float2 candidateUv = saturate( screenUv + offsetUv );
    float2       resolvedUv  = candidateUv;
    SW_BRANCH if ( g_SceneDepthCopyIndex != kInvalidIndex )
    {
        const float candidateDepth = Bindless::sampleIndexWith( SW_ENGINE_ROLE( SceneDepthCopy ), SW_SAMPLER_POINT_CLAMP, candidateUv ).r;
        resolvedUv                 = ( candidateDepth + kSceneRefractionDepthBias >= pixelDepth ) ? candidateUv : screenUv;
    }
    return resolvedUv;
}

static float3 SceneRefraction::sampleColor( float2 uv, float blurRadiusUv )
{
    float3 color = Bindless::sampleIndex( SW_ENGINE_ROLE( SceneColorCopy ), uv ).rgb;
    SW_BRANCH if ( blurRadiusUv > 0.0f )
    {
        // 보겔 원판 — 황금각으로 돌며 반지름을 √(i/n) 로 늘린다(고른 원판 표본).
        const float kGoldenAngle = 2.39996323f;
        for ( uint tap = 1u; tap < kSceneRefractionBlurTapCount; ++tap )
        {
            const float  radius = sqrt( ( tap + 0.5f ) / kSceneRefractionBlurTapCount ) * blurRadiusUv;
            const float  angle  = tap * kGoldenAngle;
            const float2 offset = float2( cos( angle ), sin( angle ) ) * radius;
            color += Bindless::sampleIndex( SW_ENGINE_ROLE( SceneColorCopy ), saturate( uv + offset ) ).rgb;
        }
        color /= (float)kSceneRefractionBlurTapCount;
    }
    return color;
}

/**
 * @brief 굴절된 장면 색 — 굴절한 시선이 두께만큼 나아간 점을 화면에 투영해 그 자리의 사본을 읽는다.
 * @param refractedDirection `refract( -toView, normal, 1 / ior )` — 두께가 0 이면(창유리) 비낌이 없다
 */
static float3 SceneRefraction::computeRefractedColor( float3 worldPosition, float3 refractedDirection, float thickness, float pixelDepth, float2 screenUv, float blurRadiusUv )
{
    const float2 exitUv   = SceneRefraction::computeScreenUv( worldPosition + refractedDirection * thickness );
    const float2 offsetUv = exitUv - SceneRefraction::computeScreenUv( worldPosition );
    return SceneRefraction::sampleColor( SceneRefraction::resolveUv( screenUv, offsetUv, pixelDepth ), blurRadiusUv );
}

#endif // SW_ENGINE_SCENEREFRACTION_HLSLI
