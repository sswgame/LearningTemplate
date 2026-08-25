/**
 * Fully-bindless sampling helpers.
 * - DX12: ResourceDescriptorHeap[PassCB index] (SM6.6)
 * - Vulkan: g_BindlessTextures[PassCB index] (descriptor indexing)
 * - DX11/OpenGL: t0..t3 slots; CPU binds from the same PassCB indices
 *
 * PassCB texture fields (invalid = 0xFFFFFFFF):
 *   g_TexShadow, g_TexAlbedo, g_TexNormal, g_TexDepth, g_TexSource, g_TexSourceDepth
 */

#ifndef SW_ENGINE_BINDLESS_HLSLI
#define SW_ENGINE_BINDLESS_HLSLI

#include "common.hlsli"

static const uint kInvalidBindlessIndex = 0xFFFFFFFFu;

struct PassCBData
{
	float4x4 g_LightViewProj;
	float4x4 g_ViewProj;
	float4x4 g_World;
	float4x4 g_CascadeViewProj[4];
	float4	 g_CascadeSplits;
	float4	 g_KeyLightDirIntensity;
	float4	 g_KeyLightColor;
	float4	 g_ShadowParams;
	float4	 g_BloomParams;
	float4	 g_OutlineColor;
	float4	 g_OutlineParams;
	uint	 g_CascadeCount;
	uint	 g_TexShadow;
	uint	 g_TexAlbedo;
	uint	 g_TexNormal;
	uint	 g_TexDepth;
	uint	 g_TexSource;
	uint	 g_TexSourceDepth;
	uint	 g_Flags;
};

// Face-aligned normal for the axis-aligned demo unit cube.
float3 DemoCubeNormal(float3 pos)
{
	float3 a = abs(pos);
	if (a.x >= a.y && a.x >= a.z)
		return float3(sign(pos.x), 0.0f, 0.0f);
	if (a.y >= a.x && a.y >= a.z)
		return float3(0.0f, sign(pos.y), 0.0f);
	return float3(0.0f, 0.0f, sign(pos.z));
}

#if defined(SW_BINDLESS) && defined(DX12)

SW_DECLARE_CBUFFER_SPACE( RootConstants, 0, 1 )
{
	uint g_BindlessCbIndex;
};

SW_DECLARE_SAMPLER( g_BindlessSampler, 0 );

PassCBData GetPassCB()
{
	ConstantBuffer<PassCBData> cb = ResourceDescriptorHeap[g_BindlessCbIndex];
	return cb;
}

float4 SampleBindlessIndex(uint index, float2 uv)
{
	if (index == kInvalidBindlessIndex)
		return float4(0, 0, 0, 1);
	Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(index)];
	return tex.Sample(g_BindlessSampler, uv);
}

#elif defined(SW_BINDLESS) && defined(VULKAN)

SW_DECLARE_CBUFFER( PassCB, 0 )
{
	PassCBData g_Pass;
};

SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( g_BindlessTextures, 0, 1 );
SW_DECLARE_SAMPLER_SPACE( g_BindlessSampler, 0, 1 );

PassCBData GetPassCB()
{
	return g_Pass;
}

float4 SampleBindlessIndex(uint index, float2 uv)
{
	if (index == kInvalidBindlessIndex)
		return float4(0, 0, 0, 1);
	return g_BindlessTextures[NonUniformResourceIndex(index)].Sample(g_BindlessSampler, uv);
}

#else

SW_DECLARE_CBUFFER( PassCB, 0 )
{
	PassCBData g_Pass;
};

SW_DECLARE_TEXTURE2D_SAMPLER( g_Slot0, g_Slot0Sampler, 0, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_Slot1, g_Slot1Sampler, 1, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_Slot2, g_Slot2Sampler, 2, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_Slot3, g_Slot3Sampler, 3, 0 );

PassCBData GetPassCB()
{
	return g_Pass;
}

// Emulation: FrameRenderer binds [shadow/source, albedo/srcDepth, normal, depth/shadow] → t0..t3
float4 SampleBindlessIndex(uint index, float2 uv)
{
	PassCBData p = g_Pass;
	if (index == kInvalidBindlessIndex)
		return float4(0, 0, 0, 1);
	if (index == p.g_TexShadow || index == p.g_TexSource)
		return g_Slot0.Sample(g_Slot0Sampler, uv);
	if (index == p.g_TexAlbedo || index == p.g_TexSourceDepth)
		return g_Slot1.Sample(g_Slot1Sampler, uv);
	if (index == p.g_TexNormal)
		return g_Slot2.Sample(g_Slot2Sampler, uv);
	if (index == p.g_TexDepth)
		return g_Slot3.Sample(g_Slot3Sampler, uv);
	return g_Slot0.Sample(g_Slot0Sampler, uv);
}

#endif

float4 SampleShadow(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexShadow, uv);
}
float4 SampleAlbedo(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexAlbedo, uv);
}
float4 SampleNormal(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexNormal, uv);
}
float4 SampleDepth(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexDepth, uv);
}
float4 SampleSource(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexSource, uv);
}
float4 SampleSourceDepth(float2 uv)
{
	return SampleBindlessIndex(GetPassCB().g_TexSourceDepth, uv);
}

#endif // SW_ENGINE_BINDLESS_HLSLI
