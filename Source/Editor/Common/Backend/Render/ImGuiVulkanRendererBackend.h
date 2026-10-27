/**
 * @file ImGuiVulkanRendererBackend.h
 * @brief ImGui Vulkan 렌더러 백엔드
 */
#pragma once
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"

struct ImGuiViewport;
struct ImVec2;
struct VkDescriptorPool_T;
struct VkDevice_T;
struct VkSampler_T;

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief ImGui Vulkan 렌더러 (디스크립터 풀 · 샘플러) */
    class ImGuiVulkanRendererBackend : public IImGuiRendererBackend
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 생성은 기본값, GPU 리소스는 initialize / shutdown
        // ------------------------------------------------------------------------------
        /** @brief 멤버만 기본값으로 둡니다. 실제 생성은 initialize()에서 합니다. */
        ImGuiVulkanRendererBackend() = default;
        /** @brief 리소스는 shutdown()에서 해제합니다. */
        virtual ~ImGuiVulkanRendererBackend() override = default;

        /** @brief Vulkan ImGui 렌더러를 초기화합니다. */
        [[nodiscard]] bool initialize( IRHIDevice* pRHIDevice ) override;
        /** @brief Vulkan ImGui 렌더러를 종료합니다. */
        void shutdown() override;

        // ------------------------------------------------------------------------------
        // 2) IImGuiRendererBackend — 프레임/텍스처
        // ------------------------------------------------------------------------------
        /** @brief ImGui Vulkan 프레임을 시작합니다. */
        void newFrame() override;
        /** @brief 대기 중인 폰트 아틀라스/텍스처 갱신을 UI 스레드에서 처리합니다. 업로드가 큐에 제출하므로 큐 잠금 안에서 합니다. */
        void processTextureUpdates() override;
        /** @brief RHI 디바이스의 그래픽스 큐 잠금입니다(초기화 전 · 종료 뒤는 null). */
        mutex* getSubmissionQueueMutex() override { return _pQueueMutex.get(); }
        /** @brief ImGui draw data를 Vulkan으로 그립니다. */
        void render( IRHIDevice* pRHIDevice, ImDrawData* pDrawData ) override;

        /** @brief RHI 텍스처를 ImGui용으로 등록합니다. */
        void* registerTexture( RHITextureHandle texture ) override;
        /** @brief 등록된 ImGui 텍스처를 해제합니다. 디스크립터 세트는 그것을 그린 마지막 프레임의 GPU 완료 뒤에 풀로 돌아갑니다. */
        void unregisterTexture( void* pTextureID ) override;

        /**
         * @brief 가로챈 ImGui 렌더러 콜백(보조 뷰포트 창 생성 · 크기 변경)의 원래 함수입니다. 백엔드가 소유합니다.
         * @details 콜백에는 사용자 포인터가 없어 설치할 때 ImGui 컨텍스트의 사용자 칸(`ImGuiIO::UserData`)에 걸고 가로채기 함수가 거기서 찾는다.
         *          에디터 범위(상태 저장소)는 렌더러 백엔드보다 늦게 생기므로 쓰지 않는다.
         */
        struct ViewportGuard
        {
            void ( *_pOriginalCreateWindow )( ImGuiViewport* )          = nullptr;
            void ( *_pOriginalSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;
        };

    private:
        ViewportGuard                          _viewportGuard;
        OnceInit<VkDescriptorPool_T*>          _pImguiDescriptorPool;
        OnceInit<VkDevice_T*>                  _pDevice;
        OnceInit<VkSampler_T*>                 _pSampler;
        OnceInit<IRHIDevice*>                  _pRHIDevice;
        OnceInit<mutex*>                       _pQueueMutex; ///< RHI 디바이스의 큐 제출 잠금(`RHINativeHandles::_pQueueMutex`)
        unordered_map<void*, RHITextureHandle> _mapTextureID;
        /// @brief 디스크립터 풀 잠금입니다. 세트를 잡는 UI 스레드와, 미뤄 둔 해제를 부르는 렌더 스레드가 같은 풀을 씁니다.
        mutex _descriptorPoolMutex;
    };
} // namespace sw::editor
