/**
 * @file GPUMeshBatchPSO.h
 * @brief 메시 배치를 그릴 PSO 하나와 그 바인딩 레이아웃 · 만들기 상태입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

namespace sw
{
    class ShaderBindingLayout;

    /**
     * @struct GPUMeshBatchPSO
     * @brief 메시 배치를 그릴 PSO 하나입니다(같은 변형 조건의 배치가 나눠 쓴다).
     */
    struct GPUMeshBatchPSO
    {
        RHIPipelineStateHandle           _pso{ 0 };           ///< 그릴 PSO — 0 이면(만드는 중 · 실패 · 없음) 그 배치는 그리지 않는다. `_state` 가 `Building` 인데 0 이 아니면 직전 변형이다
        const ShaderBindingLayout*       _pLayout{ nullptr }; ///< `_pso` 의 바인딩 레이아웃
        RenderPSOCache::MaterialPSOState _state{ RenderPSOCache::MaterialPSOState::Absent };
    };
} // namespace sw
