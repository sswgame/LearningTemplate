/**
 * hair_shading.hlsli — 머리카락 셰이딩 모델(Marschner 근사: R · TT · TRT 세 갈래 + 다중 산란 확산)입니다. 머티리얼 셰이더 hair.hlsl 과 머티리얼 그래프의
 * 셰이딩 모델 Hair(material_graph.hlsli 의 SW_MATERIAL_GRAPH_HAIR)가 함께 쓴다.
 *
 * 고른 까닭: Kajiya-Kay(이동 탄젠트 이중 스펙큘러)는 싸지만 빛이 뒤에서 올 때의 투과(TT — 역광 머리카락이 빛나는 테두리)와 색 있는 2 차 하이라이트(TRT)를
 * 손으로 흉내 내야 한다. 언리얼 Hair 셰이딩 모델(Karis 2016 "Physically Based Hair Shading in Unreal")의 근사는 빛 하나에 exp 넷 · pow 둘 정도라 포워드 빛
 * 목록에서도 Kajiya-Kay 두 갈래와 비용 차가 작고, 흰 머리카락의 구 적분이 1 근처라(에너지) 빛 세기를 머티리얼마다 맞출 일이 없다. 그래서 이 근사를 쓴다.
 *
 * 탄젠트: 가닥 방향 T(월드, 단위)를 받는다. 카드 메시는 정점 탄젠트(dP/du) 또는 바이탄젠트(dP/dv)가 가닥을 따라가므로 머티리얼이 고른다(hair.hlsl `_strandAxis`).
 * 이동(shift)은 큐티클 기울기라 R 은 뿌리 쪽(−2α), TT 는 끝 쪽(α), TRT 는 더 끝 쪽(4α)으로 하이라이트를 옮긴다(Marschner 2003).
 *
 * C++ 사본: `Source/Engine/Renderer/Lighting/HairShading.h` 의 `HairSurface` 가 같은 식이다(단위 테스트 TestHairShading 이 갈래 에너지 · 이동을 본다). 식을 바꾸면 둘을 함께 고친다 —
 * HLSL 을 C++ 로 컴파일할 수 없어 둔 사본이다(형식으로 막지 못하는 틈).
 */

#ifndef SW_ENGINE_HAIRSHADING_HLSLI
#define SW_ENGINE_HAIRSHADING_HLSLI

// 머리카락 큐티클 굴절률 1.55 의 수직 입사 반사율((n − 1)² / (n + 1)²).
static const float kHairF0 = 0.04652f;
// 거칠기 하한 — 가우스 폭이 0 이면 나눗셈이 터진다.
static const float kHairMinRoughness = 0.05f;

/** @brief 머리카락 셰이딩의 표면 값입니다. */
struct HairSurface
{
    float3 _baseColor; ///< 흡수 색(TT · TRT 가 이 색으로 물든다)
    float  _roughness; ///< 세로(가닥 방향) 거칠기 0..1
    float  _shift;     ///< 큐티클 기울기(라디안, 보통 0.035)
    float  _specular;  ///< R 갈래 배율(1 = 물리값)
    float  _scatter;   ///< 다중 산란 확산의 세기 0..1(검은 머리 0.2 · 금발 1 언저리)
    float3 _tangent;   ///< 가닥 방향(월드, 단위)
};

struct HairShading
{
    static float computeLongitudinal( float width, float offset );
    static float computeFresnel( float cosine );
    static float3 shiftTangent( float3 tangent, float3 normal, float shift );
    static float3 evaluateLobes( HairSurface surface, float3 toView, float3 toLight );
    static float3 evaluateScatter( HairSurface surface, float3 normal, float3 toView, float3 toLight );
};

/** @brief 세로 갈래(가우스)입니다 — 폭 @p width, 중심에서 떨어진 정도 @p offset(sin θi + sin θr − α). 넓이가 1 이다. */
static float HairShading::computeLongitudinal( float width, float offset )
{
    return exp( -0.5f * offset * offset / ( width * width ) ) / ( 2.50662827f * width );
}

/** @brief 슐릭 프레넬(머리카락 F0)입니다. */
static float HairShading::computeFresnel( float cosine )
{
    const float oneMinus = 1.0f - saturate( cosine );
    const float square   = oneMinus * oneMinus;
    return kHairF0 + ( 1.0f - kHairF0 ) * square * square * oneMinus;
}

/** @brief 탄젠트를 노멀 쪽으로 @p shift 만큼 기울입니다(Kajiya-Kay 이동 탄젠트 — 가닥마다 하이라이트를 흩뜨리는 노이즈에 쓴다). 결과는 단위 벡터다. */
static float3 HairShading::shiftTangent( float3 tangent, float3 normal, float shift )
{
    return normalize( tangent + normal * shift );
}

/**
 * @brief R · TT · TRT 세 갈래의 합입니다(빛 하나, 빛 색 · 그림자를 곱하기 전). 언리얼 HairBxDF 의 근사 식이다.
 * @param toView  표면에서 눈으로(단위)
 * @param toLight 표면에서 빛으로(단위)
 */
static float3 HairShading::evaluateLobes( HairSurface surface, float3 toView, float3 toLight )
{
    const float  sinThetaL  = clamp( dot( surface._tangent, toLight ), -1.0f, 1.0f );
    const float  sinThetaV  = clamp( dot( surface._tangent, toView ), -1.0f, 1.0f );
    const float  cosThetaD  = cos( 0.5f * abs( asin( sinThetaL ) - asin( sinThetaV ) ) );
    const float3 lightPerp  = toLight - surface._tangent * sinThetaL;
    const float3 viewPerp   = toView - surface._tangent * sinThetaV;
    const float  cosPhi     = dot( lightPerp, viewPerp ) * rsqrt( dot( lightPerp, lightPerp ) * dot( viewPerp, viewPerp ) + 1e-4f );
    const float  cosHalfPhi = sqrt( saturate( 0.5f + 0.5f * cosPhi ) );
    const float  roughness  = max( surface._roughness, kHairMinRoughness );
    const float  width      = roughness * roughness;
    const float  sumSin     = sinThetaL + sinThetaV;

    // R — 겉면 반사. 큐티클이 뿌리 쪽으로 −2α 옮긴다.
    const float  reflectFresnel = HairShading::computeFresnel( sqrt( saturate( 0.5f + 0.5f * dot( toLight, toView ) ) ) );
    const float3 lobeR          = HairShading::computeLongitudinal( width, sumSin + 2.0f * surface._shift ) * 0.25f * cosHalfPhi * reflectFresnel * surface._specular;

    // TT — 두 번 통과(역광). 굴절률은 기울어진 입사의 유효값(Bravais).
    const float  inverseIndex    = 1.0f / ( 1.19f / cosThetaD + 0.36f * cosThetaD );
    const float  height          = cosHalfPhi * ( 1.0f + inverseIndex * ( 0.6f - 0.8f * cosPhi ) );
    const float  transmitFresnel = HairShading::computeFresnel( cosThetaD * sqrt( saturate( 1.0f - height * height ) ) );
    const float3 transmitColor   = pow( max( surface._baseColor, 1e-5f ), 0.5f * sqrt( saturate( 1.0f - height * height * inverseIndex * inverseIndex ) ) / cosThetaD );
    const float3 lobeTT          = HairShading::computeLongitudinal( width * 0.5f, sumSin - surface._shift ) * exp( -3.65f * cosPhi - 3.98f ) * ( 1.0f - transmitFresnel ) *
                          ( 1.0f - transmitFresnel ) * transmitColor;

    // TRT — 안쪽에서 한 번 반사(색 있는 2 차 하이라이트).
    const float  innerFresnel = HairShading::computeFresnel( cosThetaD * 0.5f );
    const float3 innerColor   = pow( max( surface._baseColor, 1e-5f ), 0.8f / cosThetaD );
    const float3 lobeTRT      = HairShading::computeLongitudinal( width * 2.0f, sumSin - 4.0f * surface._shift ) * exp( 17.0f * cosPhi - 16.78f ) * ( 1.0f - innerFresnel ) *
                           ( 1.0f - innerFresnel ) * innerFresnel * innerColor;
    return lobeR + lobeTT + lobeTRT;
}

/**
 * @brief 다중 산란의 확산 근사입니다 — 가닥 사이를 여러 번 튄 빛(언리얼의 가짜 노멀 · 감싸기 확산과 Kajiya 확산을 섞은 식).
 * @details 노멀은 눈 쪽으로 편 가짜 노멀(카드 앞뒤가 같은 밝기)이고, 감싸기 1 이라 빛 반대쪽도 조금 밝다.
 */
static float3 HairShading::evaluateScatter( HairSurface surface, float3 normal, float3 toView, float3 toLight )
{
    const float3 viewFlat   = toView - normal * dot( toView, normal );
    const float  flatLength = length( viewFlat );
    const float3 fakeNormal = flatLength > 1e-4f ? viewFlat / flatLength : normal;
    const float  wrapped    = saturate( ( dot( fakeNormal, toLight ) + 1.0f ) * 0.25f );
    const float  kajiya     = 1.0f - abs( dot( surface._tangent, toLight ) );
    return sqrt( max( surface._baseColor, 0.0f ) ) * ( 0.31830989f * lerp( wrapped, kajiya, 0.33f ) * surface._scatter );
}

#endif // SW_ENGINE_HAIRSHADING_HLSLI
