/**
 * shading_model.hlsli — 겹 셰이딩 모델의 표면 BRDF. lighting.hlsli 가 `SW_SHADING_MODEL_LAYERED` 일 때 기본 PBR 대신 include 한다(빛 고리는 lighting.hlsli 한 벌).
 *
 * 기본 PBR(SurfacePBR) 위에 엽을 얹는다. 엽마다 머티리얼 define 이 정의를 고르고(꺼지면 0 을 내는 정의), 켜진 엽도 무게 0 이면 기본 PBR 과 같다
 * (`makePBR` 이 모든 무게를 0 으로 둔다 — 이 셰이더 안에서 부른 Lighting::shadeLights 도 PBR 그대로다).
 *
 * | 엽 · define                     | 식                                                                  | 언리얼 대응(기억에 의존)               |
 * |---------------------------------|---------------------------------------------------------------------|----------------------------------------|
 * | 코트 MATERIAL_LOBE_COAT         | 둘째 GGX 반사(F0 0.04, 자기 거칠기) + 아래층 감쇠 1 - w·F(N·V)       | Clear Coat (피지 · 립글로스 · 니스 · 젖음) |
 * | 시엔 MATERIAL_LOBE_SHEEN        | Charlie 분포 × Neubelt 가시성, 반사를 lerp( GGX, 시엔, 무게 )        | Cloth(Fuzz Color · Cloth 값)            |
 * | 피부 MATERIAL_LOBE_SUBSURFACE   | 채널마다 감싼 확산(산란 반경 × 곡률)                                 | Preintegrated Skin 의 값싼 판           |
 * | 투과 MATERIAL_LOBE_TRANSMISSION | 뒤에서 오는 빛의 감싼 확산 × 투과색                                  | Two Sided Foliage · Thin Translucent    |
 * | 비등방 MATERIAL_LOBE_ANISOTROPY | Burley 비등방 GGX · Heitz 가시성                                     | Anisotropy 핀                           |
 * | 박막 MATERIAL_LOBE_THIN_FILM    | 시선 각에서 잰 에어리 반사율로 F0 를 바꾼다(파장 셋)                 | Substrate Thin Film                     |
 * | 머리결 MATERIAL_LOBE_STRAND     | Kajiya · Kay(옮긴 탄젠트) — 털                                       | Hair(Marschner)의 값싼 판               |
 *
 * 형식 · 멤버 이름은 lighting.hlsli 의 기본 SurfaceBRDF 와 같다(빛 고리가 부르는 다섯 + makePBR). 엽을 더하면 칸 · 설정 함수 · 엽 함수 하나를 더하고
 * evaluateLight 에서 부른다. W13(머리카락 셰이딩 모델)이 Marschner 엽을 들이면 머리결 엽(털)은 그 식을 부르게 바꾼다 — 같은 식을 두 벌 두지 않는다.
 * 겹 셰이딩은 G버퍼에 칸이 없어 포워드 전용이다(머티리얼이 MATERIAL_FORWARD_ONLY — 툰과 같은 길, 언리얼은 G버퍼에 셰이딩 모델 번호를 싣는다).
 */

#ifndef SW_ENGINE_SHADINGMODEL_HLSLI
#define SW_ENGINE_SHADINGMODEL_HLSLI

static const float kCoatF0 = 0.04f; // 코트(기름 · 니스 · 물) 층의 수직 반사율 — 굴절률 1.5 근처
// 박막 간섭을 재는 파장(nm) — R · G · B 를 대표하는 셋(스펙트럼 적분 대신 값싼 판).
static const float3 kThinFilmWavelengthNm = float3( 650.0f, 532.0f, 450.0f );

/** @brief 겹 표면 BRDF — 기본 PBR 과 엽마다의 값. 칸은 늘 있고 꺼진 엽의 정의가 읽지 않는다(컴파일러가 버린다). */
struct SurfaceBRDF
{
    SurfacePBR _pbr;
    float3     _normal;
    float3     _toView;
    float      _normalDotView;
    float      _lightAcceptance; ///< N·L 이 -이 값보다 크면 빛을 받는다(기본 0 — 감싼 확산 · 투과가 넓힌다)

    float  _coatWeight;
    float  _coatAlpha;
    float3 _sheenColor;
    float  _sheenAlpha;
    float  _sheenWeight;
    float3 _subsurfaceWrap; ///< 채널마다 감쌈(0 이면 램버트)
    float3 _transmissionColor;
    float  _transmissionWrap;
    float3 _tangent;
    float3 _bitangent;
    float  _alphaTangent;
    float  _alphaBitangent;
    float3 _strandTangent;
    float3 _strandColor;
    float  _strandExponent;

    static SurfaceBRDF makePBR( float3 albedo, float3 worldPosition, float3 normal, float roughness, float metallic );
    // 엽 설정 — 표면 훅의 값으로 부른다. 부르지 않은 엽은 무게 0 이다.
    void setCoat( float weight, float roughness );
    void setSheen( float3 color, float roughness, float weight );
    void setSubsurface( float3 wrap );
    void setTransmission( float3 color, float wrap );
    void setAnisotropy( float3 tangent, float anisotropy );
    void setThinFilm( float thicknessNm, float filmIor, float baseIor, float weight );
    void setStrand( float3 tangent, float shift, float roughness, float3 color );

    // 빛 고리가 부르는 다섯(lighting.hlsli 의 기본 SurfaceBRDF 와 같은 이름).
    bool receivesLight( float normalDotLight );
    float3 evaluateLight( float3 toLight, float normalDotLight );
    float3 getAmbientDiffuseColor();
    float3 computeEnvironmentSpecular();
    float getReflectionRoughness();
    float3 shadeLights( float3 worldPosition, float shadow );

    // 엽 — define 이 정의를 고른다(꺼지면 0 · 그대로).
    float3 computeDiffuse( float normalDotLight );
    float3 computeBaseSpecular( float3 toLight, float3 halfway, float normalDotLight, float normalDotHalf, float viewDotHalf );
    float3 computeSheenSpecular( float normalDotLight, float normalDotHalf );
    float3 computeTransmission( float normalDotLight );
    float3 computeStrandSpecular( float3 halfway, float normalDotLight );
    float3 applyCoat( float3 under, float normalDotLight, float normalDotHalf, float viewDotHalf );
    float computeCoatAttenuation();
};

static SurfaceBRDF SurfaceBRDF::makePBR( float3 albedo, float3 worldPosition, float3 normal, float roughness, float metallic )
{
    SurfaceBRDF brdf;
    brdf._pbr               = SurfacePBR::make( albedo, roughness, metallic );
    brdf._normal            = normal;
    brdf._toView            = View::computeToView( worldPosition );
    brdf._normalDotView     = max( saturate( dot( normal, brdf._toView ) ), 1e-4f );
    brdf._lightAcceptance   = 0.0f;
    brdf._coatWeight        = 0.0f;
    brdf._coatAlpha         = brdf._pbr._alpha;
    brdf._sheenColor        = float3( 0.0f, 0.0f, 0.0f );
    brdf._sheenAlpha        = 1.0f;
    brdf._sheenWeight       = 0.0f;
    brdf._subsurfaceWrap    = float3( 0.0f, 0.0f, 0.0f );
    brdf._transmissionColor = float3( 0.0f, 0.0f, 0.0f );
    brdf._transmissionWrap  = 0.0f;
    // 비등방이 꺼진 값 — 노멀에 수직인 아무 탄젠트, 두 α 가 같으면 등방 GGX 와 같다(ShadingMathTest).
    const float3 helper  = abs( normal.y ) < 0.99f ? float3( 0.0f, 1.0f, 0.0f ) : float3( 1.0f, 0.0f, 0.0f );
    brdf._tangent        = normalize( cross( helper, normal ) );
    brdf._bitangent      = cross( normal, brdf._tangent );
    brdf._alphaTangent   = brdf._pbr._alpha;
    brdf._alphaBitangent = brdf._pbr._alpha;
    brdf._strandTangent  = brdf._tangent;
    brdf._strandColor    = float3( 0.0f, 0.0f, 0.0f );
    brdf._strandExponent = 1.0f;
    return brdf;
}

void SurfaceBRDF::setCoat( float weight, float roughness )
{
    _coatWeight         = saturate( weight );
    const float clamped = max( saturate( roughness ), kMinRoughness );
    _coatAlpha          = clamped * clamped;
}

void SurfaceBRDF::setSheen( float3 color, float roughness, float weight )
{
    _sheenColor  = color;
    _sheenAlpha  = max( saturate( roughness ), 0.07f ); // 시엔 α — Charlie 는 거칠기를 그대로 α 로 쓴다(필라멘트와 같다)
    _sheenWeight = saturate( weight );
}

void SurfaceBRDF::setSubsurface( float3 wrap )
{
    _subsurfaceWrap  = max( wrap, float3( 0.0f, 0.0f, 0.0f ) );
    _lightAcceptance = max( _lightAcceptance, max( _subsurfaceWrap.r, max( _subsurfaceWrap.g, _subsurfaceWrap.b ) ) );
}

void SurfaceBRDF::setTransmission( float3 color, float wrap )
{
    _transmissionColor = color;
    _transmissionWrap  = max( wrap, 0.0f );
    _lightAcceptance   = 1.0f; // 뒤에서 오는 빛 모두
}

void SurfaceBRDF::setAnisotropy( float3 tangent, float anisotropy )
{
    // 탄젠트를 노멀에 다시 직교화한다(노멀 맵 뒤의 노멀).
    const float3 orthogonal = tangent - _normal * dot( _normal, tangent );
    const float  lengthSq   = dot( orthogonal, orthogonal );
    _tangent                = lengthSq > 1e-8f ? orthogonal * rsqrt( lengthSq ) : _tangent;
    _bitangent              = cross( _normal, _tangent );
    _alphaTangent           = ShadingMath::computeAlphaTangent( _pbr._alpha, clamp( anisotropy, -0.95f, 0.95f ) );
    _alphaBitangent         = ShadingMath::computeAlphaBitangent( _pbr._alpha, clamp( anisotropy, -0.95f, 0.95f ) );
}

/**
 * @brief 박막 — 시선 각에서 잰 에어리 반사율로 F0 를 바꾼다.
 * @details shortcut: 빛마다 V·H 로 재지 않고 N·V 한 번으로 잰 색을 F0 로 쓴다(그 위에 슐릭이 한 번 더 걸려 스치는 각이 조금 밝다). 빛이 여럿인 장면에서 무지개가 어긋나 보이면 빛마다 잰다.
 */
void SurfaceBRDF::setThinFilm( float thicknessNm, float filmIor, float baseIor, float weight )
{
    const float3 film   = float3( ShadingMath::computeThinFilmReflectance( _normalDotView, thicknessNm, filmIor, baseIor, kThinFilmWavelengthNm.r ),
                                  ShadingMath::computeThinFilmReflectance( _normalDotView, thicknessNm, filmIor, baseIor, kThinFilmWavelengthNm.g ),
                                  ShadingMath::computeThinFilmReflectance( _normalDotView, thicknessNm, filmIor, baseIor, kThinFilmWavelengthNm.b ) );
    _pbr._specularColor = lerp( _pbr._specularColor, film, saturate( weight ) );
}

void SurfaceBRDF::setStrand( float3 tangent, float shift, float roughness, float3 color )
{
    // 비늘 모양 털 끝이 뿌리 쪽으로 기울어 하이라이트가 옮겨 간다(Scheuermann 2004 의 탄젠트 옮기기).
    _strandTangent  = normalize( tangent + _normal * shift );
    _strandExponent = ShadingMath::computeStrandExponent( roughness );
    _strandColor    = color;
}

bool SurfaceBRDF::receivesLight( float normalDotLight )
{
    return normalDotLight > -_lightAcceptance;
}

// ------------------------------------------------------------------------------
// 엽 정의 — define 이 고른다. 꺼진 엽은 기본 PBR 의 값이나 0 을 낸다.
// ------------------------------------------------------------------------------
#if defined( MATERIAL_LOBE_SUBSURFACE )
/** @brief 피부 — 채널마다 감싼 확산. 붉은 채널이 가장 멀리 번져 그늘 경계가 붉다(Penner 2011 의 미리 적분한 피부를 감싼 확산으로 근사). */
float3 SurfaceBRDF::computeDiffuse( float normalDotLight )
{
    const float3 wrapped = float3( ShadingMath::computeWrappedDiffuse( normalDotLight, _subsurfaceWrap.r ), ShadingMath::computeWrappedDiffuse( normalDotLight, _subsurfaceWrap.g ),
                                   ShadingMath::computeWrappedDiffuse( normalDotLight, _subsurfaceWrap.b ) );
    return _pbr._diffuseColor * wrapped;
}
#else
float3 SurfaceBRDF::computeDiffuse( float normalDotLight )
{
    return _pbr._diffuseColor * saturate( normalDotLight );
}
#endif

#if defined( MATERIAL_LOBE_ANISOTROPY )
/** @brief 비등방 GGX — 비단 · 공단 · 솔질 금속의 길게 끌린 하이라이트. */
float3 SurfaceBRDF::computeBaseSpecular( float3 toLight, float3 halfway, float normalDotLight, float normalDotHalf, float viewDotHalf )
{
    const float  distributionTimesPi = ShadingMath::computeDistributionAnisotropicGGXTimesPi( normalDotHalf, dot( _tangent, halfway ), dot( _bitangent, halfway ), _alphaTangent, _alphaBitangent );
    const float  visibility          = ShadingMath::computeVisibilityAnisotropicGGX( normalDotLight, _normalDotView, dot( _tangent, _toView ), dot( _bitangent, _toView ), dot( _tangent, toLight ),
                                                                                     dot( _bitangent, toLight ), _alphaTangent, _alphaBitangent );
    const float3 fresnel             = _pbr._specularColor + ( 1.0f - _pbr._specularColor ) * ShadingMath::computeSchlickWeight( viewDotHalf );
    return distributionTimesPi * visibility * fresnel * normalDotLight;
}
#else
float3 SurfaceBRDF::computeBaseSpecular( float3 toLight, float3 halfway, float normalDotLight, float normalDotHalf, float viewDotHalf )
{
    return _pbr.evaluateSpecular( _normal, _toView, toLight, normalDotLight, _normalDotView );
}
#endif

#if defined( MATERIAL_LOBE_SHEEN )
/** @brief 천 시엔 — Charlie 분포 × Neubelt 가시성. 프레넬은 시엔 색이 대신한다(천의 잔털은 스치는 각에서 밝다 — 분포가 그 모양이다). */
float3 SurfaceBRDF::computeSheenSpecular( float normalDotLight, float normalDotHalf )
{
    return _sheenColor * ShadingMath::computeDistributionCharlieTimesPi( normalDotHalf, _sheenAlpha ) * ShadingMath::computeVisibilityNeubelt( normalDotLight, _normalDotView ) *
           normalDotLight;
}
#else
float3 SurfaceBRDF::computeSheenSpecular( float normalDotLight, float normalDotHalf )
{
    return float3( 0.0f, 0.0f, 0.0f );
}
#endif

#if defined( MATERIAL_LOBE_TRANSMISSION )
/** @brief 얇은 면의 투과 — 뒤에서 오는 빛이 투과색으로 비친다(커튼 · 잎 · 귀). */
float3 SurfaceBRDF::computeTransmission( float normalDotLight )
{
    return _transmissionColor * ShadingMath::computeDiffuseTransmission( normalDotLight, _transmissionWrap );
}
#else
float3 SurfaceBRDF::computeTransmission( float normalDotLight )
{
    return float3( 0.0f, 0.0f, 0.0f );
}
#endif

#if defined( MATERIAL_LOBE_STRAND )
/** @brief 털 — Kajiya · Kay 반사. 빛을 받는 쪽만(N·L). */
float3 SurfaceBRDF::computeStrandSpecular( float3 halfway, float normalDotLight )
{
    return _strandColor * ShadingMath::computeStrandSpecular( dot( _strandTangent, halfway ), _strandExponent ) * normalDotLight;
}
#else
float3 SurfaceBRDF::computeStrandSpecular( float3 halfway, float normalDotLight )
{
    return float3( 0.0f, 0.0f, 0.0f );
}
#endif

#if defined( MATERIAL_LOBE_COAT )
/** @brief 아래층이 코트를 지나며 잃는 몫 — 시선 쪽 프레넬 한 번(값싼 판, 언리얼 Clear Coat 도 같은 생각). */
float SurfaceBRDF::computeCoatAttenuation()
{
    return 1.0f - _coatWeight * ShadingMath::computeFresnelSchlick( kCoatF0, _normalDotView );
}

/** @brief 코트 — 아래층을 감쇠하고 코트 자신의 GGX 반사를 더한다(피지 · 립글로스 · 니스 · 젖은 면). 코트 노멀은 표면 노멀이다. */
float3 SurfaceBRDF::applyCoat( float3 under, float normalDotLight, float normalDotHalf, float viewDotHalf )
{
    const float coat = ShadingMath::computeDistributionGGXTimesPi( normalDotHalf, _coatAlpha ) * ShadingMath::computeVisibilitySmithGGX( normalDotLight, _normalDotView, _coatAlpha ) *
                       ShadingMath::computeFresnelSchlick( kCoatF0, viewDotHalf ) * normalDotLight;
    return under * computeCoatAttenuation() + coat * _coatWeight;
}
#else
float SurfaceBRDF::computeCoatAttenuation()
{
    return 1.0f;
}

float3 SurfaceBRDF::applyCoat( float3 under, float normalDotLight, float normalDotHalf, float viewDotHalf )
{
    return under;
}
#endif

/** @brief 빛 하나의 응답 — 확산(피부 감쌈) + lerp( 반사, 시엔, 천 무게 ) + 머리결, 그 위를 코트가 덮고, 뒤에서 오는 빛은 투과로 더한다. */
float3 SurfaceBRDF::evaluateLight( float3 toLight, float normalDotLight )
{
    const float  clampedNormalDotLight = saturate( normalDotLight );
    const float3 halfway               = normalize( _toView + toLight );
    const float  normalDotHalf         = saturate( dot( _normal, halfway ) );
    const float  viewDotHalf           = saturate( dot( _toView, halfway ) );
    const float3 specular              = lerp( computeBaseSpecular( toLight, halfway, clampedNormalDotLight, normalDotHalf, viewDotHalf ),
                                               computeSheenSpecular( clampedNormalDotLight, normalDotHalf ), _sheenWeight );
    const float3 under                 = computeDiffuse( normalDotLight ) + specular + computeStrandSpecular( halfway, clampedNormalDotLight );
    return applyCoat( under, clampedNormalDotLight, normalDotHalf, viewDotHalf ) + computeTransmission( normalDotLight );
}

float3 SurfaceBRDF::getAmbientDiffuseColor()
{
    // 투과 면은 뒤쪽 하늘빛도 받는다 — 앰비언트에 투과색의 절반을 더한다(반구 평균의 값싼 판).
    return ( _pbr._diffuseColor + _transmissionColor * 0.5f ) * computeCoatAttenuation();
}

float3 SurfaceBRDF::computeEnvironmentSpecular()
{
    // 천은 반사 비율을 시엔 색으로 옮긴다(시엔의 반구 평균 ≈ 색 × 0.25, 값싼 판). 코트는 자기 프레넬만큼 하늘을 더 비춘다.
    const float3 base = lerp( _pbr.computeEnvironmentSpecular( _normalDotView ), _sheenColor * 0.25f, _sheenWeight );
    return base * computeCoatAttenuation() + _coatWeight * ShadingMath::computeFresnelSchlick( kCoatF0, _normalDotView );
}

float SurfaceBRDF::getReflectionRoughness()
{
    // 코트가 덮으면 반사 프로브는 코트의 거칠기로 고른다(매끈한 기름막이 하늘을 또렷이 비춘다).
    return lerp( _pbr._roughness, sqrt( _coatAlpha ), _coatWeight );
}

#endif // SW_ENGINE_SHADINGMODEL_HLSLI
