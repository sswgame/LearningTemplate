/**
 * @file ImGuiOpenGLRendererBackend.h
 * @brief ImGui OpenGL 렌더러 백엔드
 */
#pragma once
#include "Editor/Common/Backend/IImGuiRendererBackend.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief ImGui OpenGL 렌더러 */
    class ImGuiOpenGLRendererBackend : public IImGuiRendererBackend
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 생성은 기본값, GPU 리소스는 initialize / shutdown
        // ------------------------------------------------------------------------------
        /** @brief 멤버만 기본값으로 둡니다. 실제 생성은 initialize()에서 합니다. */
        ImGuiOpenGLRendererBackend() = default;
        /** @brief 리소스는 shutdown()에서 해제합니다. */
        virtual ~ImGuiOpenGLRendererBackend() override = default;

        /** @brief OpenGL ImGui 렌더러를 초기화합니다. */
        bool initialize( IRHIDevice* pRhiDevice ) override;
        /** @brief OpenGL ImGui 렌더러를 종료합니다. */
        void shutdown() override;

        // ------------------------------------------------------------------------------
        // 2) IImGuiRendererBackend — 프레임/텍스처
        //    unregisterTexture 는 해제할 자원을 소유하지 않아 아무것도 하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief ImGui OpenGL 프레임을 시작합니다. */
        void newFrame() override;
        /** @brief 대기 중인 폰트 아틀라스/텍스처 갱신을 렌더 스레드에서 처리합니다. */
        void processTextureUpdates() override;
        /** @brief GL 컨텍스트가 렌더 스레드 전용이므로 true 입니다. */
        bool requiresRenderThreadContext() const override { return true; }
        /** @brief ImGui draw data를 OpenGL로 그립니다. */
        void render( IRHIDevice* pRhiDevice, ImDrawData* pDrawData ) override;

        /** @brief RHI 텍스처 핸들을 ImGui ImTextureID(GLuint)로 등록합니다. */
        void* registerTexture( RHITextureHandle texture ) override;
        /** @brief OpenGL 에서는 따로 소유한 자원이 없어 아무것도 하지 않습니다(GL 텍스처 이름은 RHI 가 소유합니다). */
        void unregisterTexture( void* pTextureID ) override;

    private:
        IRHIDevice* _pRHIDevice{ nullptr };
    };
} // namespace sw::editor
