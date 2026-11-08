/**
 * @file FramePostProcessMaterials.h
 * @brief 후처리 머티리얼의 렌더 스레드 쪽 준비입니다 — 머티리얼마다의 원소 버퍼(g_SwMaterials 원소 하나)와 패스 자리마다의 PSO 를 셋업에서 만들어 뷰에 둡니다.
 * @details 흐름: 게임 스레드(`PostProcessMaterialSet`)가 블렌더블을 머티리얼 사본으로 풀어 뷰 설정에 싣고 → 셋업(`prepareView`, 뷰마다 프레임에 한 번)이 PSO 와 버퍼를 맞춰
 *          `FrameViewTarget::_listPostProcessMaterialPass` 에 두고 → PostProcessMaterial 패스(`FramePassExecutor::executePostProcessMaterialPass`)가 그린다.
 *          버퍼 생성 · bindless 등록은 기록 중에 할 수 없어(패스가 병렬로 기록된다) 셋업에서 한다. 언리얼 `AddPostProcessMaterialPass` 의 머티리얼 셰이더 · 파라미터 준비 자리다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct FrameViewTarget;
    struct MaterialRenderData;

    class FramePSOManager;
    class IRHIDevice;
    class RenderPipelineAsset;

    /**
     * @class FramePostProcessMaterials
     * @brief 렌더러가 소유하는 후처리 머티리얼 준비입니다. 디바이스 · 파이프라인 · PSO 관리자는 렌더러의 것을 참조로 읽는다.
     */
    class SW_API FramePostProcessMaterials
    {
    public:
        FramePostProcessMaterials( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FramePSOManager& psoManager );
        ~FramePostProcessMaterials();
        FramePostProcessMaterials( const FramePostProcessMaterials& )            = delete;
        FramePostProcessMaterials& operator=( const FramePostProcessMaterials& ) = delete;

        /**
         * @brief (셋업) @p view 가 이번 프레임에 그릴 후처리 머티리얼의 PSO · 원소 버퍼를 맞춰 `_listPostProcessMaterialPass` 에 둡니다(설정 `_listPostProcessMaterial` 과 같은 차례).
         * @details 파이프라인에 그 자리의 PostProcessMaterial 패스가 없는 블렌더블은 싣지 않는다(그릴 패스가 없다). PSO 를 만들지 못한 머티리얼은 PSO 0 으로 실려 그 그리기만 빠진다(이유는 PSO 관리자가 남겼다).
         */
        void prepareView( FrameViewTarget& view );
        /** @brief 이번 프레임에 어느 뷰도 쓰지 않은 원소 버퍼를 놓습니다(프레임 끝). */
        void pruneUnused();
        /** @brief 원소 버퍼를 모두 놓습니다(디바이스가 바뀌거나 내려갈 때). 디바이스가 없으면 핸들만 잊는다. */
        void release();

    private:
        /** @brief 머티리얼 하나의 원소 버퍼입니다. 사본이 바뀐 때만 다시 올린다(`_uploaded`). */
        struct ElementBuffer
        {
            RHIStructuredBuffer                  _buffer;
            shared_ptr<const MaterialRenderData> _uploaded;
            uint64                               _lastUsedFrame{ 0 };
        };

        /** @brief @p data 를 @p materialPath 의 원소 버퍼에 올립니다. 못 만들면 nullptr 입니다(오류를 남긴다). */
        const RHIStructuredBuffer* ensureElementBuffer( const hashed_string& materialPath, const shared_ptr<const MaterialRenderData>& data );

    private:
        IRHIDevice* const&                          _pDevice;
        const RenderPipelineAsset&                  _pipeline;
        FramePSOManager&                            _psoManager;
        unordered_map<hashed_string, ElementBuffer> _mapElementBuffer;
        uint64                                      _frame;
    };
} // namespace sw
