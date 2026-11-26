// fullscreen_trail.hlsl — 파이프라인 XML 의 풀스크린 패스(`_type="Fullscreen"`) 시연 효과입니다. 장면 색에 초록을 줄인 색을 지난 프레임 출력과 섞어 잔상을 남긴다. 시나리오: automation/fullscreenpass.
// 칸은 첨부 · 값 이름으로 PassCB 끝에 적는다 — 입력 SceneColor, 출력 TrailColor 의 기록, 값 TrailBlend · GreenScale(fullscreenpasspipeline.xml).
#define SW_PASS_CB_USER_PARAMS      \
    uint  g_SceneColorIndex;        \
    uint  g_TrailColorHistoryIndex; \
    float g_TrailBlend;             \
    float g_GreenScale;
#include "binding.hlsli"
#include "fullscreen.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float3 current = PassTexture::sampleNamed( g_SceneColorIndex, input._uv ).rgb * float3( 1.0f, g_GreenScale, 1.0f );
    // 기록이 없는 프레임(처음 · 컷 · 크기 바뀜)은 이번 색만 쓴다.
    const float3 previous = g_TrailColorHistoryIndex == kInvalidIndex ? current : PassTexture::sampleNamedPrevious( g_TrailColorHistoryIndex, input._uv ).rgb;
    return float4( lerp( current, previous, g_TrailBlend ), 1.0f );
}
