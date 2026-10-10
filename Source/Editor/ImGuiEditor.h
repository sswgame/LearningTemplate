/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 호스트입니다(OS/GPU 백엔드 · 프레임 루프).
 */
#pragma once
#include "Core/Concurrency/atomic.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/IEditor.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    struct EditorToolDefaults;

    class EditorContext;
    class IImGuiPlatformBackend;
    class IImGuiRendererBackend;

    /** @brief ImGui 플랫폼 · 렌더러 호스트입니다. 메뉴 · 도크 · 폰트는 Common 에 맡깁니다. */
    class ImGuiEditor : public IEditor
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 주기 (생성자는 플래그만, GPU · ImGui 는 initialize / shutdown 에서)
        // ------------------------------------------------------------------------------
        /** @brief ImGui 에디터 셸을 생성합니다. */
        ImGuiEditor();
        /** @brief ImGui 에디터 셸을 파괴합니다. */
        virtual ~ImGuiEditor() override;

        // ------------------------------------------------------------------------------
        // 2) IEditor — 초기화 / 프레임 / 이벤트 / 텍스처
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼 백엔드·렌더러·폰트를 초기화합니다. */
        bool initialize( IWindow* pWindow, IRHIDevice* pRHIDevice ) override;
        /** @brief 에디터 리소스를 해제합니다. */
        void shutdown() override;
        /** @brief 메인 스레드에서 ImGui 프레임을 갱신하고, 패널을 그리고, 플랫폼 창을 갱신합니다. */
        void updateUi() override;
        /** @brief UI 를 그리기 전에 패널의 GPU 작업을 합니다. */
        void preRender( IRHIDevice* pRHIDevice ) override;
        /** @brief 에디터 UI 의 DrawData 를 GPU 로 그립니다. */
        void render( IRHIDevice* pRHIDevice ) override;
        /** @brief 메인 스왑체인 Present 뒤에 멀티 뷰포트를 그립니다. */
        void postPresent( IRHIDevice* pRHIDevice ) override;
        void abandonPendingDraw() override;
        /** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어로 전달합니다. */
        bool processEvent( const NativeWindowEvent& event ) override;
        /** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록합니다. */
        void* registerTexture( uint64 texture ) override;
        /** @brief 등록된 ImGui 텍스처 ID를 해제합니다. */
        void unregisterTexture( void* pTextureID ) override;
        /** @brief 이번 프레임 게임 뷰 RT 핸들과 크기를 조회합니다. */
        void getGameViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const override;
        /** @brief 이번 프레임 씬 뷰 RT 핸들과 크기를 조회합니다. */
        void getSceneViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const override;
        /** @brief 이번 프레임 씬 뷰를 그리는 카메라를 반환합니다. */
        CameraComponent* getSceneViewCamera() const override;
        /** @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. */
        bool isPlaying() const override;
        /** @brief 에디터 시뮬레이션이 일시정지인지 반환합니다. */
        bool isPaused() const override;
        /** @brief 에디터 시뮬레이션(PIE)을 정지합니다. */
        void stopSimulation() override;
        /** @brief 월드 틱 뒤에 Step 을 소비합니다. */
        void onHostFrameEnd() override;

    private:
        /**
         * @brief `initialize` 가 세운 단계를 **세운 역순으로** 내립니다. 단계마다 null 안전이라 어느 단계에서 멈춘 초기화의 실패 경로와
         *        정상 종료(`shutdown`)가 같은 본문을 씁니다.
         * @details 세우는 순서: 에디터 데이터 → ImGui · ImPlot 컨텍스트(+ 글꼴) → 플랫폼 백엔드 → 렌더 백엔드 → (스플래시) → 에디터 컨텍스트 · 패널 ·
         *          Undo 편집 리스너 → 창 닫기 질의 처리기. 단계를 더하면 이 함수의 같은 자리(역순)에 내리기를 더합니다.
         */
        void shutdownPartialInitialization();
        bool onWindowCloseQuery();
        // ------------------------------------------------------------------------------
        // 3) ImGui 프레임 · 백엔드 렌더
        // ------------------------------------------------------------------------------
        /** @brief ImGui 프레임을 시작합니다. */
        void beginFrame();
        /** @brief ImGui 프레임을 종료합니다. */
        void endFrame();
        /** @brief ImGui 렌더러 백엔드로 주어진 DrawData 를 그립니다. */
        void renderBackend( IRHIDevice* pRHIDevice, ImDrawData* pDrawData );
        /** @brief 렌더 스레드가 이전 스냅샷을 쓰는 동안 NewFrame을 미룹니다. */
        void waitForDrawSnapshotIdle();

    private:
        /** @brief 렌더 중인 슬롯이 없음을 뜻하는 센티널 값입니다. */
        static constexpr uint32 _s_kInvalidDrawSlot = invalid_index::kUint32;
        // draw 스냅샷 슬롯 수는 인플라이트 프레임 수와 같은 개념이라 constant::kMaxFrameCountInFlight 를 직접 쓴다. 별칭을
        // 두면 같은 개념에 이름이 둘이 되고, 한쪽만 바뀌어도 컴파일은 통과한다.
        static_assert( constant::kMaxFrameCountInFlight >= 2, "draw 스냅샷은 최소 2개(더블 버퍼) 이상이어야 합니다." );

        unique_ptr<IImGuiPlatformBackend> _platformBackend;
        unique_ptr<IImGuiRendererBackend> _rendererBackend;
        unique_ptr<EditorToolDefaults>    _editorToolDefaults;
        unique_ptr<EditorContext>         _editorContext;
        EditorDockLayout                  _dockLayout;
        EditorDrawDataSnapshot            _arrDrawSnapshot[constant::kMaxFrameCountInFlight];
        atomic<uint32>                    _publishedDrawSlot;
        atomic<uint32>                    _inFlightDrawSlot;
        /// @brief 마지막으로 낸 draw 스냅샷 번호입니다(UI 스레드 전용, 1 부터). 놓은 자원의 해제를 이 번호로 줄 세웁니다(`EditorDrawReleaseQueue`).
        uint64 _lastDrawSnapshotSequence;

        uint8                  _bInitialized  : 1;
        [[maybe_unused]] uint8 _reservedFlags : 7;
    };
} // namespace sw::editor
