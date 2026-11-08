/**
 * @file appearance_lit.hlsl
 * @brief 외형 머티리얼 셰이더 — 피부 · 천 · 그물 · 털 · 금속 · 나무 · 비늘 · 진주층 · 돌 · 유리 · 반투명 · 눈을 한 셰이더의 셰이딩 모델 엽과 무늬로 그린다.
 * @details 라이브러리(`engine/materials/library/`)의 갈래 바탕 머티리얼이 define 으로 엽 · 무늬를 고르고, 프리셋은 값만 바꾼다(같은 PSO — 언리얼 Material Instance).
 *          머티리얼마다 C++ 가 없다. 조명은 겹 셰이딩 모델(shading_model.hlsli)의 표면 BRDF 를 lighting.hlsli 의 빛 고리 한 벌이 돈다.
 *
 *          엽 define(갈래 바탕의 _alwaysDefines): MATERIAL_LOBE_SHEEN · MATERIAL_LOBE_SUBSURFACE · MATERIAL_LOBE_TRANSMISSION · MATERIAL_LOBE_ANISOTROPY · MATERIAL_LOBE_THIN_FILM ·
 *          MATERIAL_LOBE_STRAND. 코트(MATERIAL_LOBE_COAT)는 이 셰이더가 늘 켠다 — 젖음 · 기름막(피지 · 눈물 · 침 · 니스)이 어느 면에나 얹히는 한 벌이라서다.
 *          기능 define: MATERIAL_PATTERN_*(appearance_pattern.hlsli) · MATERIAL_NET_UNDERLAYER · MATERIAL_REFRACTION(유리) · MATERIAL_EYE(appearance_eye.hlsli) ·
 *          MATERIAL_FUR_SHELL(appearance_fur.hlsli, MATERIAL_VERTEX_DEFORM 과 함께).
 *          디퍼드: 엽 하나는 셰이딩 모델 번호 + 사용자 칸 네 채널로 G버퍼에 싣는다(SW_MATERIAL_CUSTOM_SHADING — computeMaterialShading, 표는 shading_model_ids.hlsli).
 *            눈 → EYE(코트 · 공막 감쌈), 피부 → SUBSURFACE, 천 · 레이스 → CLOTH, 솔질 금속 → ANISOTROPIC, 얇은 면 → THIN_TRANSLUCENT, 엽 없는 갈래(나무 · 돌 · 가죽 · 금속 · 녹 · 코트) → CLEAR_COAT.
 *            코트를 칸에 싣지 않는 번호의 젖음은 G버퍼 거칠기에 접는다(computeStoredRoughness).
 *          포워드 전용(MATERIAL_FORWARD_ONLY)은 엽이 겹쳐 칸 넷에 들지 않는 갈래뿐이다 — 아래 검사가 G버퍼 변형을 쿠킹에서 멈춘다.
 *            박막(진주층 · 비늘): 시선 각마다 다른 F0 색(채널 셋) + 바탕 PBR — G버퍼의 반사율 칸은 스칼라다.
 *            털: 가닥 방향(둘) · 흡수 색(셋) · 큐티클 기울기(하나) — 여섯 채널.
 *            스타킹(그물 아래층): 피부 BRDF 와 천 BRDF 를 덮임으로 섞는다 — 번호가 둘이다.
 *            비단(시엔 + 비등방): 시엔 색 · 거칠기(넷) + 탄젠트 · 비등방(셋) — 일곱 채널.
 *            유리 · 반투명은 반투명 목록이라 G버퍼에 그리지 않는다.
 *
 *          젖음 · 마른 자국(모든 갈래, 런타임 — W16 의 캐릭터 표면 상태가 UV 공간 마스크를 머티리얼 인스턴스에 올린다):
 *            `_wetness`(스칼라 문) × `_wetMask`(텍스처 R) — 알베도를 어둡게, 코트 무게 ↑, 코트 거칠기 ↓, 노멀을 매끈하게(막이 잔결을 메운다).
 *            `_wetMeniscus` — 젖은 자리 가장자리(마스크 기울기)의 하이라이트 띠. 눈에서는 아래 눈꺼풀 눈물 띠의 세기도 이 값이다.
 *            `_residue`(스칼라 문) × `_residueMask`(텍스처 R) — 마른 눈물 · 침 자국: 조금 희게, 거칠게, 소금 반짝임(드문 칸의 매끈한 코트).
 *            텍스처를 올리지 않은 인스턴스는 흰색으로 읽히므로 스칼라가 0 이면 효과가 없게 문을 건다(surface_lit.hlsl 과 같은 규칙).
 *          `_maskMap` 채널: R 코트(기름 · 니스) 마스크, G 무늬 · 위층 마스크(칠 · 녹 양에 곱한다), B 산란 · 투과 두께, A 쓰지 않음.
 *
 *          결정 표(상용 엔진과 비교, 기억에 의존):
 *          - 피부 산란: 미리 적분한 피부의 값싼 판(채널마다 감싼 확산 × 곡률)을 골랐다. 언리얼 Subsurface Profile(화면 공간 흐림)은 확산을 따로 담는 첨부 ·
 *            스텐실 · 두 번의 흐림 패스가 들고 포워드 · MSAA · 모바일 · 웹에서 맞지 않는다 — 언리얼도 값싼 길로 Preintegrated Skin 모델을 따로 둔다. 확산 첨부가 생기면 패스로 올린다.
 *          - 거울: SSR(W11) + 반사 프로브로 반사한다(언리얼 · 유니티 HDRP 의 기본 길). 평면 반사 패스는 거울마다 장면을 한 번 더 그려 두지 않았다 — 거울 하나가 화면을 채우는
 *            게임 규칙이 생기면 파이프라인 XML 의 패스로 더한다.
 *          - 유리 굴절: 장면 색 사본을 굴절해 읽는다(scene_refraction.hlsli — 물과 한 벌). 간유리의 흐림 · 투과 색 · 프레넬이 픽셀마다 사본 색을 필요로 해서
 *            비낌만 쌓는 왜곡 버퍼(W10 Distortion)로는 다 나타낼 수 없다. 병합 지점: 맑은 유리의 비낌은 W10 왜곡 버퍼로 옮길 수 있다(같은 사본을 두 길로 읽지 않게).
 *          - 그물: 알파 투 커버리지가 RHI 에 없다 — 선 덮임을 픽셀 발자국으로 해석적으로 걸러(밉맵의 일) 피부 위 스타킹은 불투명 한 장으로 섞고, 오려 내는 레이스는 최소 폭을 받친다.
 *          - 털: 껍질(데이터만 — 같은 메시 여럿 + 인스턴스 값). 지느러미 · 카드 · 가닥(언리얼 Groom)은 메시 가공이나 전용 렌더가 든다.
 *          - 눈: 각막 굴절은 화면 공간이 아니라 구 위에서 시선을 굴절해 홍채 평면까지 따라간다(언리얼 Eye 예제의 EyeRefraction). 홍채 노멀은 메시 노멀 그대로다
 *            (shortcut: 오목한 홍채의 따로 노는 노멀이 없어 비스듬한 빛에서 홍채 음영이 얕다 — 클로즈업 눈이 중요해지면 홍채 노멀을 BRDF 에 둘째 노멀로 둔다).
 */
#define SW_SHADING_MODEL_LAYERED
#define MATERIAL_LOBE_COAT
#include "lighting.hlsli"

// appearance.material(라이브러리 뿌리)의 프로퍼티 순서 · 타입과 1:1 이다.
SW_MATERIAL_BEGIN
{
    float4 _color;           ///< 바탕 색(알파 = 불투명도). 눈은 공막, 유리는 투과 색
    float4 _secondaryColor;  ///< 무늬의 둘째 색(늦은 나이테 · 씨실 · 닳은 금속 · 비늘 가장자리 · 눈의 윤부 고리)
    float4 _layerColor;      ///< 위층 색(칠 · 녹 · 스타킹 아래 피부 · 눈의 홍채)
    float4 _sheenColor;      ///< 천 시엔 색 · 털 반사 색
    float4 _subsurfaceColor; ///< 산란 · 투과 색(피부는 채널마다 번지는 정도 — 빨강이 가장 크다)
    float4 _glowColor;       ///< 눈 반사판(눈빛) 색, HDR
    float  _roughness;
    float  _metallic;
    float  _specular; ///< 유전체 반사율 F0 = 0.08 × 값(언리얼 Specular, 0.5 → 0.04)
    float  _normalScale;
    float  _coatWeight;
    float  _coatRoughness;
    float  _sheenRoughness;
    float  _sheenWeight;      ///< 0 이면 GGX 반사, 1 이면 천 시엔(언리얼 Cloth 값)
    float  _subsurfaceRadius; ///< 감쌈 — 빛이 그늘로 번지는 폭(0..1). 투과의 감쌈도 이 값이다
    float  _transmission;     ///< 뒤에서 오는 빛의 세기(커튼 · 잎)
    float  _anisotropy;       ///< -1..1, UV u 방향(탄젠트)으로 늘어난다
    float  _strandShift;
    float  _filmThickness; ///< 박막 두께(nm)
    float  _filmIor;
    float  _filmWeight;
    float  _ior;            ///< 유리 · 각막 굴절률, 박막의 바탕 굴절률
    float  _thickness;      ///< 유리 두께(m) — 굴절의 비낌. 0 이면 얇은 창유리
    float  _refractionBlur; ///< 간유리 흐림(0..1)
    float  _patternScale;
    float  _patternWidth;
    float  _patternAmount;
    float  _layerRoughness;
    float  _alphaCutoff;
    float  _wetness;
    float  _residue;
    float  _wetMeniscus;
    float  _pupilShape;
    float  _pupilMinRadius;
    float  _pupilMaxRadius;
    float  _irisRadius;
    float  _corneaHeight;
    float  _limbalWidth;
    float  _tearLineHeight;
    uint   _albedoMap;
    uint   _normalMap;
    uint   _ormMap;
    uint   _maskMap;
    uint   _wetMask;
    uint   _residueMask;
    // 원소 크기를 16 의 배수로(pbr_lit.hlsl 과 같은 까닭). 머티리얼 속성이 아니다
    uint _swPad0;
}
SW_MATERIAL_END

#if defined( MATERIAL_VERTEX_DEFORM ) && !defined( MATERIAL_FUR_SHELL )
    #error "appearance_lit.hlsl moves vertices only for fur shells: MATERIAL_VERTEX_DEFORM needs MATERIAL_FUR_SHELL"
#endif
#if defined( MATERIAL_REFRACTION ) && !defined( MATERIAL_BLEND_TRANSLUCENT )
    #error "MATERIAL_REFRACTION draws in the translucent list: the material needs alphaMode=\"Blend\" and MATERIAL_BLEND_TRANSLUCENT"
#endif
// 엽이 겹치는 갈래는 G버퍼 칸 넷에 들지 않는다(머리말의 포워드 전용 표) — define 을 빠뜨린 머티리얼이 G버퍼에서 조용히 엽을 잃지 않게 쿠킹에서 멈춘다.
#if defined( SW_PASS_GBUFFER ) && ( defined( MATERIAL_LOBE_THIN_FILM ) || defined( MATERIAL_LOBE_STRAND ) || defined( MATERIAL_NET_UNDERLAYER ) || ( defined( MATERIAL_LOBE_SHEEN ) && ( defined( MATERIAL_LOBE_ANISOTROPY ) || defined( MATERIAL_LOBE_SUBSURFACE ) ) ) )
    #error "appearance_lit.hlsl: layered lobes (thin film, fur strand, net underlayer, sheen with anisotropy or subsurface) do not fit the G-buffer: list MATERIAL_FORWARD_ONLY"
#endif

// G버퍼의 셰이딩 모델 번호 — 엽 define 하나가 고른다(눈은 산란 define 도 가져 먼저 고른다). 코트를 칸에 싣는 번호만 SW_APPEARANCE_STORES_COAT 1 이다.
#if defined( MATERIAL_EYE )
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_EYE
    #define SW_APPEARANCE_STORES_COAT   1
#elif defined( MATERIAL_LOBE_SUBSURFACE )
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_SUBSURFACE
    #define SW_APPEARANCE_STORES_COAT   0
#elif defined( MATERIAL_LOBE_SHEEN )
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_CLOTH
    #define SW_APPEARANCE_STORES_COAT   0
#elif defined( MATERIAL_LOBE_ANISOTROPY )
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_ANISOTROPIC
    #define SW_APPEARANCE_STORES_COAT   0
#elif defined( MATERIAL_LOBE_TRANSMISSION )
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_THIN_TRANSLUCENT
    #define SW_APPEARANCE_STORES_COAT   0
#else
    #define SW_APPEARANCE_SHADING_MODEL SW_SHADING_MODEL_ID_CLEAR_COAT // 코트 무게 0 이면 기본 PBR 과 같다
    #define SW_APPEARANCE_STORES_COAT   1
#endif

#define SW_MATERIAL_CUSTOM_LIGHTING
#define SW_MATERIAL_CUSTOM_SHADING
#define SW_MATERIAL_CUSTOM_VARYINGS
#if !defined( SW_PASS_SHADOW ) && !defined( SW_PASS_DEPTH_PREPASS )
    #define SW_MATERIAL_USES_TANGENT
#endif
struct MaterialVaryings
{
    float3 _localPosition : TEXCOORD8; ///< 오브젝트 공간 위치(나뭇결 · 눈)
    float4 _instanceData0 : TEXCOORD9; ///< 인스턴스 값 0(눈: x 동공 확장, 털: y 껍질 비율 · z 밀어 낸 거리)
#if defined( MATERIAL_EYE )
    float4 _instanceData1 : TEXCOORD10; ///< 인스턴스 값 1(눈: rgb 이 눈의 홍채 색, a 무게)
    float3 _axisX : TEXCOORD11;         ///< 오브젝트 축의 월드 방향 — 시선을 눈 공간으로 옮긴다
    float3 _axisY : TEXCOORD12;
    float3 _axisZ : TEXCOORD13;
#endif
};
#include "material_shader.hlsli"
#include "normal_map.hlsli"
#include "scene_refraction.hlsli"

#if defined( MATERIAL_EYE )
MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    MaterialVaryings varyings;
    varyings._localPosition = context._localPosition;
    varyings._instanceData0 = context._instance.getCustomData( 0u );
    varyings._instanceData1 = context._instance.getCustomData( 1u );
    varyings._axisX         = context._instance._world[0].xyz;
    varyings._axisY         = context._instance._world[1].xyz;
    varyings._axisZ         = context._instance._world[2].xyz;
    return varyings;
}
#else
MaterialVaryings computeMaterialVaryings( VertexContext context )
{
    MaterialVaryings varyings;
    varyings._localPosition = context._localPosition;
    varyings._instanceData0 = context._instance.getCustomData( 0u );
    return varyings;
}
#endif

#if defined( MATERIAL_FUR_SHELL )
/** @brief 털 껍질 — 인스턴스 값의 거리만큼 노멀로 밀어 내고, 끝으로 갈수록 조금 처진다(중력). */
float3 computeWorldPositionOffset( VertexContext context )
{
    const float4 shell = context._instance.getCustomData( 0u );
    return context._worldNormal * shell.z + float3( 0.0f, -1.0f, 0.0f ) * shell.z * shell.y * 0.3f;
}
#endif

/** @brief 외형 표면 — 머티리얼 · 텍스처 · 무늬 · 젖음이 채운 엽의 값. 표면 훅과 조명 훅이 같은 make 를 부른다(같은 식이라 컴파일러가 한 번만 계산한다). */
struct AppearanceSurface
{
    float4 _albedo;
    float3 _normal;
    float  _roughness;
    float  _metallic;
    float  _specular;
    float  _coatWeight;
    float  _coatRoughness;
    float3 _sheenColor;
    float  _sheenRoughness;
    float  _sheenWeight;
    float3 _subsurfaceWrap;
    float3 _transmissionColor;
    float  _transmissionWrap;
    float3 _tangent;
    float  _anisotropy;
    float3 _strandColor;
    float  _strandShift;
    float  _filmThickness;
    float  _filmIor;
    float  _filmWeight;
    float  _ior;
    float3 _emissive;
    float3 _secondaryColor;
    float3 _layerColor;
    float  _layerRoughness;
    float  _patternScale;
    float  _patternWidth;
    float  _patternAmount;

    static AppearanceSurface make( PixelContext context );
    static float4 sampleBaseTexture( PixelContext context );
    static float3 computeBaseNormal( PixelContext context );
    static float3 getTangent( PixelContext context );
    void perturbNormal( float2 slope, PixelContext context ); // UV 공간 높이 기울기로 노멀을 기울인다(무늬)
    void applyWetFilm( PixelContext context );
    SurfaceBRDF makeBRDF( float3 worldPosition );
    float4 computeCustomData();     // G버퍼 사용자 칸 — 셰이딩 모델 번호마다(shading_model_ids.hlsli 표)
    float computeStoredRoughness(); // G버퍼 거칠기 — 코트를 싣지 않는 번호는 코트를 접는다
};

#include "appearance_pattern.hlsli"
#include "appearance_eye.hlsli"
#include "appearance_fur.hlsli"

#if defined( MATERIAL_EYE )
static float4 AppearanceSurface::sampleBaseTexture( PixelContext context )
{
    return float4( 1.0f, 1.0f, 1.0f, 1.0f ); // 눈의 _albedoMap 은 홍채 무늬다(홍채 좌표로 읽는다 — appearance_eye.hlsli)
}
#else
static float4 AppearanceSurface::sampleBaseTexture( PixelContext context )
{
    return MaterialTexture::sampleIndex( context._material._albedoMap, context._uv );
}
#endif

#if defined( SW_MATERIAL_USES_TANGENT )
static float3 AppearanceSurface::computeBaseNormal( PixelContext context )
{
    return applyNormalMap( context._material._normalMap, context._material._normalScale, context._uv, context._normal, context._tangent );
}

static float3 AppearanceSurface::getTangent( PixelContext context )
{
    return context._tangent.xyz;
}

void AppearanceSurface::perturbNormal( float2 slope, PixelContext context )
{
    const float3 orthogonal = context._tangent.xyz - _normal * dot( _normal, context._tangent.xyz );
    const float  lengthSq   = dot( orthogonal, orthogonal );
    const float3 tangent    = lengthSq > 1e-8f ? orthogonal * rsqrt( lengthSq ) : float3( 0.0f, 0.0f, 0.0f );
    const float3 bitangent  = cross( _normal, tangent ) * ( context._tangent.w < 0.0f ? -1.0f : 1.0f );
    _normal                 = normalize( _normal - tangent * slope.x - bitangent * slope.y );
}
#else
static float3 AppearanceSurface::computeBaseNormal( PixelContext context )
{
    return context._normal;
}

static float3 AppearanceSurface::getTangent( PixelContext context )
{
    return float3( 1.0f, 0.0f, 0.0f );
}

void AppearanceSurface::perturbNormal( float2 slope, PixelContext context )
{
    // 깊이 패스 — 노멀을 쓰지 않는다.
}
#endif

/** @brief 젖음 · 기름막 · 마른 자국 — 코트 엽 하나로 그린다(피지 · 눈물 · 침 · 비 · 니스가 같은 층이다). */
void AppearanceSurface::applyWetFilm( PixelContext context )
{
    const MaterialData material = context._material;
    const float        wetMask  = MaterialTexture::sampleIndex( material._wetMask, context._uv ).r;
    const float        wet      = ( material._wetness > 0.0f ) ? saturate( material._wetness * wetMask ) : 0.0f;
    // 가장자리 띠: 마스크가 화면에서 바뀌는 곳(표면장력으로 물이 모여 솟은 가장자리).
    const float meniscus = saturate( length( float2( ddx( wet ), ddy( wet ) ) ) * 8.0f ) * saturate( material._wetMeniscus );
    _albedo.rgb *= lerp( 1.0f, 0.6f, wet ); // 젖으면 안으로 들어간 빛이 더 흡수된다
    _normal        = normalize( lerp( _normal, context._normal, wet * 0.7f ) );
    _coatWeight    = max( max( _coatWeight, wet ), meniscus );
    _coatRoughness = lerp( lerp( _coatRoughness, 0.05f, wet ), 0.02f, meniscus );

    const float residueMask = MaterialTexture::sampleIndex( material._residueMask, context._uv ).r;
    const float residue     = ( material._residue > 0.0f ) ? saturate( material._residue * residueMask ) : 0.0f;
    // 소금 반짝임: 드문 작은 칸이 매끈한 결정면이다.
    const float sparkle = step( 0.97f, AppearanceNoise::hash( context._uv * 600.0f ) ) * residue;
    _albedo.rgb         = lerp( _albedo.rgb, 1.0f - ( 1.0f - _albedo.rgb ) * 0.7f, residue * 0.5f );
    _roughness          = lerp( _roughness, 0.8f, residue );
    _coatWeight         = max( _coatWeight, sparkle );
    _coatRoughness      = lerp( _coatRoughness, 0.03f, sparkle );
}

static AppearanceSurface AppearanceSurface::make( PixelContext context )
{
    const MaterialData material = context._material;
    // 텍스처가 없으면(kInvalidIndex · bindless 에뮬 백엔드) 흰색이라 값 그대로다.
    const float3 orm  = MaterialTexture::sampleIndex( material._ormMap, context._uv ).rgb;
    const float4 mask = MaterialTexture::sampleIndex( material._maskMap, context._uv );
    // 곡률(1/m) — 휘어진 곳(코 · 귀 · 손가락)일수록 빛이 그늘로 더 번진다(미리 적분한 피부가 곡률로 표를 고르는 것의 값싼 판).
    const float curvature = length( fwidth( context._normal ) ) / max( length( fwidth( context._worldPosition ) ), 1e-5f );

    AppearanceSurface surface;
    surface._albedo = context._color * material._color * AppearanceSurface::sampleBaseTexture( context );
    surface._albedo.rgb *= orm.r;
    surface._normal            = AppearanceSurface::computeBaseNormal( context );
    surface._roughness         = saturate( orm.g * material._roughness );
    surface._metallic          = saturate( orm.b * material._metallic );
    surface._specular          = material._specular;
    surface._coatWeight        = saturate( material._coatWeight * mask.r );
    surface._coatRoughness     = material._coatRoughness;
    surface._sheenColor        = material._sheenColor.rgb;
    surface._sheenRoughness    = material._sheenRoughness;
    surface._sheenWeight       = material._sheenWeight;
    surface._subsurfaceWrap    = material._subsurfaceColor.rgb * material._subsurfaceRadius * mask.b * lerp( 0.6f, 1.0f, saturate( curvature * 0.05f ) );
    surface._transmissionColor = material._subsurfaceColor.rgb * material._transmission * mask.b;
    surface._transmissionWrap  = material._subsurfaceRadius;
    surface._tangent           = AppearanceSurface::getTangent( context );
    surface._anisotropy        = material._anisotropy;
    surface._strandColor       = float3( 0.0f, 0.0f, 0.0f );
    surface._strandShift       = material._strandShift;
    surface._filmThickness     = material._filmThickness;
    surface._filmIor           = material._filmIor;
    surface._filmWeight        = material._filmWeight;
    surface._ior               = material._ior;
    surface._emissive          = float3( 0.0f, 0.0f, 0.0f );
    surface._secondaryColor    = material._secondaryColor.rgb;
    surface._layerColor        = material._layerColor.rgb;
    surface._layerRoughness    = material._layerRoughness;
    surface._patternScale      = material._patternScale;
    surface._patternWidth      = material._patternWidth;
    surface._patternAmount     = material._patternAmount * mask.g;

    AppearancePattern::apply( surface, context );
    AppearanceFur::apply( surface, context );
    AppearanceEye::apply( surface, context );
    surface.applyWetFilm( context );
    return surface;
}

/** @brief 표면 값을 겹 BRDF 로 — 엽 설정은 늘 부르고, 꺼진 엽은 define 이 읽지 않는다. */
SurfaceBRDF AppearanceSurface::makeBRDF( float3 worldPosition )
{
    SurfaceBRDF brdf = SurfaceBRDF::makePBR( ViewMode::overrideLightingAlbedo( _albedo.rgb ), worldPosition, _normal, _roughness, _metallic );
    // 유전체 F0 를 Specular 값으로(금속은 기본 색 그대로).
    const float dielectric   = 0.08f * saturate( _specular );
    brdf._pbr._specularColor = lerp( float3( dielectric, dielectric, dielectric ), _albedo.rgb, saturate( _metallic ) );
    brdf.setCoat( _coatWeight, _coatRoughness );
    brdf.setSheen( _sheenColor, _sheenRoughness, _sheenWeight );
    brdf.setSubsurface( _subsurfaceWrap );
    brdf.setTransmission( _transmissionColor, _transmissionWrap );
    brdf.setAnisotropy( _tangent, _anisotropy );
    brdf.setThinFilm( _filmThickness, _filmIor, max( _ior, 1.0f ), _filmWeight );
    brdf.setStrand( _tangent, _strandShift, _roughness, _strandColor );
    return brdf;
}

#if defined( MATERIAL_EYE )
float4 AppearanceSurface::computeCustomData()
{
    // 홍채 마스크는 make 가 감쌈에 이미 곱했다(appearance_eye.hlsli) — a 는 0 이다.
    return float4( _coatWeight, _coatRoughness, _subsurfaceWrap.r, 0.0f );
}
#elif defined( MATERIAL_LOBE_SUBSURFACE )
float4 AppearanceSurface::computeCustomData()
{
    // 디퍼드는 투과 색을 알베도 × 세기로 푼다(deferred_lighting.hlsl) — 세기는 투과 색의 가장 밝은 채널이다.
    return float4( _subsurfaceWrap, max( _transmissionColor.r, max( _transmissionColor.g, _transmissionColor.b ) ) );
}
#elif defined( MATERIAL_LOBE_SHEEN )
float4 AppearanceSurface::computeCustomData()
{
    return float4( _sheenColor, SurfaceShading::packPair( _sheenRoughness, _sheenWeight ) );
}
#elif defined( MATERIAL_LOBE_ANISOTROPY )
float4 AppearanceSurface::computeCustomData()
{
    return float4( Octahedral::encode( _tangent ) * 0.5f + 0.5f, _anisotropy * 0.5f + 0.5f, 0.0f );
}
#elif defined( MATERIAL_LOBE_TRANSMISSION )
float4 AppearanceSurface::computeCustomData()
{
    return float4( _transmissionColor, _transmissionWrap );
}
#else
float4 AppearanceSurface::computeCustomData()
{
    return float4( _coatWeight, _coatRoughness, 0.0f, 0.0f );
}
#endif

#if SW_APPEARANCE_STORES_COAT
float AppearanceSurface::computeStoredRoughness()
{
    return _roughness;
}
#else
/** @brief 코트 칸이 없는 번호 — 젖음 · 기름막을 코트 무게만큼 코트 거칠기로 접는다(shortcut: 코트의 둘째 반사 · 아래층 감쇠는 잃는다, 젖은 피부 클로즈업이 중요해지면 번호를 나눈다). */
float AppearanceSurface::computeStoredRoughness()
{
    return lerp( _roughness, min( _roughness, _coatRoughness ), _coatWeight );
}
#endif

/** @brief G버퍼 셰이딩 값 — 갈래의 번호 · 사용자 칸 · 반사율 · 발광(눈빛). 조명 훅과 같은 make 를 부른다(같은 식이라 컴파일러가 한 번만 계산한다). */
SurfaceShading computeMaterialShading( PixelContext context, MaterialSurface surface )
{
    AppearanceSurface appearance = AppearanceSurface::make( context );
    SurfaceShading    shading    = SurfaceShading::make( SW_APPEARANCE_SHADING_MODEL );
    shading._specular            = saturate( appearance._specular );
    shading._customData          = appearance.computeCustomData();
    shading._emissive            = appearance._emissive;
    return shading;
}

MaterialSurface computeMaterialSurface( PixelContext context )
{
    AppearanceSurface appearance = AppearanceSurface::make( context );
    MaterialSurface   surface;
    surface._albedo    = appearance._albedo;
    surface._normal    = appearance._normal;
    surface._roughness = appearance.computeStoredRoughness();
    surface._metallic  = appearance._metallic;
    surface.setAlphaCutoff( context._material._alphaCutoff );
    return surface;
}

/** @brief 유리 — 반사(겹 BRDF 의 반사 · 환경)는 그대로 두고, 투과는 굴절한 장면 사본 × 투과 색 × (1 - 프레넬)로 덮는다. */
struct AppearanceRefraction
{
    static float4 compose( PixelContext context, AppearanceSurface appearance, float3 reflected );
};

#if defined( MATERIAL_REFRACTION )
/**
 * @details 몸통(창유리의 먼지 · 간유리의 흐린 몸)은 알베도 알파와 먼지 잡음(`_patternAmount` × fbm)으로 정하고 그만큼 장면이 덜 비친다.
 *          사본이 없는 파이프라인(2D 등)은 굴절 없이 알파 블렌드다(물과 같은 규칙 — 필수 값이 아니라 파이프라인의 능력이다).
 */
static float4 AppearanceRefraction::compose( PixelContext context, AppearanceSurface appearance, float3 reflected )
{
    const float3 toView  = View::computeToView( context._worldPosition );
    const float  fresnel = ShadingMath::computeFresnelSchlick( ShadingMath::computeDielectricF0( max( appearance._ior, 1.0f ) ), saturate( dot( appearance._normal, toView ) ) );
    const float  dirt    = saturate( AppearanceNoise::computeFractal( context._uv * max( appearance._patternScale, 1e-3f ) ) * appearance._patternAmount );
    const float  body    = saturate( max( appearance._albedo.a, dirt ) );
    float4       result  = float4( reflected, max( body, fresnel ) );
    SW_BRANCH if ( SceneRefraction::isAvailable() )
    {
        const float2 screenUv   = context._position.xy * g_SourceTexel.xy;
        const float3 refracted  = refract( -toView, appearance._normal, 1.0f / max( appearance._ior, 1.0f ) );
        const float  blurRadius = saturate( context._material._refractionBlur ) * 0.03f;
        const float3 behind     = SceneRefraction::computeRefractedColor( context._worldPosition, refracted, context._material._thickness, context._position.z, screenUv, blurRadius );
        // 투과 색은 머티리얼 색(알베도 rgb) — 몸통 확산은 reflected 에 이미 있다.
        result = float4( behind * context._material._color.rgb * ( 1.0f - fresnel ) * ( 1.0f - body ) + reflected, 1.0f );
    }
    return result;
}
#else
static float4 AppearanceRefraction::compose( PixelContext context, AppearanceSurface appearance, float3 reflected )
{
    return float4( reflected, appearance._albedo.a );
}
#endif

#if SW_VIEWMODE_SKIPS_LIGHTING
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    return surface._albedo; // 보기 모드 Unlit — 조명 · 그림자 없이 표면 색
}
#else
float4 computeMaterialLighting( PixelContext context, MaterialSurface surface )
{
    AppearanceSurface appearance = AppearanceSurface::make( context );
    const SurfaceBRDF brdf       = appearance.makeBRDF( context._worldPosition );
    const float       shadow     = Shadow::sampleAtWorld( context._worldPosition, appearance._normal );
    const float3      lit        = brdf.shadeLights( context._worldPosition, shadow ) + appearance._emissive;
    return AppearanceRefraction::compose( context, appearance, lit );
}
#endif

#include "material_shader_main.hlsli"
