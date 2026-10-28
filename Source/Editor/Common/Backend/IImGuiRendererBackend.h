/**
 * @file IImGuiRendererBackend.h
 * @brief ImGui GPU 렌더러 백엔드의 추상 인터페이스입니다. RHI 마다 구현합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Backend/Model/EditorDrawReleaseQueue.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    enum class RHIBackend : uint32;

    class IRHIDevice;
} // namespace sw

struct ImDrawData;
struct ImTextureData;

namespace sw::editor
{
    using RHITextureHandle = uint64;

    /**
     * @brief 렌더 스레드가 한 프레임에 같은 뷰포트(메인)의 그리기 데이터를 `IImGuiRendererBackend::render` 로 그리는 최대 횟수입니다.
     * @details 메인 UI 한 번과 에디터 UI 캡처(`EditorUICapture::renderJob`, 주인 뷰포트가 메인인 합본) 한 번이다. 부르는 곳은 `ImGuiEditor::render` 하나다.
     */
    inline constexpr uint32 kImGuiDrawsPerViewportPerFrame = 2u;

    /**
     * @brief ImGui 렌더러 백엔드(Vulkan, DX12)가 뷰포트마다 두는 정점 버퍼와 인덱스 버퍼 링의 슬롯 수입니다.
     * @details 두 백엔드는 `RenderDrawData` 를 부를 **때마다** 링을 한 슬롯 넘긴다. 슬롯이 모자라면 그 슬롯의 버퍼를 GPU 를 기다리지 않고 바로 지우고
     *          새로 만든다(`imgui_impl_vulkan` 의 `CreateOrResizeBuffer`, `imgui_impl_dx12` 의 `SafeRelease`). 그래서 GPU 가 슬롯을 다 읽은 뒤에야
     *          그 슬롯이 다시 돌아와야 한다. 한 프레임에 여러 번 그리면 링이 프레임보다 빨리 돈다. 링을 인플라이트 프레임 수로만 두면 캡처 프레임이
     *          아직 GPU 에 있는 앞 프레임의 슬롯을 다시 쓰고, 그 슬롯을 키우면 쓰는 중인 버퍼를 지운다
     *          (Vulkan 검증 레이어 VUID-vkDestroyBuffer-buffer-00922, 2026-10-20 `editor.inspectorhandle`).
     *          슬롯 수를 "인플라이트 프레임 수 × 프레임당 그리는 수" 로 두면 어느 슬롯이든 인플라이트 프레임 수만큼 뒤의 프레임에서야 다시 쓰인다.
     */
    inline constexpr uint32 kImGuiDrawBufferRingSize = constant::kMaxFrameCountInFlight * kImGuiDrawsPerViewportPerFrame;

    /** @brief ImGui GPU 렌더러 백엔드입니다. DX11, DX12, Vulkan, OpenGL 이 각자 구현합니다. */
    class IImGuiRendererBackend
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 주기와 프레임
        // ------------------------------------------------------------------------------
        /** @brief 파생 렌더러가 GPU 리소스를 해제할 수 있도록 소멸을 가상으로 둡니다. */
        virtual ~IImGuiRendererBackend() = default;

        /** @brief RHI 디바이스에 ImGui 렌더러를 바인딩하고 초기화합니다. */
        [[nodiscard]] virtual bool initialize( IRHIDevice* pRHIDevice ) = 0;
        /** @brief ImGui 렌더러 리소스를 해제합니다. */
        virtual void shutdown() = 0;
        /** @brief 프레임을 시작할 때 ImGui 렌더러 상태를 갱신합니다. */
        virtual void newFrame() = 0;
        /** @brief 주어진 DrawData 를 RHI 로 그립니다. nullptr 이면 그리지 않습니다. 한 프레임에 한 뷰포트로 `kImGuiDrawsPerViewportPerFrame` 번까지 부릅니다. */
        virtual void render( IRHIDevice* pRHIDevice, ImDrawData* pDrawData ) = 0;
        /**
         * @brief 대기 중인 ImGui 텍스처 생성과 갱신(폰트 아틀라스 재빌드 등)을 즉시 처리합니다.
         * @details ImGui 1.92 동적 아틀라스에서 draw-data 스냅샷을 다른 스레드로 넘길 때, 텍스처 갱신은
         *          렌더 스레드가 아니라 UI 스레드에서 이 호출로 끝내야 한다. 공유 리스트의 레이스를 막기 위해서다.
         */
        virtual void processTextureUpdates() {}

        /**
         * @brief 프레임 GPU 작업(newFrame, 텍스처 갱신, 보조 뷰포트 렌더)을 렌더 스레드에서 해야 하면 true 입니다.
         * @details OpenGL 처럼 GPU 컨텍스트가 스레드 전용(wglMakeCurrent)인 백엔드는 UI 스레드에서 컨텍스트를 잡을 수 없다. DX11 은 즉시 컨텍스트의
         *          파이프라인 상태를 렌더 스레드의 프레임 스트림과 나눠 쓰므로 같은 쪽이다.
         *          이 값이 true 이면 ImGuiEditor 가 해당 호출들을 present 훅(렌더 스레드)으로 옮겨 실행한다. DX12, Vulkan 은 false 이다.
         */
        virtual bool requiresRenderThreadContext() const { return false; }

        /**
         * @brief 보조(플로팅) 뷰포트를 렌더하고 present 합니다. `requiresRenderThreadContext()` 가 true 면 렌더 스레드(present 훅)에서, 아니면 UI 스레드에서 부릅니다.
         * @details 기본은 `ImGui::RenderPlatformWindowsDefault` 다. 컨텍스트를 렌더 스레드의 프레임 스트림과 나눠 쓰는 백엔드(DX11)는 그 잠금과 뒷정리를 더한다.
         */
        virtual void renderPlatformWindows();

        /**
         * @brief UI 스레드가 ImGui 백엔드 코드로 GPU 큐에 제출하고 대기하는 동안 잡는 락입니다. 보조 뷰포트의 생성, 렌더, 파괴와 텍스처 업로드가 해당합니다.
         * @details Vulkan 큐는 외부 동기화 대상이므로 렌더 스레드의 제출과 겹치면 안 된다. Vulkan 백엔드는 RHI 디바이스의 큐 락을
         *          돌려준다(`RHINativeHandles::_pQueueMutex`). 다른 백엔드는 null 이다. 부르는 쪽은 `ScopedOptionalLock` 으로 잡고, 다른 락보다 먼저 잡는다.
         */
        virtual mutex* getSubmissionQueueMutex() { return nullptr; }

        // ------------------------------------------------------------------------------
        // 2) ImGui 텍스처(씬 뷰와 게임 뷰의 렌더 타깃 등)
        // ------------------------------------------------------------------------------
        /** @brief RHI 텍스처를 ImGui 텍스처 ID 로 등록하고 핸들을 반환합니다. */
        virtual void* registerTexture( RHITextureHandle texture ) = 0;
        /**
         * @brief registerTexture 로 발급한 ImGui 텍스처 ID 를 해제합니다. UI 스레드에서 호출합니다.
         * @details 이미 낸 draw 스냅샷이 그 ID 를 그릴 수 있으므로, 네이티브 리소스(디스크립터)를 해제하는 일은 `getDrawReleaseQueue()` 에 맡긴다.
         *          ID 는 호출한 즉시 무효가 된다. 같은 UI 프레임에서 다시 그리지 않는다.
         */
        virtual void unregisterTexture( void* pTextureID ) = 0;

        /**
         * @brief UI 스레드에서 해제한 리소스(ImGui 텍스처와 그것이 가리키는 RHI 텍스처)의 해제 큐입니다.
         * @details 해제는 그 리소스를 그렸을 수 있는 마지막 프레임의 GPU 작업이 끝난 뒤에 실행된다(`EditorDrawReleaseQueue`). `ImGuiEditor` 가 스냅샷을 낼 때
         *          번호를 알리고, 렌더 스레드가 스냅샷을 그린 프레임에서 디바이스로 넘긴다.
         */
        EditorDrawReleaseQueue& getDrawReleaseQueue() { return _drawReleaseQueue; }

        // ------------------------------------------------------------------------------
        // 3) 팩토리. RHI 백엔드별로 구현한다
        // ------------------------------------------------------------------------------
        /** @brief 주어진 RHI 백엔드에 맞는 렌더러 구현을 만듭니다. 그 백엔드용 구현이 없으면 nullptr 입니다. */
        static unique_ptr<IImGuiRendererBackend> createRendererBackend( RHIBackend backend );

    protected:
        /**
         * @brief 갱신 대기 중인 ImGui 텍스처를 백엔드 UpdateTexture 로 모두 처리합니다.
         * @param pUpdateTexture ImGui_Impl*_UpdateTexture 함수 포인터입니다.
         * @details 네 백엔드가 함께 쓴다. ImGui_Impl*_RenderDrawData 가 draw_data->Textures 를 순회하며 하는 일인데,
         *          draw-data 스냅샷은 그 리스트를 공유하지 않으므로 그리기 전에 끝내야 한다. 호출하는
         *          스레드는 requiresRenderThreadContext() 에 따라 UI 스레드와 렌더 스레드로 나뉜다.
         */
        static void updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) );

        /**
         * @brief 종료의 첫 단계입니다. 렌더 스레드와 GPU 를 기다려(`IRHIDevice::waitIdle`) 디바이스로 넘긴 해제를 실행하게 하고, 넘기지 않은 해제도 지금 실행합니다.
         * @details 해제 콜백은 이 모듈의 코드와 백엔드 객체를 가리킨다. 모듈을 언로드하거나 디스크립터 풀과 힙을 파괴하기 전에 모두 실행되어야 한다.
         */
        void flushDrawReleases( IRHIDevice* pRHIDevice );

    private:
        EditorDrawReleaseQueue _drawReleaseQueue;
    };
} // namespace sw::editor
