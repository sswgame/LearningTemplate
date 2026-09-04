#include "common.hlsli"

/**
 * Frustum cull: reset / keep indirect instance counts.
 * Phase-1: CPU pre-fills instanceCount; CS validates bounds against frustum planes.
 * Hi-Z occlusion can extend this pass later.
 */
struct GpuInstance
{
	float4x4 world;
	float3	 boundsCenter;
	float	 boundsRadius;
	uint	 meshBatchIndex;
	uint	 materialIndex;
	uint	 blendMode;
	uint	 pad;
};

struct DrawIndirectCommand
{
	uint vertexCount;
	uint instanceCount;
	uint startVertex;
	uint startInstance;
};

#if defined(VULKAN)
SW_DECLARE_CBUFFER( CullParams, 0 )
{
	float4 g_FrustumPlanes[6];
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint2  g_Pad;
};
SW_DECLARE_STRUCTURED_BUFFER_SPACE( GpuInstance, g_Instances, 0, 6 );
SW_DECLARE_RW_STRUCTURED_BUFFER_SPACE( DrawIndirectCommand, g_IndirectArgs, 0, 7 );
#elif defined(OPENGL)
SW_DECLARE_CBUFFER( CullParams, 0 )
{
	float4 g_FrustumPlanes[6];
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint2  g_Pad;
};
SW_DECLARE_STRUCTURED_BUFFER( GpuInstance, g_Instances, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( DrawIndirectCommand, g_IndirectArgs, 0 );
#else
SW_DECLARE_CBUFFER( CullParams, 0 )
{
	float4 g_FrustumPlanes[6];
	uint   g_InstanceCount;
	uint   g_BatchCount;
	uint2  g_Pad;
};
SW_DECLARE_STRUCTURED_BUFFER( GpuInstance, g_Instances, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( DrawIndirectCommand, g_IndirectArgs, 0 );
#endif

bool IsVisible(float3 center, float radius)
{
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float d = dot(g_FrustumPlanes[i].xyz, center) + g_FrustumPlanes[i].w;
		if (d < -radius)
			return false;
	}
	return true;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
	uint idx = dtid.x;
	if (idx >= g_BatchCount)
		return;

	DrawIndirectCommand cmd = g_IndirectArgs[idx];
	uint visible = 0;
	uint baseInst = cmd.startInstance;
	uint count = cmd.instanceCount;
	// Re-count visibles within the pre-batched instance range.
	for (uint i = 0; i < count; ++i)
	{
		uint instId = baseInst + i;
		if (instId >= g_InstanceCount)
			break;
		GpuInstance inst = g_Instances[instId];
		float3 center = mul(float4(inst.boundsCenter, 1.0f), inst.world).xyz;
		if (IsVisible(center, inst.boundsRadius))
			visible++;
	}
	cmd.instanceCount = visible;
	g_IndirectArgs[idx] = cmd;
}
