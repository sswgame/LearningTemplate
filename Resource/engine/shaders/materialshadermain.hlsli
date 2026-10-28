/**
 * materialshadermain.hlsli — 머티리얼 셰이더 뼈대의 2단: 정점 · 픽셀 진입점(VSMain · PSMain).
 *
 * 셰이더 파일이 훅을 정의한 **뒤에** include 한다(순서는 materialshader.hlsli 머리말). 훅의 형식 · 선택 규칙은 거기에 있다.
 *
 * 패스마다 다른 점은 이 파일 한 곳에 있다 — 셰이더 파일은 패스를 가르는 `#if` 를 쓰지 않는다.
 *   본 패스(포워드)   `SurfaceOutput` 하나: 표면 훅 → 조명 훅 → 보기 모드 → SurfaceOutput::store
 *   G버퍼(SW_PASS_GBUFFER)  조명 훅을 컴파일 아웃하고 표면만 적는다(셰이딩은 디퍼드 조명 패스가 한다)
 *   깊이(SW_PASS_SHADOW · SW_PASS_DEPTH_PREPASS)  위치만 만든다. 픽셀은 알파 컷오프 머티리얼만 오린다
 *   머티리얼 없는 사용자(SW_MATERIAL_NONE)  위치 오프셋 훅이 없다 — 이 셰이더가 같은 자리를 못 만드는 배치는 깊이 패스에서 한 점으로 모아 뺀다
 * 클립 위치는 정점 단계의 한 곳에서만 만든다(`Transform::computeWorldPosition` → 훅의 오프셋 → `Transform::computeClipPosition`). 그림자는 빛 행렬, 나머지는 카메라 행렬이다
 * (`View::getViewProjection`).
 *
 * **진입점 본문에는 `#if` 가 없다.** 패스 · 기능 스위치마다 달라지는 단계는 멤버 함수 하나씩이고, 파일 수준의 `#if` 가 그 정의 하나를 고른다
 * (스위치가 꺼지면 아무것도 하지 않는 정의). 새 스위치는 단계 함수를 더하고 진입점에서 부른다 — 진입점 본문에 `#if` 를 들이지 않는다.
 *
 * 출구가 하나다 — 진입점과 도우미 모두 마지막에 한 번만 반환한다.
 */

#ifndef SW_ENGINE_MATERIALSHADERMAIN_HLSLI
#define SW_ENGINE_MATERIALSHADERMAIN_HLSLI

// ------------------------------------------------------------------------------
// 1) 정점 단계
// ------------------------------------------------------------------------------
#if defined( SW_MATERIAL_READ_IN_VERTEX )
void VertexContext::readMaterial()
{
    _material = SW_MATERIAL( _instance._materialIndex );
}
#else
void VertexContext::readMaterial()
{
    // 머티리얼은 픽셀 단계에서 읽는다(2 절 PixelContext::readMaterial).
}
#endif

#if defined( MATERIAL_VERTEX_DEFORM ) && !defined( SW_MATERIAL_NONE )
/** @brief 정점 셰이더가 정점을 옮기는 머티리얼(바람 · 파도) — 훅에서 오프셋만 받고 위치는 여기서 만든다. */
float4 VertexContext::applyWorldPositionOffset( float4 worldPosition )
{
    worldPosition.xyz += computeWorldPositionOffset( this );
    return worldPosition;
}
#else
float4 VertexContext::applyWorldPositionOffset( float4 worldPosition )
{
    return worldPosition;
}
#endif

#if defined( SW_PASS_MESH_OUTLINE )
/** @brief 외곽선 패스 — 외곽선 훅의 월드 오프셋을 위치 오프셋 뒤에 더하고, 클립 위치를 만든 뒤 화면 두께를 더한다. */
float4 VertexContext::computeClipPosition( inout float4 inoutWorldPosition )
{
    inoutWorldPosition          = applyWorldPositionOffset( inoutWorldPosition );
    const OutlineOffset outline = computeMaterialOutline( this );
    inoutWorldPosition.xyz += outline._worldOffset;
    float4 clipPosition = Transform::computeClipPosition( inoutWorldPosition, View::getViewProjection() );
    clipPosition.xy += outline._clipOffset;
    return clipPosition;
}
#else
float4 VertexContext::computeClipPosition( inout float4 inoutWorldPosition )
{
    inoutWorldPosition = applyWorldPositionOffset( inoutWorldPosition );
    return Transform::computeClipPosition( inoutWorldPosition, View::getViewProjection() );
}
#endif

// 깊이 패스에서 이 셰이더가 같은 자리를 못 만드는 배치 — 클립 공간 밖 한 점으로 모아 아무것도 그리지 않는다(드로우는 그대로 나가지만 래스터가 없다).
// 조건식 안의 `defined` 조합은 컴파일러마다 다르므로(FXC) 두 경우를 따로 적는다.
#if defined( SW_MATERIAL_NONE ) && defined( SW_PASS_DEPTH_PREPASS ) && defined( MATERIAL_VERTEX_DEFORM )
/**
 * @brief 정점을 옮기는 머티리얼(바람 · 파도)의 깊이 프리패스 — 이 셰이더는 옮긴 자리를 못 만든다. 남기면 기본 패스의 옮긴 정점이 프리패스 깊이에 가려 구멍이 난다.
 *        그 머티리얼의 그림자는 머티리얼 셰이더가 그린다.
 */
static float4 VertexContext::excludeFromDepthPass( float4 clipPosition )
{
    return float4( 0.0f, 0.0f, -1.0f, 1.0f );
}
#elif defined( SW_MATERIAL_NONE ) && defined( SW_PASS_SHADOW ) && defined( MATERIAL_SHADOW_CAST_OFF )
/** @brief 그림자를 드리우지 않는 머티리얼(풀) — 같은 방식으로 뺀다. */
static float4 VertexContext::excludeFromDepthPass( float4 clipPosition )
{
    return float4( 0.0f, 0.0f, -1.0f, 1.0f );
}
#else
static float4 VertexContext::excludeFromDepthPass( float4 clipPosition )
{
    return clipPosition;
}
#endif

#if defined( SW_MATERIAL_DEPTH_ONLY )
void MaterialInterpolants::storeVaryings( VertexContext context, float4 vertexWorldPosition, float4 vertexClipPosition )
{
    // 깊이만 쓴다 — 위치 말고 넘길 칸이 없다(그림자 맵은 패스마다 모든 메시를 그리므로 쓰이지 않는 칸을 내보내지 않는다).
}
#else
    #if defined( SW_MATERIAL_USES_TANGENT )
/** @brief 탄젠트는 표면을 따라가는 방향이라 노멀(여인수 행렬)과 달리 월드 행렬 그대로 곱한다. 스킨 · 모프 메시는 컴퓨트가 돌린 탄젠트다. */
void MaterialInterpolants::storeTangent( VertexContext context )
{
    const float4 localTangent = MorphPool::loadTangent( context._instance._meshBatchIndex, context._vertexId, context._input._tangent );
    _tangent                  = float4( mul( float4( localTangent.xyz, 0.0f ), context._instance._world ).xyz, localTangent.w );
}
    #else
void MaterialInterpolants::storeTangent( VertexContext context )
{
}
    #endif

    #if !defined( SW_MATERIAL_READ_IN_VERTEX ) && !defined( SW_MATERIAL_NONE )
void MaterialInterpolants::storeMaterialIndex( VertexContext context )
{
    _materialIndex = context._instance._materialIndex;
}
    #else
void MaterialInterpolants::storeMaterialIndex( VertexContext context )
{
    // 머티리얼을 정점에서 읽거나(사용자 보간 칸으로 넘긴다) 머티리얼이 없다.
}
    #endif

    #if defined( SW_MATERIAL_CUSTOM_VARYINGS )
void MaterialInterpolants::storeUserVaryings( VertexContext context, float4 vertexClipPosition )
{
    context._clipPosition = vertexClipPosition;
    _user                 = computeMaterialVaryings( context );
}
    #else
void MaterialInterpolants::storeUserVaryings( VertexContext context, float4 vertexClipPosition )
{
}
    #endif

void MaterialInterpolants::storeVaryings( VertexContext context, float4 vertexWorldPosition, float4 vertexClipPosition )
{
    _worldPosition = vertexWorldPosition.xyz;
    _color         = context._input._color;
    // UV 는 정점 속성이다. 위치로 지어내면(`position.xy * 0.5 + 0.5`) 원점 중심 단위 도형이 아닐 때 텍스처가 엉뚱하게 붙는다.
    _uv     = context._input._uv;
    _normal = context._worldNormal;
    storeTangent( context );
    storeMaterialIndex( context );
    storeUserVaryings( context, vertexClipPosition );
}
#endif // SW_MATERIAL_DEPTH_ONLY

MaterialInterpolants VSMain( VertexInput input, uint vertexId : SV_VertexID )
{
    MaterialInterpolants output;
    VertexContext        context;
    context._input    = input;
    context._vertexId = vertexId;
    // 인스턴스는 슬롯 스트림이 준 자리에서, 배치는 인스턴스에서 — 모프 풀 시작 · 정점 풀 시작이 배치 표에 있다.
    context._instance = InstanceData::load( input._instanceSlot );
    // GPU 가 변형한 정점(VAT · 모프 · 스킨)이 있으면 그걸 쓴다(아니면 입력 스트림 그대로). 위치와 노멀이 같이 온다.
    context._instance.loadAnimatedVertex( vertexId, input._position, input._normal, context._localPosition, context._localNormal );
    // 위치는 깊이 프리패스 · 그림자와 **같은 함수**로 만든다 — 프리패스가 쓴 깊이와 비트까지 같아야 본 패스의 LessEqual 을 통과한다(binding.hlsli 7 절).
    float4 worldPosition   = Transform::computeWorldPosition( context._localPosition, context._instance._world );
    context._worldPosition = worldPosition.xyz;
    // 월드 노멀은 월드 행렬이 아니라 그 3x3 의 여인수 행렬로 옮긴다(binding.hlsli Transform::computeWorldNormal) — 비균등 스케일에서도 조명이 맞는다.
    context._worldNormal  = Transform::computeWorldNormal( context._localNormal, context._instance._world );
    context._clipPosition = float4( 0.0f, 0.0f, 0.0f, 0.0f );
    context.readMaterial();
    const float4 clipPosition = VertexContext::excludeFromDepthPass( context.computeClipPosition( worldPosition ) );
    output._position          = clipPosition;
    output.storeVaryings( context, worldPosition, clipPosition );
    SW_APPEARANCE_WRITE( output, context._instance )
    SW_LODDEBUG_WRITE( output, input._instanceSlot )
    return output;
}

// ------------------------------------------------------------------------------
// 2) 픽셀 단계
// ------------------------------------------------------------------------------
#if !defined( SW_MATERIAL_DEPTH_ONLY )
    #if defined( MATERIAL_TWO_SIDED )
/** @brief 양면 머티리얼의 뒷면은 노멀이 반대다 — 뒤집지 않으면 안쪽이 바깥 빛으로 칠해진다. */
static float3 PixelContext::faceNormal( float3 normal, bool bFrontFace )
{
    return bFrontFace ? normal : -normal;
}
    #else
static float3 PixelContext::faceNormal( float3 normal, bool bFrontFace )
{
    return normal;
}
    #endif

    #if defined( SW_MATERIAL_USES_TANGENT )
void PixelContext::copyTangent( MaterialInterpolants input )
{
    _tangent = input._tangent;
}
    #else
void PixelContext::copyTangent( MaterialInterpolants input )
{
}
    #endif

    #if !defined( SW_MATERIAL_READ_IN_VERTEX ) && !defined( SW_MATERIAL_NONE )
/** @brief 머티리얼은 인스턴스의 materialIndex 로 버퍼에서 읽는다 — 드로우별 바인딩 없음. 픽셀 단계에서만 읽는다(binding.hlsli 9 절). */
void PixelContext::readMaterial( MaterialInterpolants input )
{
    _material = SW_MATERIAL( input._materialIndex );
}
    #else
void PixelContext::readMaterial( MaterialInterpolants input )
{
    // 머티리얼을 정점에서 읽었거나(1 절 VertexContext::readMaterial) 머티리얼이 없다.
}
    #endif

/** @brief 보간된 값(과 픽셀에서 읽는 셰이더의 머티리얼)으로 픽셀 문맥을 만든다. */
static PixelContext PixelContext::make( MaterialInterpolants input, bool bFrontFace )
{
    PixelContext context;
    SW_LODDEBUG_READ( input )
    context._position      = input._position;
    context._color         = input._color;
    context._uv            = input._uv;
    context._normal        = PixelContext::faceNormal( normalize( input._normal ), bFrontFace );
    context._worldPosition = input._worldPosition;
    context.copyTangent( input );
    context._user = input._user;
    context.readMaterial( input );
    return context;
}

    // 조명 훅의 선택을 풀어 색을 낸다 — 훅 정의 · 기본 PBR · 조명 없음 중 셰이더가 고른 하나다.
    #if defined( SW_MATERIAL_CUSTOM_LIGHTING )
float4 PixelContext::computeSelectedLighting( MaterialSurface surface )
{
    return computeMaterialLighting( this, surface );
}
    #elif defined( SW_MATERIAL_UNLIT )
float4 PixelContext::computeSelectedLighting( MaterialSurface surface )
{
    return surface._albedo;
}
    #elif SW_VIEWMODE_SKIPS_LIGHTING
/** @brief 보기 모드 Unlit 등 — 조명 · 그림자를 컴파일 아웃하고 알베도 그대로. */
float4 PixelContext::computeSelectedLighting( MaterialSurface surface )
{
    return surface._albedo;
}
    #else
/** @brief 기본 PBR — 조명 식은 디퍼드와 **같은 함수**다(lighting.hlsli). 두 벌로 두면 두 경로의 그림이 갈라진다. */
float4 PixelContext::computeSelectedLighting( MaterialSurface surface )
{
    const float shadow = Shadow::sampleAtWorld( _worldPosition, surface._normal );
    return float4( Lighting::shadeLights( surface._albedo.rgb, _worldPosition, surface._normal, shadow, surface._roughness, surface._metallic ), surface._albedo.a );
}
    #endif
#endif // !SW_MATERIAL_DEPTH_ONLY

#if defined( MATERIAL_ALPHA_CUTOFF )
/** @brief 알파 컷오프 머티리얼 — 깊이 패스와 본 패스가 같은 표면 훅의 같은 알파 · 컷오프로 오려 같은 자리를 버린다. */
void MaterialSurface::applyAlphaCutoff()
{
    clip( _albedo.a - _alphaCutoff );
}
#else
void MaterialSurface::applyAlphaCutoff()
{
}
#endif

#if defined( SW_MATERIAL_DEPTH_PASS )

    #if defined( MATERIAL_ALPHA_CUTOFF )
/** @brief 깊이 패스의 픽셀 — 등장 디더와 알파 컷오프를 오린다(본 패스와 같은 표면 훅이라 같은 자리를 버린다). */
void MaterialInterpolants::shadeDepthPixel( bool bFrontFace )
{
    SW_APPEARANCE_CLIP( this )
    const PixelContext    context = PixelContext::make( this, bFrontFace );
    const MaterialSurface surface = computeMaterialSurface( context );
    surface.applyAlphaCutoff();
}
    #else
/** @brief 깊이 패스의 픽셀 — 등장 디더만 오린다(알파 컷오프가 아니면 표면을 읽지 않는다). */
void MaterialInterpolants::shadeDepthPixel( bool bFrontFace )
{
    SW_APPEARANCE_CLIP( this )
}
    #endif

void PSMain( MaterialInterpolants input )
{
    input.shadeDepthPixel( true );
}

#else

    /**
     * 표면을 패스가 원하는 출력으로 낸다. 반환 타입이 패스에 따라 갈린다(binding.hlsli 12 절 `SurfaceOutput`) — 포워드는 색 하나, G버퍼는 알베도 · 노멀 · 머티리얼 셋이다.
     * G버퍼 패스는 조명을 계산하지 않는다 — 조명 코드가 통째로 컴파일 아웃되므로 런타임 분기가 아니다.
     * 알파를 쓰는지는 퍼뮤테이션이 정한다: 반투명 머티리얼만 MATERIAL_BLEND_TRANSLUCENT 를 가져 불투명 변형에서 머티리얼 알파가 새지 않는다.
     */
    #if defined( SW_PASS_GBUFFER )
SurfaceOutput PixelContext::storeSurface( MaterialSurface surface )
{
    return SurfaceOutput::store( float4( 0.0f, 0.0f, 0.0f, 0.0f ), surface._albedo, surface._normal, _worldPosition, surface._roughness, surface._metallic );
}
    #elif defined( MATERIAL_BLEND_TRANSLUCENT )
SurfaceOutput PixelContext::storeSurface( MaterialSurface surface )
{
    return SurfaceOutput::store( computeSelectedLighting( surface ), surface._albedo, surface._normal, _worldPosition, surface._roughness, surface._metallic );
}
    #else
SurfaceOutput PixelContext::storeSurface( MaterialSurface surface )
{
    float4 lit = computeSelectedLighting( surface );
    lit.a      = 1.0f;
    return SurfaceOutput::store( lit, surface._albedo, surface._normal, _worldPosition, surface._roughness, surface._metallic );
}
    #endif

/** @brief 본 패스 · G버퍼 패스의 픽셀 — 표면 훅 → 알파 컷오프 → (G버퍼가 아니면) 조명 훅 → 출력. */
SurfaceOutput MaterialInterpolants::shadePixel( bool bFrontFace )
{
    SW_APPEARANCE_CLIP( this )
    const PixelContext    context = PixelContext::make( this, bFrontFace );
    const MaterialSurface surface = computeMaterialSurface( context );
    surface.applyAlphaCutoff();
    return context.storeSurface( surface );
}

    #if defined( MATERIAL_TWO_SIDED )
SurfaceOutput PSMain( MaterialInterpolants input, bool bFrontFace : SV_IsFrontFace )
{
    return input.shadePixel( bFrontFace );
}
    #else
SurfaceOutput PSMain( MaterialInterpolants input )
{
    return input.shadePixel( true );
}
    #endif

#endif // SW_MATERIAL_DEPTH_PASS

#endif // SW_ENGINE_MATERIALSHADERMAIN_HLSLI
