/**
 * @file display_encode.hlsl
 * @brief HDR 화면 출력의 마지막 두 패스입니다 — UI 합성(`SW_DISPLAY_UI_COMPOSITE`)과 인코딩(`SW_DISPLAY_OUTPUT` 1 HDR10 · 2 scRGB). C++: FrameDisplayEncode::recordToSwapchain.
 * @details HDR 이면 장면은 장면 출력 RT(R16G16B16A16F, [0,1] 출력 신호 — 1 = 최대 휘도)에, UI 는 SDR UI RT(RGBA8, 미리 곱한 알파 — 1 = 종이 흰색)에 따로 그린다.
 *          UI 합성은 UI 신호를 장면 신호로 옮겨(`(종이 흰색 / 최대 휘도)^(1/2.2)` 배) 미리 곱한 알파 블렌드로 장면 RT 위에 섞고, 인코딩은 장면 RT 를 nits 로 풀어 백버퍼 값으로 쓴다
 *          (언리얼 `r.HDR.UI.CompositeMode` · `r.HDR.UI.Level` 과 같은 자리). SDR 은 두 패스가 없다(그래프 · 골든 그대로).
 *          원본은 네이티브(DX12 · Vulkan)는 bindless 번호, 에뮬(DX11)은 SourceColor 칸이다. 크기가 같은 옮김이라 걸러 읽지 않고 텍셀을 그대로 읽는다.
 */
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "fullscreen.hlsli"
#include "display_encode.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_DisplaySource;      // 원본(UI 합성은 UI RT, 인코딩은 장면 RT)의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    float g_DisplaySourceWidth; // 원본 픽셀 크기
    float g_DisplaySourceHeight;
    float g_DisplayScale; // UI 합성: UI 신호 → 장면 신호 배율, 인코딩: 출력 최대 휘도(nits)
};
SW_ROOT_CONSTANTS_END

float4 PSMain( PSInput input ) : SV_TARGET
{
    const int2   size   = int2( SW_ROOT( g_DisplaySourceWidth ), SW_ROOT( g_DisplaySourceHeight ) );
    const int2   texel  = clamp( int2( floor( input._uv * float2( size ) ) ), int2( 0, 0 ), size - int2( 1, 1 ) );
    const float4 source = Bindless::loadEngineTexel( SW_ROOT( g_DisplaySource ), SW_EMU_SLOT_SourceColor, texel );
#if defined( SW_DISPLAY_UI_COMPOSITE )
    // 미리 곱한 알파라 색만 배율을 곱한다 — 블렌드(src + dst × (1 - a))가 장면 신호 위에 SDR 화면과 같은 감마 공간에서 섞는다.
    return float4( source.rgb * SW_ROOT( g_DisplayScale ), source.a );
#else
    return float4( DisplayEncode::encode( DisplayEncode::toDisplayNits( source.rgb, SW_ROOT( g_DisplayScale ) ) ), 1.0f );
#endif
}
