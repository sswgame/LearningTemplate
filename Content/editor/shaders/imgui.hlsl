// imgui.hlsl — 에디터 UI(Dear ImGui)의 그리기 목록. 에디터 전용이라 에디터 도메인에 둔다(엔진 · 게임은 쓰지 않는다). 언리얼 Slate 가 제 RHI 렌더러로 UI 를 그리듯 ImGui 렌더러 백엔드 넷 대신 RHI 하나로 그린다(Editor ImGuiRenderer).
// 정점 버퍼 · 인덱스 버퍼가 없다 — 프레임의 모든 그리기 목록을 uint 구조 버퍼 하나에 싣고(정점 다섯 칸 · 인덱스 한 칸) SV_VertexID 로 꺼낸다(canvas 와 같은 정점 끌어오기).
// 정점 배치는 ImDrawVert(위치 float2 · UV float2 · 색 RGBA8)와 같다(C++ ImGuiRenderer 의 static_assert). 색은 곧은 알파다(PSO 블렌드 SrcAlpha/InvSrcAlpha — ImGui 의 규약).

// 이 셰이더는 루트 상수 블록을 직접 선언한다(binding.hlsli 의 그래픽스 블록 g_SwMaterialCount 대신).
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"

static const uint kImGuiVertexWords         = 5u; // ImDrawVert 하나 = uint 다섯
static const uint kImGuiVerticesPerInstance = 6u; // 인스턴스 하나 = 삼각형 둘(C++ kVerticesPerInstance)

SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwImGuiWords, SW_SLOT_PASS_SRV );

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_SwImGuiVertexBase; // 이 그리기 목록의 첫 정점 칸(uint 번호)
    uint  g_SwImGuiIndexBase;  // 이 그리기 명령의 첫 인덱스 칸(uint 번호)
    uint  g_SwImGuiTexture;    // DX12/Vulkan 은 bindless 전역 번호, DX11/GL 은 서수 0(t16)
    float g_SwImGuiScaleX;     // 화면 좌표 → NDC: ndc = position * scale + offset(y 는 아래가 +)
    float g_SwImGuiScaleY;
    float g_SwImGuiOffsetX;
    float g_SwImGuiOffsetY;
};
SW_ROOT_CONSTANTS_END

struct PSInput
{
    float4 _position : SV_POSITION;
    float2 _uv : TEXCOORD0;
    float4 _color : COLOR0;
};

// 삼각형 둘(ImGui 사각형 하나) = 인스턴스 하나(정점 여섯)로 그린다 — 메시가 없는 드로우의 슬롯 0 버퍼(kFallbackVertexCount 여섯)보다 많은 정점을 한 인스턴스에 만들면
// D3D12 디버그 레이어가 버퍼가 짧다고 알린다(id 210). 홀수 개의 마지막 삼각형은 정점 셋 · 인스턴스 하나로 그린다. 시작 인스턴스는 0 이다(D3D 는 SV_InstanceID 에 시작 인스턴스를 더하지 않는다).
PSInput VSMain( VertexInput input, uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID )
{
    PSInput output;
    input._position    = input._position;
    const uint   index = g_SwImGuiWords[SW_ROOT( g_SwImGuiIndexBase ) + instanceID * kImGuiVerticesPerInstance + vertexID];
    const uint   word  = SW_ROOT( g_SwImGuiVertexBase ) + index * kImGuiVertexWords;
    const float2 pos   = float2( asfloat( g_SwImGuiWords[word] ), asfloat( g_SwImGuiWords[word + 1u] ) );
    output._uv         = float2( asfloat( g_SwImGuiWords[word + 2u] ), asfloat( g_SwImGuiWords[word + 3u] ) );
    const uint packed  = g_SwImGuiWords[word + 4u];
    output._color      = float4( packed & 0xFFu, ( packed >> 8u ) & 0xFFu, ( packed >> 16u ) & 0xFFu, packed >> 24u ) / 255.0f;
    output._position   = float4( pos.x * SW_ROOT( g_SwImGuiScaleX ) + SW_ROOT( g_SwImGuiOffsetX ), pos.y * SW_ROOT( g_SwImGuiScaleY ) + SW_ROOT( g_SwImGuiOffsetY ), 0.0f, 1.0f );
    return output;
}

float4 PSMain( PSInput input ) : SV_TARGET
{
    return input._color * MaterialTexture::sampleIndexTexture( SW_ROOT( g_SwImGuiTexture ), input._uv );
}
