/**
 * @file screen_rotation.hlsl
 * @brief 화면 회전 출력(pre-rotation)입니다 — 회전한 휴대 기기 화면에서 엔진이 논리 크기(사용자가 보는 방향)로 그린 대기 텍스처를 스왑체인 이미지(기기 기본 방향)로 돌려 옮긴다.
 * @details 스왑체인이 서피스의 currentTransform 으로 만들어져 합성기가 돌리지 않으므로 이 패스가 돌린다(https://developer.android.com/games/optimize/vulkan-prerotation).
 *          실제 이미지 UV → 논리 UV 대응(2x2 행렬 · 이동)은 C++ `RHISurfaceRotation::computeLogicalUVMapping` 이 구해 루트 상수로 넘긴다 — 셰이더는 회전 각도를 모른다.
 *          크기가 같은 회전이라 걸러 읽지 않고 텍셀을 그대로 옮긴다. 원본은 네이티브(DX12 · Vulkan)는 bindless 번호, 에뮬은 SourceColor 칸이다.
 *          C++: FrameScreenRotation::recordToSwapchain.
 */
#define SW_OWN_ROOT_CONSTANTS 1
#include "binding.hlsli"
#include "fullscreen.hlsli"

SW_ROOT_CONSTANTS_BEGIN
{
    uint  g_RotationSource;       // 논리 대기 텍스처의 bindless 번호(네이티브). 에뮬은 쓰지 않는다
    float g_RotationLogicalWidth; // 논리 대기 텍스처 픽셀 크기
    float g_RotationLogicalHeight;
    float g_RotationM00; // 논리 UV = M × 실제 UV + 이동 (원소는 -1 · 0 · 1)
    float g_RotationM01;
    float g_RotationM10;
    float g_RotationM11;
    float g_RotationOffsetU;
    float g_RotationOffsetV;
};
SW_ROOT_CONSTANTS_END

float4 PSMain( PSInput input ) : SV_TARGET
{
    const float2 physicalUV = input._uv;
    const float2 logicalUV  = float2( SW_ROOT( g_RotationM00 ) * physicalUV.x + SW_ROOT( g_RotationM01 ) * physicalUV.y + SW_ROOT( g_RotationOffsetU ),
                                      SW_ROOT( g_RotationM10 ) * physicalUV.x + SW_ROOT( g_RotationM11 ) * physicalUV.y + SW_ROOT( g_RotationOffsetV ) );
    const int2   size       = int2( SW_ROOT( g_RotationLogicalWidth ), SW_ROOT( g_RotationLogicalHeight ) );
    const int2   texel      = clamp( int2( floor( logicalUV * float2( size ) ) ), int2( 0, 0 ), size - int2( 1, 1 ) );
    return Bindless::loadEngineTexel( SW_ROOT( g_RotationSource ), SW_EMU_SLOT_SourceColor, texel );
}
