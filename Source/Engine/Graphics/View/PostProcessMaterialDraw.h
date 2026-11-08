/**
 * @file PostProcessMaterialDraw.h
 * @brief 뷰 하나가 그릴 후처리 머티리얼 하나입니다 — 게임 스레드가 블렌더블 경로를 읽기를 마친 머티리얼로 풀어 렌더 뷰 설정에 싣습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/View/PostProcessValues.h"

namespace sw
{
    /**
     * @struct PostProcessMaterialDraw
     * @brief 후처리 머티리얼 그리기 하나의 값입니다(값 타입 — 게임 스레드에서 렌더 스레드로 넘어간다). 렌더 스레드는 Material 을 보지 않고 이 사본만 읽는다(`MaterialRenderBinding`).
     * @details 만드는 곳은 `PostProcessMaterialSet::resolve`(블렌더블 차례 그대로), 읽는 곳은 `FramePostProcessMaterials::prepareView`(PSO · 원소 버퍼)와 PostProcessMaterial 패스다.
     */
    struct PostProcessMaterialDraw
    {
        MaterialRenderBinding        _binding;      ///< 원소 바이트 · stride · 텍스처 번호의 불변 사본(프록시는 쓰지 않는다 — 원소 버퍼는 렌더 스레드가 머티리얼마다 둔다)
        string                       _shaderPath;   ///< 머티리얼 셰이더(PostProcess 영역 — post_process_material.hlsli)
        hashed_string                _materialPath; ///< 원소 버퍼의 열쇠(같은 머티리얼을 여러 뷰가 그려도 한 번 올린다)
        float32                      _weight{ 0.0f };
        PostProcessBlendableLocation _location{ PostProcessBlendableLocation::None };
    };
} // namespace sw
