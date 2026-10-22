/**
 * materialshadermain.hlsli — 머티리얼 셰이더 뼈대의 2단: 정점 · 픽셀 진입점(VSMain · PSMain).
 *
 * 셰이더 파일이 훅을 정의한 **뒤에** include 한다(순서는 materialshader.hlsli 머리말). 훅의 형식 · 선택 규칙은 거기에 있다.
 *
 * 패스마다 다른 점은 이 파일 한 곳에 있다 — 셰이더 파일은 패스를 가르는 `#if` 를 쓰지 않는다.
 *   본 패스(포워드)   `SW_SURFACE_OUTPUT` 하나: 표면 훅 → 조명 훅 → 보기 모드 → swStoreSurface
 *   G버퍼(SW_PASS_GBUFFER)  조명 훅을 컴파일 아웃하고 표면만 적는다(셰이딩은 디퍼드 조명 패스가 한다)
 *   깊이(SW_PASS_SHADOW · SW_PASS_DEPTH_PREPASS)  위치만 만든다. 픽셀은 알파 컷오프 머티리얼만 오린다
 * 클립 위치는 정점 단계의 한 곳에서만 만든다(`swComputeWorldPosition` → 훅의 오프셋 → `swComputeClipPosition`). 그림자는 빛 행렬, 나머지는 카메라 행렬이다.
 *
 * 출구가 하나다 — 진입점과 도우미 모두 마지막에 한 번만 반환한다.
 */

#ifndef SW_ENGINE_MATERIALSHADERMAIN_HLSLI
#define SW_ENGINE_MATERIALSHADERMAIN_HLSLI

// ------------------------------------------------------------------------------
// 1) 정점 단계
// ------------------------------------------------------------------------------
#if defined( SW_PASS_SHADOW )
#define SW_MATERIAL_VIEW_PROJECTION g_LightViewProj
#else
#define SW_MATERIAL_VIEW_PROJECTION g_ViewProj
#endif

SwMaterialInterpolants VSMain( SwVertexInput input, uint vertexId : SV_VertexID )
{
	SwMaterialInterpolants output;
	SwVertexContext        context;
	context.input    = input;
	context.vertexId = vertexId;
	// 인스턴스는 슬롯 스트림이 준 자리에서, 배치는 인스턴스에서 — 모프 풀 시작 · 정점 풀 시작이 배치 표에 있다.
	context.instance = swLoadInstance( input.instanceSlot );
	// GPU 가 변형한 정점(VAT · 모프 · 스킨)이 있으면 그걸 쓴다(아니면 입력 스트림 그대로). 위치와 노멀이 같이 온다.
	swLoadAnimatedVertex( context.instance, vertexId, input.position, input.normal, context.localPosition, context.localNormal );
	// 위치는 깊이 프리패스 · 그림자와 **같은 함수**로 만든다 — 프리패스가 쓴 깊이와 비트까지 같아야 본 패스의 LessEqual 을 통과한다(binding.hlsli 7 절).
	const float4 worldPosition = swComputeWorldPosition( context.localPosition, context.instance.world );
	context.worldPosition      = worldPosition.xyz;
	// 월드 노멀은 월드 행렬이 아니라 그 3x3 의 여인수 행렬로 옮긴다(binding.hlsli swComputeWorldNormal) — 비균등 스케일에서도 조명이 맞는다.
	context.worldNormal        = swComputeWorldNormal( context.localNormal, context.instance.world );
#if defined( SW_MATERIAL_READ_IN_VERTEX )
	context.material = SW_MATERIAL( context.instance.materialIndex );
#endif

	output.position      = swComputeClipPosition( worldPosition, SW_MATERIAL_VIEW_PROJECTION );
	output.worldPosition = worldPosition.xyz;
	output.color         = input.color;
	// UV 는 정점 속성이다. 위치로 지어내면(`position.xy * 0.5 + 0.5`) 원점 중심 단위 도형이 아닐 때 텍스처가 엉뚱하게 붙는다.
	output.uv            = input.uv;
	output.normal        = context.worldNormal;
#if defined( SW_MATERIAL_USES_TANGENT )
	// 탄젠트는 표면을 따라가는 방향이라 노멀(여인수 행렬)과 달리 월드 행렬 그대로 곱한다. 스킨 · 모프 메시는 컴퓨트가 돌린 탄젠트다.
	const float4 localTangent = swLoadMorphedTangent( context.instance.meshBatchIndex, vertexId, input.tangent );
	output.tangent            = float4( mul( float4( localTangent.xyz, 0.0f ), context.instance.world ).xyz, localTangent.w );
#endif
#if !defined( SW_MATERIAL_READ_IN_VERTEX )
	output.materialIndex = context.instance.materialIndex;
#endif
#if defined( SW_MATERIAL_CUSTOM_VARYINGS )
	output.user = computeMaterialVaryings( context );
#endif
	SW_APPEARANCE_WRITE( output, context.instance )
	return output;
}

// ------------------------------------------------------------------------------
// 2) 픽셀 단계
// ------------------------------------------------------------------------------
/** @brief 보간된 값(과 픽셀에서 읽는 셰이더의 머티리얼)으로 픽셀 문맥을 만든다. 양면 머티리얼의 뒷면은 노멀을 뒤집는다. */
SwPixelContext swMakePixelContext( SwMaterialInterpolants input, bool bFrontFace )
{
	SwPixelContext context;
	context.position      = input.position;
	context.color         = input.color;
	context.uv            = input.uv;
	context.normal        = normalize( input.normal );
#if defined( MATERIAL_TWO_SIDED )
	// 양면 머티리얼의 뒷면은 노멀이 반대다 — 뒤집지 않으면 안쪽이 바깥 빛으로 칠해진다.
	context.normal        = bFrontFace ? context.normal : -context.normal;
#endif
	context.worldPosition = input.worldPosition;
#if defined( SW_MATERIAL_USES_TANGENT )
	context.tangent       = input.tangent;
#endif
	context.user          = input.user;
#if !defined( SW_MATERIAL_READ_IN_VERTEX )
	// 머티리얼은 인스턴스의 materialIndex 로 버퍼에서 읽는다 — 드로우별 바인딩 없음. 픽셀 단계에서만 읽는다(binding.hlsli 9 절).
	context.material      = SW_MATERIAL( input.materialIndex );
#endif
	return context;
}

/** @brief 조명 훅의 선택을 풀어 색을 낸다 — 훅 정의 · 기본 PBR · 조명 없음 중 셰이더가 고른 하나다. */
float4 swComputeSelectedLighting( SwPixelContext context, SwMaterialSurface surface )
{
	float4 lit;
#if defined( SW_MATERIAL_CUSTOM_LIGHTING )
	lit = computeMaterialLighting( context, surface );
#elif defined( SW_MATERIAL_UNLIT )
	lit = surface.albedo;
#else
#if SW_VIEWMODE_SKIPS_LIGHTING
	// 보기 모드 Unlit 등 — 조명 · 그림자를 컴파일 아웃하고 알베도 그대로.
	lit = surface.albedo;
#else
	// 조명 식은 디퍼드와 **같은 함수**다(lighting.hlsli). 두 벌로 두면 두 경로의 그림이 갈라진다.
	const float  shadow = swSampleShadowAtWorld( context.worldPosition, surface.normal );
	lit                 = float4( swShadeLights( surface.albedo.rgb, context.worldPosition, surface.normal, shadow, surface.roughness, surface.metallic ), surface.albedo.a );
#endif
#endif
	return lit;
}

#if defined( SW_MATERIAL_DEPTH_PASS )

/** @brief 깊이 패스의 픽셀 — 등장 디더와 알파 컷오프만 오린다(본 패스와 같은 표면 훅의 같은 알파 · 컷오프라 같은 자리를 버린다). */
void swShadeDepthPixel( SwMaterialInterpolants input, bool bFrontFace )
{
	SW_APPEARANCE_CLIP( input )
#if defined( MATERIAL_ALPHA_CUTOFF )
	const SwPixelContext    context = swMakePixelContext( input, bFrontFace );
	const SwMaterialSurface surface = computeMaterialSurface( context );
	clip( surface.albedo.a - surface.alphaCutoff );
#endif
}

void PSMain( SwMaterialInterpolants input )
{
	swShadeDepthPixel( input, true );
}

#else

/**
 * @brief 본 패스 · G버퍼 패스의 픽셀 — 표면 훅 → (G버퍼가 아니면) 조명 훅 → 출력.
 * @details 반환 타입이 패스에 따라 갈린다(binding.hlsli 12 절 `SW_SURFACE_OUTPUT`) — 포워드는 색 하나, G버퍼는 알베도 · 노멀 · 머티리얼 셋이다.
 *          G버퍼 패스는 조명을 계산하지 않는다 — 조명 코드가 통째로 컴파일 아웃되므로 런타임 분기가 아니다.
 *          알파를 쓰는지는 퍼뮤테이션이 정한다: 반투명 머티리얼만 MATERIAL_BLEND_TRANSLUCENT 를 가져 불투명 변형에서 머티리얼 알파가 새지 않는다.
 */
SW_SURFACE_OUTPUT swShadePixel( SwMaterialInterpolants input, bool bFrontFace )
{
	SW_APPEARANCE_CLIP( input )
	const SwPixelContext    context = swMakePixelContext( input, bFrontFace );
	const SwMaterialSurface surface = computeMaterialSurface( context );
#if defined( MATERIAL_ALPHA_CUTOFF )
	clip( surface.albedo.a - surface.alphaCutoff );
#endif
	SW_SURFACE_OUTPUT result;
#if defined( SW_PASS_GBUFFER )
	result = swStoreSurface( float4( 0.0f, 0.0f, 0.0f, 0.0f ), surface.albedo, surface.normal, context.worldPosition, surface.roughness, surface.metallic );
#else
	float4 lit = swComputeSelectedLighting( context, surface );
#if !defined( MATERIAL_BLEND_TRANSLUCENT )
	lit.a = 1.0f;
#endif
	result = swStoreSurface( lit, surface.albedo, surface.normal, context.worldPosition, surface.roughness, surface.metallic );
#endif
	return result;
}

#if defined( MATERIAL_TWO_SIDED )
SW_SURFACE_OUTPUT PSMain( SwMaterialInterpolants input, bool bFrontFace : SV_IsFrontFace )
{
	return swShadePixel( input, bFrontFace );
}
#else
SW_SURFACE_OUTPUT PSMain( SwMaterialInterpolants input )
{
	return swShadePixel( input, true );
}
#endif

#endif // SW_MATERIAL_DEPTH_PASS

#endif // SW_ENGINE_MATERIALSHADERMAIN_HLSLI
