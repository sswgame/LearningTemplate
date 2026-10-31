#include "binding.hlsli"

/**
 * gpu_nan_check.hlsl — 패스 출력 텍스처의 NaN · Inf 감시(컴퓨트, RenderPassType::NanCheck). 언리얼 r.CheckNaN 과 같은 선이다.
 *
 * 스레드 하나가 텍셀 하나다. 네 채널 중 하나라도 NaN 이면 슬롯의 NaN 카운터에, NaN 은 없고 무한이 있으면 Inf 카운터에 하나를 더한다(원자 연산).
 * 카운터는 지우지 않는 누적 값이다 — CPU 가 지난번 읽은 값과의 차로 이번 프레임의 수를 구한다(GPUNanCheck). 슬롯은 패스 출력마다 하나이고 슬롯의 이름은 CPU 가 안다.
 *
 * 텍스처는 엔진 텍스처(bindless 배열)로 읽는다(Bindless::loadEngineTexel). 샘플 텍스처를 컴퓨트에서 읽을 수 없는 에뮬 백엔드(DX11 · GL)에서는 읽지 않는다 —
 * 렌더러가 이 패스를 만들지 않고(kRequiresNativeBindless), 아래 에뮬 분기는 쿠커가 컴파일만 하게 두는 빈 진입점이다.
 *
 * 바인딩 계약: 쓰기 u0 카운터(슬롯마다 uint 둘: [슬롯 * 2] NaN 텍셀 수 · [슬롯 * 2 + 1] Inf 텍셀 수), 루트 상수 아래 블록.
 * C++: GPUNanCheck::scan — dispatchCompute( ceil(너비 / 8), ceil(높이 / 8), 1 ).
 */

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_NanTextureIndex; // 감시할 텍스처의 bindless 번호
    uint g_NanWidth;        // 텍스처 크기(px)
    uint g_NanHeight;
    uint g_NanSlot; // 카운터 슬롯
};
SW_ROOT_CONSTANTS_END

SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_NanCountRW, 0 );

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
#if defined( SW_NATIVE_BINDLESS )
    if ( dispatchThreadID.x >= SW_ROOT( g_NanWidth ) || dispatchThreadID.y >= SW_ROOT( g_NanHeight ) )
        return;
    const float4 texel    = Bindless::loadEngineTexel( SW_ROOT( g_NanTextureIndex ), SW_EMU_SLOT_SourceColor, int2( dispatchThreadID.xy ) );
    const bool   bNan     = any( isnan( texel ) );
    const bool   bInfOnly = bNan == false && any( isinf( texel ) );
    if ( bNan )
        InterlockedAdd( g_NanCountRW[SW_ROOT( g_NanSlot ) * 2u], 1u );
    if ( bInfOnly )
        InterlockedAdd( g_NanCountRW[SW_ROOT( g_NanSlot ) * 2u + 1u], 1u );
#endif
}
