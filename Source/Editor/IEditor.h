/**
 * @file IEditor.h
 * @brief App↔Editor Runtime API에 대응하는 에디터 코어 인터페이스
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	class IWindow;
	class CameraComponent;
	struct NativeWindowEvent;

	/**
	 * @class IEditor
	 * @brief EditorAPI 함수 테이블이 위임하는 최소 표면 (구현은 sw::editor::ImGuiEditor)
	 */
	class IEditor
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 생성은 App, 해제는 파생 구현
		// ------------------------------------------------------------------------------
		/** @brief 파생 에디터가 리소스를 해제할 수 있게 합니다. */
		virtual ~IEditor() = default;

		/** @brief 플랫폼 백엔드·렌더러·폰트를 초기화합니다. */
		virtual bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) = 0;
		/** @brief 에디터 리소스를 해제합니다. */
		virtual void shutdown() = 0;

		// ------------------------------------------------------------------------------
		// 2) 프레임 — updateUI (Main Thread) → preRender / render / postPresent (RenderThread)
		// ------------------------------------------------------------------------------
		/** @brief 메인 스레드에서 ImGui 프레임 갱신, 패널 그리기 및 플랫폼 윈도우를 업데이트합니다. */
		virtual void updateUI() = 0;
		/** @brief UI 그리기 전 패널 GPU 작업을 수행합니다. */
		virtual void preRender( IRHIDevice* pRhiDevice ) = 0;
		/** @brief GPU 상에 에디터 UI DrawData를 렌더링합니다. */
		virtual void render( IRHIDevice* pRhiDevice ) = 0;
		/** @brief 메인 스왑체인 Present 이후 호출 (멀티 뷰포트 보조 윈도우 렌더) */
		virtual void postPresent( IRHIDevice* pRhiDevice ) = 0;

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
		/** @brief 이번 프레임 Game View에 쓸 카메라를 반환합니다. 편집 모드면 에디터 카메라, PIE면 게임 카메라. */
		virtual CameraComponent* getViewportCamera() const = 0;
		/** @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. Step 대기 중이면 true입니다. */
		virtual bool isPlaying() const = 0;
		/** @brief 에디터 시뮬레이션이 일시정지인지 반환합니다. */
		virtual bool isPaused() const = 0;
		/** @brief 에디터 시뮬레이션(PIE)을 정지합니다. */
		virtual void stopSimulation() = 0;
		/** @brief 월드 틱 이후 한 프레임 Step을 소비합니다. */
		virtual void onHostFrameEnd() = 0;
	};
} // namespace sw
