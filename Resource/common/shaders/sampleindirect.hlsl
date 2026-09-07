// ==============================================================================
// @file SampleIndirect.hlsl
// @brief GPU 컴퓨트 셰이더 기반 간접 드로우/디스패치 버퍼 갱신 셰이더
// ==============================================================================

/// @struct RHIDrawIndirectCommand
/// @brief GPU 렌더링 드로우 인디렉트 파라미터 구조체 (20 Bytes)
struct RHIDrawIndirectCommand
{
	uint vertexCount;           ///< 출력할 정점 수
	uint instanceCount;         ///< 출력할 인스턴스 수
	uint startVertexLocation;   ///< 시작 정점 오프셋
	uint startInstanceLocation; ///< 시작 인스턴스 오프셋
};

/// @struct RHIDispatchIndirectCommand
/// @brief GPU 컴퓨트 디스패치 인디렉트 파라미터 구조체 (12 Bytes)
struct RHIDispatchIndirectCommand
{
	uint threadGroupCountX; ///< X축 스레드 그룹 수
	uint threadGroupCountY; ///< Y축 스레드 그룹 수
	uint threadGroupCountZ; ///< Z축 스레드 그룹 수
};

#include "common.hlsli"

// 바인딩 계약(bindingslots.hlsli): 컴퓨트 UAV 는 u0/u1. 백엔드별 자리(DX12 루트 UAV, Vulkan set 0 binding 32+#,
// GL SSBO SW_GL_UAV_BINDING0+#)는 백엔드가 정한다. 엔진은 bindComputeUAV( index, 0/1 ) 로 건다.
SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( g_IndirectDrawBuffer, 0 );
SW_DECLARE_RW_BYTE_ADDRESS_BUFFER( g_IndirectDispatchBuffer, 1 );

#define GET_DRAW_BUFFER g_IndirectDrawBuffer
#define GET_DISPATCH_BUFFER g_IndirectDispatchBuffer

/**
 * @brief CSMain 컴퓨트 셰이더 진입점 (1,1,1 스레드 그룹)
 * @details DrawIndirect 커맨드 버퍼 및 DispatchIndirect 커맨드 버퍼의 파라미터를 동적으로 GPU 상에서 계산 및 저장
 */
[numthreads( 1, 1, 1 )] void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
	// 1. DrawIndirect 커맨드 바이트 버퍼 기록 (vertexCount: 6, instanceCount: 1)
	GET_DRAW_BUFFER.Store( 0, 6 );
	GET_DRAW_BUFFER.Store( 4, 1 );
	GET_DRAW_BUFFER.Store( 8, 0 );
	GET_DRAW_BUFFER.Store( 12, 0 );

	// 2. DispatchIndirect 커맨드 바이트 버퍼 기록 (threadGroupCountX: 4, Y: 1, Z: 1)
	GET_DISPATCH_BUFFER.Store( 0, 4 );
	GET_DISPATCH_BUFFER.Store( 4, 1 );
	GET_DISPATCH_BUFFER.Store( 8, 1 );
}
