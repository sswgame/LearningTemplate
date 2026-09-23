/**
 * @file IEditor.h
 * @brief App ↔ Editor 런타임 API(EditorAPI)에 대응하는 에디터 핵심 인터페이스입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct NativeWindowEvent;

    class CameraComponent;
    class IRHIDevice;
    class IWindow;

    /**
     * @class IEditor
     * @brief EditorAPI 함수 테이블이 호출을 넘기는 최소 인터페이스입니다(구현은 sw::editor::ImGuiEditor).
     */
    class IEditor
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 주기 (생성은 App, 해제는 파생 구현)
        // ------------------------------------------------------------------------------
        /** @brief 파생 에디터가 리소스를 해제할 수 있게 합니다. */
        virtual ~IEditor() = default;

        /** @brief 플랫폼 백엔드·렌더러·폰트를 초기화합니다. */
        virtual bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) = 0;
        /** @brief 에디터 리소스를 해제합니다. */
        virtual void shutdown() = 0;

        // ------------------------------------------------------------------------------
        // 2) 프레임: updateUi(메인 스레드) → preRender / render / postPresent(렌더 스레드)
        // ------------------------------------------------------------------------------
        /** @brief 메인 스레드에서 ImGui 프레임을 갱신하고, 패널을 그리고, 플랫폼 창을 갱신합니다. */
        virtual void updateUi() = 0;
        /** @brief UI 를 그리기 전에 패널의 GPU 작업을 합니다. */
        virtual void preRender( IRHIDevice* pRhiDevice ) = 0;
        /** @brief 에디터 UI 의 DrawData 를 GPU 로 그립니다. */
        virtual void render( IRHIDevice* pRhiDevice ) = 0;
        /** @brief 메인 스왑체인 Present 뒤에 불립니다(멀티 뷰포트의 보조 창을 그립니다). */
        virtual void postPresent( IRHIDevice* pRhiDevice ) = 0;
        /**
         * @brief 렌더 대기 중인 draw 스냅샷 표시를 버립니다. **스냅샷을 읽을 쪽이 더 이상 없을 때** 부릅니다.
         * @details `updateUi` 는 스냅샷을 내보내면서 "렌더 대기" 로 표시하고, 그 표시를 푸는 것은 렌더 스레드의 `postPresent`
         *          뿐입니다. 모듈 리로드 · RHI 교체처럼 렌더 워커를 먼저 비우는 경로에서는 그 `postPresent` 가 영영 오지 않아, 다음
         *          `updateUi` 나 `shutdown` 이 끝없이 기다립니다. 그래서 워커를 비운 쪽이 이 상태 변화를 알려 줍니다.
         */
        virtual void abandonPendingDraw() = 0;

        // ------------------------------------------------------------------------------
        // 3) 입력 · ImGui 텍스처
        // ------------------------------------------------------------------------------
        /** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어로 전달합니다. */
        virtual bool processEvent( const NativeWindowEvent& event ) = 0;
        /** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록합니다. */
        virtual void* registerTexture( uint64 texture ) = 0;
        /** @brief 등록된 ImGui 텍스처 ID를 해제합니다. */
        virtual void unregisterTexture( void* pTextureID ) = 0;
        /** @brief 이번 프레임 Game View RT 핸들과 크기를 조회합니다. */
        virtual void getGameViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const = 0;
        /** @brief 이번 프레임 Game View 에 쓸 카메라를 반환합니다. 편집 모드면 에디터 카메라, PIE 면 게임 카메라입니다. */
        virtual CameraComponent* getViewportCamera() const = 0;
        /** @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. Step 대기 중이면 true입니다. */
        virtual bool isPlaying() const = 0;
        /** @brief 에디터 시뮬레이션이 일시정지인지 반환합니다. */
        virtual bool isPaused() const = 0;
        /** @brief 에디터 시뮬레이션(PIE)을 정지합니다. */
        virtual void stopSimulation() = 0;
        /** @brief 월드 틱 뒤에 한 프레임짜리 Step 을 소비합니다. */
        virtual void onHostFrameEnd() = 0;
    };
} // namespace sw
