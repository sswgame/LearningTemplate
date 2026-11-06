// compute_channelswap.hlsl — 파이프라인 XML 의 컴퓨트 패스 시험용 효과입니다. 입력의 빨강과 파랑을 맞바꿔 출력에 씁니다. 시나리오: automation/computepass.
#define SW_RW_SLOT0_GL_FORMAT "rgba16f"
#define SW_RW_SLOT1_GL_FORMAT "rgba16f"
#include "compute_pass.hlsli"

SW_NUMTHREADS( 8, 8, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    if ( dispatchThreadID.x >= SW_ROOT( g_ComputeWidth ) || dispatchThreadID.y >= SW_ROOT( g_ComputeHeight ) )
        return;
    const float4 source = Bindless::loadRwTexture2D( SW_ROOT( g_ComputeInput0Index ), dispatchThreadID.xy );
    Bindless::storeRwTexture2D( SW_ROOT( g_ComputeOutputIndex ), dispatchThreadID.xy, source.bgra );
}
