// canvas.hlsl — 화면 2D(UI · 월드 글자). 사각형 하나 = 인스턴스 하나, 모서리 여섯을 SV_VertexID 로 만든다(동적 정점 버퍼가 없다).
// 모양(둥근 모서리 · 테두리 · 그림자 · 둥근 자르기)은 픽셀 셰이더의 부호 거리(SDF)다. 사각 자르기는 일괄의 가위가 한다(C++ CanvasBatch).
// 출력은 프리멀티플라이 알파다(PSO _bPremultipliedAlpha — One/InvSrcAlpha) — 렌더 텍스처에 그려도 알파가 두 번 곱해지지 않는다.
// 사각형 원소는 C++ CanvasQuad(Graphics/Canvas/CanvasDrawList.h)와 같은 배치다(CanvasDrawListTest.QuadLayoutMatchesShader).
// 색각 보정(gv_colorVisionMode)은 여기서 건다 — UI 는 톤맵 뒤에 그리므로 톤맵 쪽 보정이 닿지 않는다. 함수는 톤맵 쪽과 같은 colorvision.hlsli 하나다.

// 이 셰이더는 루트 상수 블록을 직접 선언한다(binding.hlsli 의 그래픽스 블록 g_SwMaterialCount 대신).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "colorvision.hlsli"

static const uint kCanvasQuadRect        = 0u;
static const uint kCanvasQuadImage       = 1u;
static const uint kCanvasQuadGlyph       = 2u;
static const uint kCanvasQuadShadow      = 3u;
static const uint kCanvasFlagRoundedClip = 1u;

struct CanvasQuad
{
	float4 rect;         // x, y(변환 전 왼쪽 위의 화면 위치) · 너비, 높이(픽셀)
	float4 uvRect;       // u0, v0, u1, v1
	float4 color;        // 곧은 RGBA
	float4 borderColor;  // Rect: 테두리 색 · Glyph: 외곽선 색
	float4 cornerRadius; // 왼위 · 오위 · 오아 · 왼아(픽셀)
	float4 clipRect;     // 둥근 자르기 x0, y0, x1, y1(픽셀)
	float4 axis;         // 2×2 변환 — 축 X = xy · 축 Y = zw
	float4 params;       // Rect: x 테두리 · Glyph: x 거리 배율 · y 외곽선 · z 굵게 · Shadow: y 흐림 · w 둥근 자르기 반지름
	uint   kind;
	uint   textureSlot;  // 일괄 텍스처 번호(0..3), 없음 = kInvalidIndex
	uint   flags;
	uint   padding0;
};

SW_DECLARE_STRUCTURED_BUFFER( CanvasQuad, g_SwCanvasQuads, SW_SLOT_CANVAS_QUAD_SRV );

SW_ROOT_CONSTANTS_BEGIN
	uint  g_SwCanvasQuadBase;    // 이 일괄의 첫 사각형 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않아 시작을 여기로 받는다
	uint  g_SwCanvasTexture0;    // 일괄 텍스처 넷 — DX12/Vulkan 은 bindless 전역 번호, DX11/GL 은 서수(t5..t8)
	uint  g_SwCanvasTexture1;
	uint  g_SwCanvasTexture2;
	uint  g_SwCanvasTexture3;
	float g_SwCanvasTargetWidth; // 대상 픽셀 크기
	float g_SwCanvasTargetHeight;
	uint  g_SwCanvasColorVision; // 색각 보정 방식(0 끔 · 1 적색약 · 2 녹색약 · 3 청색약) — 주 출력만, 렌더 텍스처 대상은 0
SW_ROOT_CONSTANTS_END

struct PSInput
{
	float4 position                : SV_POSITION;
	float2 local                   : TEXCOORD0; // 사각형 안 픽셀(0..너비, 0..높이) — 넓힌 가장자리는 밖
	float2 uv                      : TEXCOORD1;
	float2 screen                  : TEXCOORD2; // 대상 픽셀(둥근 자르기)
	nointerpolation uint quadIndex : TEXCOORD3;
};

static const float2 kCornerTable[6] = { float2( 0.0f, 0.0f ), float2( 1.0f, 0.0f ), float2( 0.0f, 1.0f ),
                                        float2( 1.0f, 0.0f ), float2( 1.0f, 1.0f ), float2( 0.0f, 1.0f ) };

// 둥근 상자의 부호 거리(안쪽 음수). position 은 상자 가운데 기준, radius 는 (왼위, 오위, 오아, 왼아) — y 는 아래가 + 다.
float computeRoundedBoxDistance( float2 position, float2 halfSize, float4 radius )
{
	const float  cornerRadius = ( position.x < 0.0f ) ? ( ( position.y < 0.0f ) ? radius.x : radius.w ) : ( ( position.y < 0.0f ) ? radius.y : radius.z );
	const float2 corner       = abs( position ) - halfSize + cornerRadius;
	return min( max( corner.x, corner.y ), 0.0f ) + length( max( corner, 0.0f ) ) - cornerRadius;
}

uint selectTexture( uint slot )
{
	if ( slot == 0u )
		return SW_ROOT( g_SwCanvasTexture0 );
	if ( slot == 1u )
		return SW_ROOT( g_SwCanvasTexture1 );
	if ( slot == 2u )
		return SW_ROOT( g_SwCanvasTexture2 );
	if ( slot == 3u )
		return SW_ROOT( g_SwCanvasTexture3 );
	return kInvalidIndex;
}

PSInput VSMain( SwVertexInput input, uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID )
{
	PSInput output;
	input.position              = input.position;
	const uint       quadIndex  = SW_ROOT( g_SwCanvasQuadBase ) + instanceId;
	const CanvasQuad quad       = g_SwCanvasQuads[quadIndex];
	const float2     corner     = kCornerTable[vertexId % 6u];
	// 모양을 셰이더가 깎는 종류(사각형 · 그림자)는 가장자리 계단 없애기 1 px · 흐림만큼 넓힌다. 텍스처 종류는 UV 가 밖으로 새지 않게 넓히지 않는다.
	float expand = 0.0f;
	if ( quad.kind == kCanvasQuadRect )
		expand = 1.0f;
	else if ( quad.kind == kCanvasQuadShadow )
		expand = quad.params.y * 2.0f + 1.0f;
	const float2 local  = lerp( float2( -expand, -expand ), quad.rect.zw + expand, corner );
	const float2 screen = quad.rect.xy + local.x * quad.axis.xy + local.y * quad.axis.zw;
	output.position     = float4( screen.x / SW_ROOT( g_SwCanvasTargetWidth ) * 2.0f - 1.0f, 1.0f - screen.y / SW_ROOT( g_SwCanvasTargetHeight ) * 2.0f, 0.0f, 1.0f );
	output.local        = local;
	output.uv           = lerp( quad.uvRect.xy, quad.uvRect.zw, local / max( quad.rect.zw, float2( 1e-5f, 1e-5f ) ) );
	output.screen       = screen;
	output.quadIndex    = quadIndex;
	return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
	const CanvasQuad quad     = g_SwCanvasQuads[input.quadIndex];
	const float2     halfSize = quad.rect.zw * 0.5f;
	float4           color    = quad.color;
	float            coverage = 1.0f;

	if ( quad.kind == kCanvasQuadRect )
	{
		const float distance       = computeRoundedBoxDistance( input.local - halfSize, halfSize, quad.cornerRadius );
		const float fillCoverage   = saturate( 0.5f - distance );
		const float borderCoverage = ( quad.params.x > 0.0f ) ? saturate( 0.5f + distance + quad.params.x ) : 0.0f;
		// 테두리 띠는 테두리 색, 안쪽은 채움 색 — 둘 다 곧은 색이라 여기서 섞고 아래에서 한 번 프리멀티플라이한다.
		color    = lerp( quad.color, quad.borderColor, borderCoverage );
		coverage = fillCoverage;
	}
	else if ( quad.kind == kCanvasQuadImage )
	{
		color *= swSampleMaterialTexture( selectTexture( quad.textureSlot ), input.uv );
		if ( any( quad.cornerRadius > 0.0f ) )
			coverage = saturate( 0.5f - computeRoundedBoxDistance( input.local - halfSize, halfSize, quad.cornerRadius ) );
	}
	else if ( quad.kind == kCanvasQuadGlyph )
	{
		// 아틀라스 값 0..1 → 윤곽까지의 화면 픽셀 거리(안쪽 +). params.x = 2 × spread × (화면 px / 래스터 px).
		const float sdfValue = swSampleMaterialTexture( selectTexture( quad.textureSlot ), input.uv ).r;
		const float distance = ( sdfValue - 0.5f ) * quad.params.x + quad.params.z;
		const float fill     = saturate( distance + 0.5f );
		const float outline  = ( quad.params.y > 0.0f ) ? saturate( distance + quad.params.y + 0.5f ) : fill;
		color    = lerp( quad.borderColor, quad.color, ( outline > 0.0f ) ? fill / outline : 0.0f );
		coverage = outline;
	}
	else
	{
		// 흐린 둥근 상자 그림자 — 부호 거리를 흐림 폭으로 부드럽게(가우시안 적분의 근사).
		const float distance = computeRoundedBoxDistance( input.local - halfSize, halfSize, quad.cornerRadius );
		const float blur     = max( quad.params.y, 0.5f );
		coverage             = 1.0f - smoothstep( -blur, blur, distance );
	}

	if ( ( quad.flags & kCanvasFlagRoundedClip ) != 0u )
	{
		const float2 clipHalf   = ( quad.clipRect.zw - quad.clipRect.xy ) * 0.5f;
		const float2 clipCenter = ( quad.clipRect.zw + quad.clipRect.xy ) * 0.5f;
		coverage *= saturate( 0.5f - computeRoundedBoxDistance( input.screen - clipCenter, clipHalf, quad.params.wwww ) );
	}
	const float  alpha     = color.a * coverage;
	const float3 corrected = swApplyColorVisionCorrection( color.rgb, SW_ROOT( g_SwCanvasColorVision ) );
	return float4( corrected * alpha, alpha );
}
