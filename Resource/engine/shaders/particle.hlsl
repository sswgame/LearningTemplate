// particle.hlsl — 파티클 빌보드(Vfx). 입자 하나 = 인스턴스 하나, 모서리 여섯을 SV_VertexID 로 만든다(동적 정점 버퍼가 없다 — canvas.hlsl 과 같은 방식).
// 방출기 하나가 그리기 하나다. 입자 원소는 C++ GPUParticle(Engine/Vfx/VfxParticle.h)과 같은 배치이고, 좌표는 방출기 로컬이라 루트 상수의 방출기 행렬로 옮긴다.
// 사각형은 카메라를 본다: 화면 x · y 축의 월드 방향을 g_InvViewProj 로 입자 깊이에서 되돌려 구한다(원근 · 직교 모두 — PassCB 를 늘리지 않는다).
// 출력은 프리멀티플라이 알파다(PSO _bPremultipliedAlpha — One/InvSrcAlpha). 더하기 블렌드는 알파를 0 으로 내서 같은 PSO 로 그린다.

// 이 셰이더는 루트 상수 블록을 직접 선언한다(binding.hlsli 의 그래픽스 블록 g_SwMaterialCount 대신).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

struct GPUParticle
{
	float4 positionSize; // xyz = 방출기 로컬 위치, w = 크기(월드 변 길이 — 방출기 배율이 곱해진다)
	float4 color;        // 선형 RGBA
	float4 rotationLife; // x = 회전(라디안), y = 수명 비율, zw = 비어 있음
};

SW_DECLARE_STRUCTURED_BUFFER( GPUParticle, g_SwParticles, SW_SLOT_PASS_SRV );

// 칸 순서는 C++ VfxParticleRenderer 의 kRoot* 와 같다. 스칼라만 둔다 — float4 를 두면 DX12 루트 상수 · Vulkan 푸시 상수 · UBO 에뮬의 채움 규칙이 갈린다.
SW_ROOT_CONSTANTS_BEGIN
	uint  g_SwParticleBase;     // 이 방출기의 첫 입자 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않아 시작을 여기로 받는다
	uint  g_SwParticleTexture;  // DX12/Vulkan 은 bindless 전역 번호, DX11/GL 은 서수 0(t12)
	uint  g_SwParticleAdditive; // 1 이면 더하기(알파 0 으로 낸다)
	float g_SwParticleSizeScale; // 방출기 행렬의 최대 축 배율 — 입자 크기에 곱한다
	float g_SwParticleColumn0X;  // 방출기 행렬(행벡터 규약)의 열 0..2 — 월드 = dot( float4( 로컬, 1 ), 열 )
	float g_SwParticleColumn0Y;
	float g_SwParticleColumn0Z;
	float g_SwParticleColumn0W;
	float g_SwParticleColumn1X;
	float g_SwParticleColumn1Y;
	float g_SwParticleColumn1Z;
	float g_SwParticleColumn1W;
	float g_SwParticleColumn2X;
	float g_SwParticleColumn2Y;
	float g_SwParticleColumn2Z;
	float g_SwParticleColumn2W;
SW_ROOT_CONSTANTS_END

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR0;
	float2 uv       : TEXCOORD0;
};

static const float2 kCornerTable[6] = { float2( -0.5f, 0.5f ), float2( 0.5f, 0.5f ), float2( -0.5f, -0.5f ),
                                        float2( 0.5f, 0.5f ), float2( 0.5f, -0.5f ), float2( -0.5f, -0.5f ) };

// 화면 축 방향을 구할 때 NDC 에서 비켜 보는 거리 — 깊이 정밀도가 받쳐 주는 한 작게.
static const float kAxisProbeNdc = 0.01f;

float3 unprojectNdc( float2 ndc, float deviceDepth )
{
	const float4 world = mul( float4( ndc, deviceDepth, 1.0f ), g_InvViewProj );
	return world.xyz / world.w;
}

PSInput VSMain( SwVertexInput input, uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID )
{
	PSInput output;
	input.position              = input.position;
	const GPUParticle particle  = g_SwParticles[SW_ROOT( g_SwParticleBase ) + instanceId];
	const float4      local     = float4( particle.positionSize.xyz, 1.0f );
	const float3      center    = float3( dot( local, float4( SW_ROOT( g_SwParticleColumn0X ), SW_ROOT( g_SwParticleColumn0Y ), SW_ROOT( g_SwParticleColumn0Z ), SW_ROOT( g_SwParticleColumn0W ) ) ),
	                                      dot( local, float4( SW_ROOT( g_SwParticleColumn1X ), SW_ROOT( g_SwParticleColumn1Y ), SW_ROOT( g_SwParticleColumn1Z ), SW_ROOT( g_SwParticleColumn1W ) ) ),
	                                      dot( local, float4( SW_ROOT( g_SwParticleColumn2X ), SW_ROOT( g_SwParticleColumn2Y ), SW_ROOT( g_SwParticleColumn2Z ), SW_ROOT( g_SwParticleColumn2W ) ) ) );
	const float4      centerClip = mul( float4( center, 1.0f ), g_ViewProj );
	const float2      corner     = kCornerTable[vertexId % 6u];
	output.uv                    = float2( corner.x + 0.5f, 0.5f - corner.y );
	output.color                 = particle.color;
	// 카메라 뒤(또는 눈 위)의 입자는 그리지 않는다 — 넓이 0 인 삼각형을 클립 밖에 둔다.
	if ( centerClip.w <= 1e-4f )
	{
		output.position = float4( 2.0f, 2.0f, 2.0f, 1.0f );
		return output;
	}
	const float2 centerNdc   = centerClip.xy / centerClip.w;
	const float  centerDepth = centerClip.z / centerClip.w;
	const float3 origin      = unprojectNdc( centerNdc, centerDepth );
	const float3 right       = normalize( unprojectNdc( centerNdc + float2( kAxisProbeNdc, 0.0f ), centerDepth ) - origin );
	const float3 up          = normalize( unprojectNdc( centerNdc + float2( 0.0f, kAxisProbeNdc ), centerDepth ) - origin );
	const float  angle       = particle.rotationLife.x;
	const float2 rotated     = float2( corner.x * cos( angle ) - corner.y * sin( angle ), corner.x * sin( angle ) + corner.y * cos( angle ) );
	const float  size        = particle.positionSize.w * SW_ROOT( g_SwParticleSizeScale );
	const float3 world       = center + ( right * rotated.x + up * rotated.y ) * size;
	output.position          = mul( float4( world, 1.0f ), g_ViewProj );
	return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
	const float4 color = input.color * swSampleMaterialTexture( SW_ROOT( g_SwParticleTexture ), input.uv );
	const float  alpha = ( SW_ROOT( g_SwParticleAdditive ) != 0u ) ? 0.0f : color.a;
	return float4( color.rgb * color.a, alpha );
}
