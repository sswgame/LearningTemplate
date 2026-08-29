/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 호스트 (OS/GPU 백엔드 · 프레임 루프)
 */
#pragma once
#include "Core/Concurrency/atomic.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Gui/EditorDockLayout.h"
#include "Editor/IEditor.h"

namespace sw::editor
{
	struct EditorData;
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;
	class EditorContext;

	/** @brief ImGui 플랫폼/렌더러 호스트. 메뉴·도크·폰트는 Common으로 위임합니다. */
	class ImGuiEditor : public IEditor
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 생성은 플래그만, GPU/ImGui는 initialize / shutdown
		// ------------------------------------------------------------------------------
		/** @brief ImGui 에디터 셸을 생성합니다. */
		ImGuiEditor();
		/** @brief ImGui 에디터 셸을 파괴합니다. */
		virtual ~ImGuiEditor() override;

		// ------------------------------------------------------------------------------
		// 2) IEditor — 초기화 / 프레임 / 이벤트 / 텍스처
		// ------------------------------------------------------------------------------
		/** @brief 플랫폼 백엔드·렌더러·폰트를 초기화합니다. */
		bool initialize( IWindow* pWindow, IRHIDevice* pRhiDevice ) override;
		/** @brief 에디터 리소스를 해제합니다. */
		void shutdown() override;
		/** @brief 메인 스레드에서 ImGui 프레임 갱신, 패널 그리기 및 플랫폼 윈도우를 업데이트합니다. */
		void updateUI() override;
		/** @brief UI 그리기 전 패널 GPU 작업을 수행합니다. */
		void preRender( IRHIDevice* pRhiDevice ) override;
		/** @brief GPU 상에 에디터 UI DrawData를 렌더링합니다. */
		void render( IRHIDevice* pRhiDevice ) override;
		/** @brief 메인 스왑체인 Present 이후 멀티 뷰포트를 렌더합니다. */
		void postPresent( IRHIDevice* pRhiDevice ) override;
		/** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어로 전달합니다. */
		bool processEvent( const NativeWindowEvent& event ) override;
		/** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록합니다. */
		void* registerTexture( uint64 texture ) override;
		/** @brief 등록된 ImGui 텍스처 ID를 해제합니다. */
		void unregisterTexture( void* pTextureID ) override;
		/** @brief 이번 프레임 Game View RT 핸들과 크기를 조회합니다. */
		void getGameViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const override;
		/** @brief 이번 프레임 Game View에 쓸 카메라를 반환합니다. */
		CameraComponent* getViewportCamera() const override;
		/** @brief 에디터 시뮬레이션(PIE)이 실행 중인지 반환합니다. */
		bool isPlaying() const override;
		/** @brief 에디터 시뮬레이션이 일시정지인지 반환합니다. */
		bool isPaused() const override;
		/** @brief 에디터 시뮬레이션(PIE)을 정지합니다. */
		void stopSimulation() override;
		/** @brief 월드 틱 이후 Step을 소비합니다. */
		void onHostFrameEnd() override;

	private:
		bool onWindowCloseQuery();
		// ------------------------------------------------------------------------------
		// 3) ImGui 프레임 · 백엔드 렌더
		// ------------------------------------------------------------------------------
		/** @brief ImGui 프레임을 시작합니다. */
		void beginFrame();
		/** @brief ImGui 프레임을 종료합니다. */
		void endFrame();
		/** @brief ImGui 렌더러 백엔드로 지정 DrawData를 그립니다. */
		void renderBackend( IRHIDevice* pRhiDevice, ImDrawData* pDrawData );
		/** @brief 스냅샷의 보조 뷰포트를 렌더합니다. */
		void renderPlatformWindows( IRHIDevice* pRhiDevice );
		/** @brief 렌더 스레드가 이전 스냅샷을 쓰는 동안 NewFrame을 미룹니다. */
		void waitForDrawSnapshotIdle();

	private:
		unique_ptr<IImGuiPlatformBackend> _platformBackend;
		unique_ptr<IImGuiRendererBackend> _rendererBackend;
		unique_ptr<EditorData>			  _editorData;
		unique_ptr<EditorContext>		  _editorContext;
		EditorDockLayout				  _dockLayout;
		EditorDrawDataSnapshot			  _arrDrawSnapshot[2];
		atomic<uint32>					  _publishedDrawSlot;
		atomic<uint32>					  _inFlightDrawSlot;

		uint8				   _bInitialized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};
} // namespace sw::editor
