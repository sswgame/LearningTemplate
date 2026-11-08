/**
 * material_displacement.hlsli — 머티리얼 변위(MATERIAL_DISPLACEMENT)의 정점 · 헐 · 도메인 진입점(VSMain · HSMain · DSMain).
 *
 * material_shader_main.hlsli 가 MATERIAL_DISPLACEMENT 일 때 보간 칸 VSMain 대신 이 파일을 include 한다. 렌더러가 그 머티리얼의 변형 PSO 에 헐 · 도메인 진입점과
 * 패치 토폴로지를 붙인다(`FrameRendererUtil::tessellatesMaterial` — 본 · G버퍼 패스와 머티리얼 셰이더로 갈아탄 그림자 · 깊이 프리패스).
 *
 *   정점   문맥은 보간 칸 VSMain 과 같은 함수(`VertexContext::load`)로 만들고 위치 오프셋 훅까지 더한 월드 위치를 제어점으로 낸다(클립 위치는 아직 없다).
 *   헐     변 계수를 변의 두 끝점(월드 위치 · 카메라 뷰 깊이)만으로 정한다 — 변을 나누는 두 패치가 같은 값을 내어 틈이 없다. 안쪽은 세 변의 최댓값이다.
 *   도메인 무게 중심으로 보간하고 높이 맵을 읽어 노멀 방향으로 옮긴 뒤 클립 위치를 만든다(그림자는 빛의 뷰-투영 — View::getViewProjection).
 *
 * 머티리얼 칸(셰이더의 SW_MATERIAL 구조체에 이 이름이 있어야 한다 — 없으면 컴파일이 멈춘다):
 *   _displacementMap(텍스처 번호, R 채널 높이 0 ~ 1) · _displacementScale(m) · _displacementMidLevel(변위 0 인 높이) · _tessellationDensity(각 1 rad 당 분할 수) ·
 *   _tessellationMaxFactor(변 계수 상한, 64 이하)
 * 식의 C++ 짝은 `MaterialDisplacement`(MaterialDisplacement.h)이고 단위 테스트가 성질을 지킨다 — 한쪽을 고치면 다른 쪽도 고친다.
 *
 * 뷰 깊이는 늘 카메라의 뷰-투영(g_ViewProj)에서 읽는다 — 그림자 패스도 같은 값이라 그림자와 본 패스가 같은 계수 · 같은 변위로 같은 윤곽을 그린다.
 * 셰이딩 노멀은 메시 노멀(과 노멀 맵)이다 — shortcut: 변위가 만든 기울기로 노멀을 다시 구하지 않는다. 높이 맵과 짝인 노멀 맵을 쓴다(언리얼 변위도 노멀 맵을 함께 쓴다).
 * shortcut: UV 이음매에서는 양쪽이 다른 높이를 읽어 틈이 날 수 있다. 이음매 없는 UV 나 이음매에서 같은 높이인 맵을 쓴다(언리얼 Nanite 변위도 같은 제약, 기억에 의존).
 * 헐 · 도메인은 머티리얼 버퍼를 읽는다 — OpenGL 은 그 링크를 거절해(binding.hlsli 9 절) 이 변형을 만들지 않는다(`RHICapabilities::_bMaterialTessellation`).
 */

#ifndef SW_ENGINE_MATERIALDISPLACEMENT_HLSLI
#define SW_ENGINE_MATERIALDISPLACEMENT_HLSLI

static const float kDisplacementMaxTessFactor = 64.0f; // MaterialDisplacement::kMaxTessFactor 와 같다
static const float kDisplacementMinViewDepth  = 0.05f; // MaterialDisplacement::kMinViewDepth 와 같다

/**
 * @brief 정점 → 헐 → 도메인 제어점. 위치는 SV_POSITION 이 아니라 일반 값이다(내장 위치는 GL 이 패치 사이에서 잃는다 — optional_stages.hlsl).
 *        나누는 값(머티리얼 · 인스턴스 · 등장)은 보간하지 않고 첫 제어점의 값을 쓴다 — 한 드로우의 패치는 같은 인스턴스다.
 */
struct MaterialControlPoint
{
    float3 _worldPosition : TEXCOORD2; ///< 위치 오프셋 훅까지 더한 월드 위치(변위 전)
    float3 _normal : TEXCOORD1;
    float2 _uv : TEXCOORD0;
    float4 _color : COLOR;
    float4 _tangent : TEXCOORD3;   ///< 탄젠트를 쓰지 않는 셰이더는 0
    float  _viewDepth : TEXCOORD6; ///< 카메라 뷰 깊이(카메라 뷰-투영의 w) — 변 계수만 읽는다
    uint   _materialIndex : TEXCOORD4;
    uint   _instanceSlot : TEXCOORD5;
    float  _appearance : TEXCOORD15;
};

/** @brief 헐의 패치 상수 — 변 i 는 꼭짓점 i 의 맞은편(SV_DomainLocation 의 i 성분이 0 인 변)이다. */
struct DisplacementPatchConstants
{
    float _arrEdge[3] : SV_TessFactor;
    float _inside : SV_InsideTessFactor;
};

/** @brief 변위의 식(C++ 짝 MaterialDisplacement). */
struct Displacement
{
    static float computeEdgeFactor( float3 position0, float3 position1, float viewDepth0, float viewDepth1, float density, float maxFactor );
    static float computeOffset( float height, float scale, float midLevel );
};

/** @brief 변 하나의 계수 — 월드 길이 / 평균 뷰 깊이(각 크기) × 밀도를 [1, 상한] 으로. 두 끝점만 읽고 순서를 바꿔도 같은 값이다. */
static float Displacement::computeEdgeFactor( float3 position0, float3 position1, float viewDepth0, float viewDepth1, float density, float maxFactor )
{
    const float edgeLength = length( position1 - position0 );
    const float viewDepth  = max( ( viewDepth0 + viewDepth1 ) * 0.5f, kDisplacementMinViewDepth );
    const float upper      = clamp( maxFactor, 1.0f, kDisplacementMaxTessFactor );
    return clamp( edgeLength / viewDepth * density, 1.0f, upper );
}

/** @brief 노멀 방향 변위 길이 — 기준 높이에서 0. */
static float Displacement::computeOffset( float height, float scale, float midLevel )
{
    return ( height - midLevel ) * scale;
}

// ------------------------------------------------------------------------------
// 1) 정점 — 제어점을 낸다
// ------------------------------------------------------------------------------
#if defined( SW_MATERIAL_USES_TANGENT )
/** @brief 탄젠트는 월드 행렬 그대로 곱한다(보간 칸 storeTangent 와 같다). */
float4 computeControlPointTangent( VertexContext context )
{
    const float4 localTangent = MorphPool::loadTangent( context._instance._meshBatchIndex, context._instance._skinSlot, context._vertexID, context._input._tangent );
    return float4( mul( float4( localTangent.xyz, 0.0f ), context._instance._world ).xyz, localTangent.w );
}
#else
float4 computeControlPointTangent( VertexContext context )
{
    return float4( 0.0f, 0.0f, 0.0f, 0.0f );
}
#endif

MaterialControlPoint VSMain( MaterialVertexInput input, SW_INSTANCE_SLOT_INPUT( instanceSlot ), uint vertexID : SV_VertexID )
{
    MaterialControlPoint output;
    VertexContext        context;
    float4               worldPosition;
    context.load( input, instanceSlot, vertexID, worldPosition );
    worldPosition         = context.applyWorldPositionOffset( worldPosition );
    output._worldPosition = worldPosition.xyz;
    output._normal        = context._worldNormal;
    output._uv            = context._input._uv;
    output._color         = context._input._color;
    output._tangent       = computeControlPointTangent( context );
    output._viewDepth     = mul( worldPosition, g_ViewProj ).w;
    output._materialIndex = context._instance._materialIndex;
    output._instanceSlot  = instanceSlot;
    output._appearance    = context._instance._appearance;
    return output;
}

// ------------------------------------------------------------------------------
// 2) 헐 — 변 계수(틈 없음)
// ------------------------------------------------------------------------------
DisplacementPatchConstants DisplacementPatchConstantMain( InputPatch<MaterialControlPoint, 3> patch )
{
    const MaterialData         material  = SW_MATERIAL( patch[0]._materialIndex );
    const float                density   = material._tessellationDensity;
    const float                maxFactor = material._tessellationMaxFactor;
    DisplacementPatchConstants constants;
    constants._arrEdge[0] = Displacement::computeEdgeFactor( patch[1]._worldPosition, patch[2]._worldPosition, patch[1]._viewDepth, patch[2]._viewDepth, density, maxFactor );
    constants._arrEdge[1] = Displacement::computeEdgeFactor( patch[2]._worldPosition, patch[0]._worldPosition, patch[2]._viewDepth, patch[0]._viewDepth, density, maxFactor );
    constants._arrEdge[2] = Displacement::computeEdgeFactor( patch[0]._worldPosition, patch[1]._worldPosition, patch[0]._viewDepth, patch[1]._viewDepth, density, maxFactor );
    constants._inside     = max( max( constants._arrEdge[0], constants._arrEdge[1] ), constants._arrEdge[2] );
    return constants;
}

// 출력 감김은 입력 패치와 같은 시계 방향이다 — 엔진의 앞면은 시계 방향(RHIFrontFace::Clockwise 기본)이라 뒷면 컬링이 그대로 맞는다.
// fractional_odd: 거리에 따라 계수가 바뀔 때 새 정점이 한 번에 튀지 않고 미끄러져 나온다(언리얼 테셀레이션 기본과 같다, 기억에 의존).
[domain( "tri" )]
    [partitioning( "fractional_odd" )]
    [outputtopology( "triangle_cw" )]
    [outputcontrolpoints( 3 )]
    [patchconstantfunc( "DisplacementPatchConstantMain" )]
    [maxtessfactor( 64.0f )] MaterialControlPoint
    HSMain( InputPatch<MaterialControlPoint, 3> patch, uint pointIndex : SV_OutputControlPointID )
{
    return patch[pointIndex];
}

// ------------------------------------------------------------------------------
// 3) 도메인 — 보간 → 변위 → 클립 위치 → 보간 칸
// ------------------------------------------------------------------------------
/** @brief 세 제어점을 무게 중심으로 섞는다. 나누는 값은 첫 제어점의 것이다. */
MaterialControlPoint interpolateControlPoint( MaterialControlPoint point0, MaterialControlPoint point1, MaterialControlPoint point2, float3 barycentric )
{
    MaterialControlPoint point;
    point._worldPosition = point0._worldPosition * barycentric.x + point1._worldPosition * barycentric.y + point2._worldPosition * barycentric.z;
    point._normal        = point0._normal * barycentric.x + point1._normal * barycentric.y + point2._normal * barycentric.z;
    point._uv            = point0._uv * barycentric.x + point1._uv * barycentric.y + point2._uv * barycentric.z;
    point._color         = point0._color * barycentric.x + point1._color * barycentric.y + point2._color * barycentric.z;
    point._tangent       = float4( point0._tangent.xyz * barycentric.x + point1._tangent.xyz * barycentric.y + point2._tangent.xyz * barycentric.z, point0._tangent.w );
    point._viewDepth     = point0._viewDepth * barycentric.x + point1._viewDepth * barycentric.y + point2._viewDepth * barycentric.z;
    point._materialIndex = point0._materialIndex;
    point._instanceSlot  = point0._instanceSlot;
    point._appearance    = point0._appearance;
    return point;
}

#if defined( SW_MATERIAL_DEPTH_ONLY )
void storeDisplacedVaryings( inout MaterialInterpolants output, MaterialControlPoint point, float3 worldPosition )
{
    // 깊이만 쓴다 — 위치 말고 넘길 칸이 없다.
}
#else
    #if defined( SW_MATERIAL_USES_TANGENT )
void storeDisplacedTangent( inout MaterialInterpolants output, MaterialControlPoint point )
{
    output._tangent = point._tangent;
}
    #else
void storeDisplacedTangent( inout MaterialInterpolants output, MaterialControlPoint point )
{
}
    #endif

    #if !defined( SW_MATERIAL_READ_IN_VERTEX )
void storeDisplacedMaterialIndex( inout MaterialInterpolants output, MaterialControlPoint point )
{
    output._materialIndex = point._materialIndex;
}
    #else
void storeDisplacedMaterialIndex( inout MaterialInterpolants output, MaterialControlPoint point )
{
}
    #endif

void storeDisplacedVaryings( inout MaterialInterpolants output, MaterialControlPoint point, float3 worldPosition )
{
    output._worldPosition = worldPosition;
    output._color         = point._color;
    output._uv            = point._uv;
    output._normal        = point._normal;
    storeDisplacedTangent( output, point );
    storeDisplacedMaterialIndex( output, point );
}
#endif // SW_MATERIAL_DEPTH_ONLY

[domain( "tri" )] MaterialInterpolants DSMain( DisplacementPatchConstants constants, float3 barycentric : SV_DomainLocation, const OutputPatch<MaterialControlPoint, 3> patch )
{
    const MaterialControlPoint point    = interpolateControlPoint( patch[0], patch[1], patch[2], barycentric );
    const MaterialData         material = SW_MATERIAL( point._materialIndex );
    // 밉 0 을 읽는다 — 도메인 스테이지에는 화면 미분이 없다. shortcut: 먼 표면도 밉 0 이라 높이 맵이 크면 읽기가 흩어진다. 뷰 깊이로 밉을 고르면 그때 고친다.
    const float  height        = MaterialTexture::sampleLevelZero( material._displacementMap, point._uv ).r;
    const float  offset        = Displacement::computeOffset( height, material._displacementScale, material._displacementMidLevel );
    const float4 worldPosition = float4( point._worldPosition + normalize( point._normal ) * offset, 1.0f );

    MaterialInterpolants output;
    output._position = Transform::computeClipPosition( worldPosition, View::getViewProjection() );
    storeDisplacedVaryings( output, point, worldPosition.xyz );
    SW_APPEARANCE_WRITE( output, point )
    SW_LODDEBUG_WRITE( output, point._instanceSlot )
    return output;
}

#endif // SW_ENGINE_MATERIALDISPLACEMENT_HLSLI
