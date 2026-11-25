/**
 * @file EngineComputeShaders.h
 * @brief 엔진 컴퓨트 셰이더의 매개변수 구조체입니다 — 셰이더 파일 하나(또는 커널 묶음)에 형식 하나, 이름은 셰이더의 선언 이름 그대로입니다.
 * @details PSO 저장소가 컴퓨트 PSO 를 만들 때 패스 종류의 표(`RenderPassKindInfo::_computeParameters`)로 이 표를 리플렉션에 풀고, 패스 몸통은
 *          `ComputeShaderParameters<형식>` 에 서수로 값을 채워 건다. 셰이더에 자원을 더하면 여기 줄을 더한다 — 빠뜨리면 PSO 생성이 이름과 함께 오류로 멈춘다.
 *          셰이더 메시지 링(`g_GPUMessageRW`, gpu_message.hlsli)은 메시지 변형 PSO 에만 있다 — 변형이 아니면 걸지 않는다.
 */
#pragma once
#include "Engine/Graphics/Shader/Binding/ComputeShaderParameters.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace computeshader
    {
        using Kind                                = ComputeShaderParameterKind;
        inline constexpr const utf8* kRWSlot0Name = "g_SwRWSlot0"; ///< binding_texture.hlsli 의 RW 텍스처 칸(에뮬 백엔드만, u5..u7 — `shaderslot::resname::kRwTextureSlot` + 서수)
        inline constexpr const utf8* kRWSlot1Name = "g_SwRWSlot1";
        inline constexpr const utf8* kRWSlot2Name = "g_SwRWSlot2";
    } // namespace computeshader

    /** @brief gpu_cull.hlsl — 컬링과 같은 파일의 간접 인자 되돌리기 · 모으기 커널(`GPUCull` · `IndirectArgsReset` · `IndirectArgsCompact`)이 같은 표를 쓴다. */
    struct GPUCullShader
    {
        enum Parameter : uint8
        {
            CullParams,
            Instances,
            BatchInfo,
            HZB,
            InstanceOrder,
            IndirectArgs,
            VisibleInstanceIDs,
            LODState,
            OccludedOrders,
            GPUMessages,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {        CullParams,                         "CullParams", computeshader::Kind::ConstantBuffer},
            {         Instances,                        "g_Instances",         computeshader::Kind::Buffer},
            {         BatchInfo,                        "g_BatchInfo",         computeshader::Kind::Buffer},
            {               HZB,                              "g_HZB",         computeshader::Kind::Buffer},
            {     InstanceOrder,                    "g_InstanceOrder",         computeshader::Kind::Buffer},
            {      IndirectArgs,                     "g_IndirectArgs",            computeshader::Kind::UAV},
            {VisibleInstanceIDs,               "g_VisibleInstanceIDs",            computeshader::Kind::UAV},
            {          LODState,                         "g_LODState",            computeshader::Kind::UAV},
            {    OccludedOrders,                   "g_OccludedOrders",            computeshader::Kind::UAV},
            {       GPUMessages, shaderslot::resname::kGPUMessageRing,            computeshader::Kind::UAV},
        };
    };

    /** @brief instance_sort.hlsl — 그룹 정렬(`InstanceSort`)과 여러 단계 정렬(`InstanceSortMultiPass`)이 같은 표를 쓴다. */
    struct InstanceSortShader
    {
        enum Parameter : uint8
        {
            SortParams,
            Instances,
            BatchInfo,
            ViewRank,
            InstanceOrder,
            IndirectArgs,
            VisibleInstanceIDs,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {        SortParams,           "SortParams", computeshader::Kind::ConstantBuffer},
            {         Instances,          "g_Instances",         computeshader::Kind::Buffer},
            {         BatchInfo,          "g_BatchInfo",         computeshader::Kind::Buffer},
            {          ViewRank,           "g_ViewRank",         computeshader::Kind::Buffer},
            {     InstanceOrder,      "g_InstanceOrder",         computeshader::Kind::Buffer},
            {      IndirectArgs,       "g_IndirectArgs",            computeshader::Kind::UAV},
            {VisibleInstanceIDs, "g_VisibleInstanceIDs",            computeshader::Kind::UAV},
        };
    };

    /** @brief instance_animation.hlsl */
    struct InstanceAnimationShader
    {
        enum Parameter : uint8
        {
            AnimationParams,
            Instances,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {AnimationParams, "AnimationParams", computeshader::Kind::ConstantBuffer},
            {      Instances,   "g_InstancesRW",            computeshader::Kind::UAV},
        };
    };

    /** @brief mesh_morph.hlsl */
    struct MeshMorphShader
    {
        enum Parameter : uint8
        {
            MorphParams,
            RestVertices,
            DeformPrograms,
            MorphVertices,
            GPUMessages,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   MorphParams,                        "MorphParams", computeshader::Kind::ConstantBuffer},
            {  RestVertices,                     "g_RestVertices",         computeshader::Kind::Buffer},
            {DeformPrograms,                   "g_DeformPrograms",         computeshader::Kind::Buffer},
            { MorphVertices,                  "g_MorphVerticesRW",            computeshader::Kind::UAV},
            {   GPUMessages, shaderslot::resname::kGPUMessageRing,            computeshader::Kind::UAV},
        };
    };

    /** @brief mesh_skin.hlsl */
    struct MeshSkinShader
    {
        enum Parameter : uint8
        {
            SkinParams,
            RestVertices,
            SkinWeights,
            SkinPalette,
            SkinInstances,
            MorphVertices,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   SkinParams,        "SkinParams", computeshader::Kind::ConstantBuffer},
            { RestVertices,    "g_RestVertices",         computeshader::Kind::Buffer},
            {  SkinWeights,     "g_SkinWeights",         computeshader::Kind::Buffer},
            {  SkinPalette,     "g_SkinPalette",         computeshader::Kind::Buffer},
            {SkinInstances,   "g_SkinInstances",         computeshader::Kind::Buffer},
            {MorphVertices, "g_MorphVerticesRW",            computeshader::Kind::UAV},
        };
    };

    /** @brief light_grid.hlsl */
    struct LightGridShader
    {
        enum Parameter : uint8
        {
            Lights,
            Views,
            Grid,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {Lights, "g_LightGridLights", computeshader::Kind::Buffer},
            { Views,  "g_LightGridViews", computeshader::Kind::Buffer},
            {  Grid,     "g_LightGridRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief hzb_build.hlsl — 깊이 히스토리는 에뮬 백엔드의 RW 칸 0 이다(네이티브는 루트 상수의 번호로 읽는다). */
    struct HZBBuildShader
    {
        enum Parameter : uint8
        {
            HZB,
            TileDepths,
            DepthHistory,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {         HZB,                   "g_HZBRW", computeshader::Kind::UAV},
            {  TileDepths,        "g_OcclusionTilesRW", computeshader::Kind::UAV},
            {DepthHistory, computeshader::kRWSlot0Name, computeshader::Kind::UAV},
        };
    };

    /** @brief volumetric_fog.hlsl */
    struct VolumetricFogShader
    {
        enum Parameter : uint8
        {
            Lights,
            Params,
            LightGrid,
            Volume,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   Lights,    "g_FogLights", computeshader::Kind::Buffer},
            {   Params,    "g_FogParams", computeshader::Kind::Buffer},
            {LightGrid, "g_FogLightGrid", computeshader::Kind::Buffer},
            {   Volume,  "g_FogVolumeRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief vfx_simulate.hlsl — 장면 깊이는 에뮬 백엔드의 RW 칸 0 이다. */
    struct VFXSimulationShader
    {
        enum Parameter : uint8
        {
            Params,
            State,
            Particles,
            Args,
            Lights,
            SceneDepth,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {    Params,               "g_VFXParams", computeshader::Kind::Buffer},
            {     State,              "g_VFXStateRW",    computeshader::Kind::UAV},
            { Particles,          "g_VFXParticlesRW",    computeshader::Kind::UAV},
            {      Args,               "g_VFXArgsRW",    computeshader::Kind::UAV},
            {    Lights,             "g_VFXLightsRW",    computeshader::Kind::UAV},
            {SceneDepth, computeshader::kRWSlot0Name,    computeshader::Kind::UAV},
        };
    };

    /** @brief vfx_particle_sort.hlsl */
    struct VFXParticleSortShader
    {
        enum Parameter : uint8
        {
            Params,
            Particles,
            Args,
            Order,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   Params,    "g_VFXParams", computeshader::Kind::Buffer},
            {Particles, "g_VFXParticles", computeshader::Kind::Buffer},
            {     Args,      "g_VFXArgs", computeshader::Kind::Buffer},
            {    Order,   "g_VFXOrderRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief vfx_mesh_instance.hlsl */
    struct VFXMeshInstanceShader
    {
        enum Parameter : uint8
        {
            Params,
            Particles,
            Args,
            Instances,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   Params,      "g_VFXParams", computeshader::Kind::Buffer},
            {Particles,   "g_VFXParticles", computeshader::Kind::Buffer},
            {     Args,        "g_VFXArgs", computeshader::Kind::Buffer},
            {Instances, "g_VFXInstancesRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief scene_voxelize.hlsl */
    struct SceneVoxelizeShader
    {
        enum Parameter : uint8
        {
            Jobs,
            Positions,
            Indices,
            VoxelGrid,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {     Jobs,      "g_VoxelJobs", computeshader::Kind::Buffer},
            {Positions, "g_VoxelPositions", computeshader::Kind::Buffer},
            {  Indices,   "g_VoxelIndices", computeshader::Kind::Buffer},
            {VoxelGrid,        "g_VoxelRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief probe_update.hlsl */
    struct ProbeUpdateShader
    {
        enum Parameter : uint8
        {
            Lights,
            Params,
            VoxelGrid,
            History,
            Output,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {   Lights,    "g_ProbeLights", computeshader::Kind::Buffer},
            {   Params,    "g_ProbeParams", computeshader::Kind::Buffer},
            {VoxelGrid,    "g_ProbeVoxels", computeshader::Kind::Buffer},
            {  History, "g_ProbeHistoryRW",    computeshader::Kind::UAV},
            {   Output,     "g_ProbeOutRW",    computeshader::Kind::UAV},
        };
    };

    /** @brief canvas_marker.hlsl */
    struct CanvasMarkerShader
    {
        enum Parameter : uint8
        {
            MarkerRows,
            MarkerInstances,
            Quads,
            GPUMessages,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {     MarkerRows,                       "g_MarkerRows", computeshader::Kind::Buffer},
            {MarkerInstances,                  "g_MarkerInstances", computeshader::Kind::Buffer},
            {          Quads,                    "g_MarkerQuadsRW",    computeshader::Kind::UAV},
            {    GPUMessages, shaderslot::resname::kGPUMessageRing,    computeshader::Kind::UAV},
        };
    };

    /** @brief deferred_tile_classify.hlsl */
    struct DeferredTileClassifyShader
    {
        enum Parameter : uint8
        {
            TileArgs,
            TileList,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {TileArgs, "g_DeferredTileArgsRW", computeshader::Kind::UAV},
            {TileList, "g_DeferredTileListRW", computeshader::Kind::UAV},
        };
    };

    /** @brief gpu_nan_check.hlsl */
    struct GPUNanCheckShader
    {
        enum Parameter : uint8
        {
            Counts,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            { Counts, "g_NanCountRW", computeshader::Kind::UAV },
        };
    };

    /**
     * @brief 파이프라인 XML 의 컴퓨트 패스(compute_pass.hlsli)와 풀스크린 패스의 컴퓨트 구현(post_compute.hlsli) — 첨부는 에뮬 백엔드의 RW 칸 0(출력) · 1 · 2(입력)이다.
     * @details 네이티브 bindless 에서는 셰이더가 루트 상수의 RW 번호로 읽어 이 칸이 리플렉션에 없다(걸지 않는다).
     */
    struct ComputePassShader
    {
        enum Parameter : uint8
        {
            Output,
            Input0,
            Input1,
            Count
        };
        static constexpr ComputeShaderParameterInfo kParameters[Count] = {
            {Output, computeshader::kRWSlot0Name, computeshader::Kind::UAV},
            {Input0, computeshader::kRWSlot1Name, computeshader::Kind::UAV},
            {Input1, computeshader::kRWSlot2Name, computeshader::Kind::UAV},
        };
    };
} // namespace sw
