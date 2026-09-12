#include "common.hlsli"

/**
 * meshmorph.hlsl — GPU 메시 모프 (컴퓨트).
 *
 * 레스트 포즈를 읽어 변형 결과를 **다른 버퍼**에 쓴다. 원본을 덮지 않는 이유는 둘이다 —
 * (1) 변형은 절대 시간의 함수라 매 프레임 원본에서 다시 만들어야 하고(누적하면 프레임 시간
 * 흔들림이 그대로 쌓여 백엔드마다 다른 모양이 나온다), (2) 예산이 모자라 이 메시가 풀에 못
 * 들어가면 레스트 포즈 그대로 그려야 한다. 언리얼 GPU Skin Cache 가 원본을 두고 결과를 캐시에
 * 쓰는 것과 같은 구성이다.
 *
 * 정점을 CPU 에서 매 프레임 다시 올리면 `Mesh::setVertices` 가 정점 버퍼를 파괴하고 다시 만든다 —
 * 메시마다, 프레임마다. 게다가 그 호출은 게임 스레드라 OpenGL 에서는 컨텍스트가 없다. 그래서 GPU 다.
 *
 * 바인딩 계약(bindingslots.hlsli): 컴퓨트 CB 는 b0, 레스트 읽기는 t0, 결과 쓰기는 u0.
 * C++: bindComputeConstantBuffer( cb, 0 ) / bindComputeShaderResource( restSrv, 0 ) / bindComputeUAV( morphUav, 0 ).
 */

// binding.hlsli 의 SwVertexData 와 **같은 레이아웃이어야 한다** — 정렬 사연은 그쪽 주석 참고.
struct SwVertexData
{
	float4 pos;
	float4 col;
};

SW_DECLARE_CBUFFER( MorphParams, SW_SLOT_COMPUTE_CB )
{
	float g_Time;         // 씬 시작부터의 절대 시간 (초)
	float g_Amplitude;    // 변형 크기 (로컬 단위) — 도형이 단위 크기라 0.5 를 넘으면 뭉개진다
	float g_Frequency;    // 공간 주파수 — 클수록 물결이 촘촘하다
	uint  g_MorphVertexCount;
};

SW_DECLARE_STRUCTURED_BUFFER( SwVertexData, g_RestVertices, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( SwVertexData, g_MorphVerticesRW, 0 );

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
	const uint idx = dtid.x;
	if (idx >= g_MorphVertexCount)
		return;

	SwVertexData vertex = g_RestVertices[idx];

	// 위치를 **원점에서 바깥으로** 밀고 당긴다. 법선이 따로 없으므로(RHIVertex 는 위치와 색뿐)
	// 원점 기준 방향을 법선 대신 쓴다 — 내장 도형이 전부 원점 중심이라 성립한다.
	const float3 rest   = vertex.pos.xyz;
	const float  radius = length(rest);
	const float3 dir    = radius > 1e-5f ? rest / radius : float3(0.0f, 1.0f, 0.0f);

	// 위상을 위치에서 뽑아 정점마다 어긋나게 한다 — 전부 같은 위상이면 도형이 통째로 커졌다 작아질 뿐
	// 모양이 변하지 않아, 변형이 실제로 걸렸는지 그림으로 구분할 수 없다.
	const float phase = (rest.x + rest.y + rest.z) * g_Frequency;
	const float wave  = sin(g_Time * 2.0f + phase);

	vertex.pos = float4(rest + dir * (wave * g_Amplitude), 1.0f);
	g_MorphVerticesRW[idx] = vertex;
}
