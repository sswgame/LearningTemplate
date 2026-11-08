// distortion_apply.hlsl — 화면 왜곡 적용(풀스크린). 왜곡 누적 패스가 쓴 화면 UV 오프셋(Distortion 역할)만큼 장면 색 사본(SceneColorCopy 역할)을 비껴 읽어 장면 색에 쓴다.
// 깊이를 본다 — 비껴 읽을 자리가 이 픽셀보다 가까우면(왜곡 면 뒤의 배경 위에 앞의 물체를 끌어오는 경우) 비끼지 않는다. 왜곡 면 자체의 깊이는 없으므로(반투명이라 깊이를 쓰지 않는다)
// "이 픽셀의 장면 깊이" 로 견준다 — 왜곡 면 뒤에 있으면서 이 픽셀보다 가까운 물체도 끌어오지 않는 보수적인 판정이다(언리얼은 왜곡 깊이를 따로 둔다).
// 오프셋이 0 인 픽셀은 사본 그대로라 왜곡이 없는 곳의 그림이 바뀌지 않는다. 씬에 왜곡이 없으면 이 패스를 기록하지 않는다(FramePassExecutor::executeDistortionApplyPass).
#include "binding.hlsli"
#include "fullscreen.hlsli"

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 offset       = PassTexture::sampleDistortionPoint( input._uv );
    const float  centerDepth  = PassTexture::sampleDepthPoint( input._uv ).r;
    const float2 shiftedUv    = saturate( input._uv + offset );
    const float  shiftedDepth = PassTexture::sampleDepthPoint( shiftedUv ).r;
    // 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버 사연 — binding.hlsli MorphPool::computeElement).
    const bool   bForeground = shiftedDepth < centerDepth;
    const float2 sampleUv    = bForeground ? input._uv : shiftedUv;
    return Bindless::sampleIndex( g_SceneColorCopyIndex, sampleUv );
}
