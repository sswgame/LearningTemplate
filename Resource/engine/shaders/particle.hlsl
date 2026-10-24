// particle.hlsl — 파티클 빌보드 · 띠(Vfx). 인스턴스 하나 = 사각형 하나이고, 모서리 여섯을 SV_VertexID 로 만든다(동적 정점 버퍼가 없다 — canvas.hlsl 과 같은 방식).
// 방출기 하나가 그리기 하나다. 입자 원소는 C++ GPUParticle(Engine/Vfx/VfxParticle.h)과 같은 배치이고, 좌표는 방출기 로컬이라 루트 상수의 방출기 행렬로 옮긴다.
// 빌보드: 인스턴스 = 입자. 사각형은 카메라를 본다 — 화면 x · y 축의 월드 방향을 g_InvViewProj 로 입자 깊이에서 되돌려 구한다(원근 · 직교 모두 — PassCB 를 늘리지 않는다).
// 띠: 인스턴스 = 이웃 입자 둘(태어난 순서로 놓여 있다 — VfxCPUSimulation::writeParticles)을 잇는 마디. 폭은 입자 크기이고, 마디 방향과 시선의 외적으로 편다.
// 출력은 프리멀티플라이 알파다(PSO _bPremultipliedAlpha — One/InvSrcAlpha). 더하기 블렌드는 알파를 0 으로 내서 같은 PSO 로 그린다.

// 이 셰이더는 루트 상수 블록을 직접 선언한다(binding.hlsli 의 그래픽스 블록 g_SwMaterialCount 대신).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

struct GPUParticle
{
    float4 _positionSize; // xyz = 방출기 로컬 위치, w = 크기(월드 변 길이 — 방출기 배율이 곱해진다)
    float4 _color;        // 선형 RGBA
    float4 _rotationLife; // x = 회전(라디안), y = 수명 비율, zw = 비어 있음
};

SW_DECLARE_STRUCTURED_BUFFER( GPUParticle, g_SwParticles, SW_SLOT_PASS_SRV );

// 칸 순서는 C++ VfxParticleRenderer 의 kRoot* 와 같다. 스칼라만 둔다 — float4 를 두면 DX12 루트 상수 · Vulkan 푸시 상수 · UBO 에뮬의 채움 규칙이 갈린다.
SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_SwParticleBase;      // 이 방출기의 첫 입자 — D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않아 시작을 여기로 받는다
    uint  g_SwParticleTexture;   // DX12/Vulkan 은 bindless 전역 번호, DX11/GL 은 서수 0(머티리얼 텍스처 슬롯 t16)
    uint  g_SwParticleFlags;     // kParticleFlag* 비트(C++ VfxParticleRenderer 의 kFlag* 와 같다)
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
};
SW_ROOT_CONSTANTS_END

static const uint kParticleFlagAdditive = 1u; // 더하기 — 알파를 0 으로 낸다
static const uint kParticleFlagRibbon   = 2u; // 띠 — 인스턴스가 이웃 입자 둘을 잇는 마디다

struct PSInput
{
    float4 _position : SV_POSITION;
    float4 _color : COLOR0;
    float2 _uv : TEXCOORD0;
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

// 방출기 로컬 점을 월드로 옮긴다(루트 상수의 열 셋).
float3 toWorld( float3 localPosition )
{
    const float4 local = float4( localPosition, 1.0f );
    return float3( dot( local, float4( SW_ROOT( g_SwParticleColumn0X ), SW_ROOT( g_SwParticleColumn0Y ), SW_ROOT( g_SwParticleColumn0Z ), SW_ROOT( g_SwParticleColumn0W ) ) ),
                   dot( local, float4( SW_ROOT( g_SwParticleColumn1X ), SW_ROOT( g_SwParticleColumn1Y ), SW_ROOT( g_SwParticleColumn1Z ), SW_ROOT( g_SwParticleColumn1W ) ) ),
                   dot( local, float4( SW_ROOT( g_SwParticleColumn2X ), SW_ROOT( g_SwParticleColumn2Y ), SW_ROOT( g_SwParticleColumn2Z ), SW_ROOT( g_SwParticleColumn2W ) ) ) );
}

// 띠 마디 하나의 모서리. 모서리 표의 x(-0.5 · 0.5)가 마디의 앞 · 뒤 입자, y 가 폭의 양쪽이다.
PSInput ribbonVertex( uint vertexId, uint instanceId )
{
    PSInput           output;
    const float2      corner    = kCornerTable[vertexId % 6u];
    const uint        endIndex  = corner.x < 0.0f ? 0u : 1u;
    const GPUParticle particle  = g_SwParticles[SW_ROOT( g_SwParticleBase ) + instanceId + endIndex];
    const GPUParticle neighbour = g_SwParticles[SW_ROOT( g_SwParticleBase ) + instanceId + 1u - endIndex];
    const float3      center    = toWorld( particle._positionSize.xyz );
    const float3      other     = toWorld( neighbour._positionSize.xyz );
    const float4      clip      = mul( float4( center, 1.0f ), g_ViewProj );
    output._color               = particle._color;
    output._uv                  = float2( particle._rotationLife.y, 0.5f - corner.y );
    if ( clip.w <= 1e-4f )
    {
        output._position = float4( 2.0f, 2.0f, 2.0f, 1.0f );
        return output;
    }
    // 시선: 같은 화면 점의 다른 깊이로 되돌린 점과의 차(원근 · 직교 모두 같은 식). 마디 방향과의 외적이 폭 방향이다.
    const float2 ndc      = clip.xy / clip.w;
    const float  depth    = clip.z / clip.w;
    const float3 origin   = unprojectNdc( ndc, depth );
    const float3 viewRay  = unprojectNdc( ndc, depth * 0.5f ) - origin;
    const float3 along    = endIndex == 0u ? other - center : center - other;
    float3       side     = cross( along, viewRay );
    const float  sideSize = length( side );
    // 마디가 시선과 나란하면 폭 방향이 없다 — 화면 x 축으로 편다(넓이가 0 이 되지 않게).
    side             = sideSize > 1e-6f ? side / sideSize : normalize( unprojectNdc( ndc + float2( kAxisProbeNdc, 0.0f ), depth ) - origin );
    const float size = particle._positionSize.w * SW_ROOT( g_SwParticleSizeScale );
    output._position = mul( float4( center + side * ( corner.y * size ), 1.0f ), g_ViewProj );
    return output;
}

PSInput billboardVertex( uint vertexId, uint instanceId )
{
    PSInput           output;
    const GPUParticle particle   = g_SwParticles[SW_ROOT( g_SwParticleBase ) + instanceId];
    const float3      center     = toWorld( particle._positionSize.xyz );
    const float4      centerClip = mul( float4( center, 1.0f ), g_ViewProj );
    const float2      corner     = kCornerTable[vertexId % 6u];
    output._uv                   = float2( corner.x + 0.5f, 0.5f - corner.y );
    output._color                = particle._color;
    // 카메라 뒤(또는 눈 위)의 입자는 그리지 않는다 — 넓이 0 인 삼각형을 클립 밖에 둔다.
    if ( centerClip.w <= 1e-4f )
    {
        output._position = float4( 2.0f, 2.0f, 2.0f, 1.0f );
        return output;
    }
    const float2 centerNdc   = centerClip.xy / centerClip.w;
    const float  centerDepth = centerClip.z / centerClip.w;
    const float3 origin      = unprojectNdc( centerNdc, centerDepth );
    const float3 right       = normalize( unprojectNdc( centerNdc + float2( kAxisProbeNdc, 0.0f ), centerDepth ) - origin );
    const float3 up          = normalize( unprojectNdc( centerNdc + float2( 0.0f, kAxisProbeNdc ), centerDepth ) - origin );
    const float  angle       = particle._rotationLife.x;
    const float2 rotated     = float2( corner.x * cos( angle ) - corner.y * sin( angle ), corner.x * sin( angle ) + corner.y * cos( angle ) );
    const float  size        = particle._positionSize.w * SW_ROOT( g_SwParticleSizeScale );
    const float3 world       = center + ( right * rotated.x + up * rotated.y ) * size;
    output._position         = mul( float4( world, 1.0f ), g_ViewProj );
    return output;
}

PSInput VSMain( VertexInput input, uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID )
{
    input._position = input._position;
    if ( ( SW_ROOT( g_SwParticleFlags ) & kParticleFlagRibbon ) != 0u )
        return ribbonVertex( vertexId, instanceId );
    return billboardVertex( vertexId, instanceId );
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float4 color = input._color * MaterialTexture::sampleIndex( SW_ROOT( g_SwParticleTexture ), input._uv );
    const float  alpha = ( ( SW_ROOT( g_SwParticleFlags ) & kParticleFlagAdditive ) != 0u ) ? 0.0f : color.a;
    return float4( color.rgb * color.a, alpha );
}
