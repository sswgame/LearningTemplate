/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 셸 (백엔드 / 도킹 / 윈도우 오케스트레이션)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/IEditor.h"

namespace sw
{
	struct EditorData;
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;
	class IEditorWindow;
	class EditorContext;

	/** @brief ImGui 도킹 셸: 백엔드와 Window/Tool 오케스트레이션 */
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
		/** @brief UI 그리기 전 패널 GPU 작업을 수행합니다. */
		void preRender( IRHIDevice* pRhiDevice ) override;
		/** @brief ImGui 프레임과 패널을 그립니다. */
		void render( const EditorUIContext& context ) override;
		/** @brief 메인 스왑체인 Present 이후 멀티 뷰포트를 렌더합니다. */
		void postPresent( IRHIDevice* pRhiDevice ) override;
		/** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어로 전달합니다. */
		bool processEvent( const NativeWindowEvent& event, const EditorUIContext* pContext ) override;
		/** @brief RHI 텍스처를 ImGui 텍스처 ID로 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;
		/** @brief 등록된 ImGui 텍스처 ID를 해제합니다. */
		void unregisterTexture( void* pTextureID ) override;

	private:
		// ------------------------------------------------------------------------------
		// 3) 셸 — 윈도우 등록 · 폰트 · ImGui 프레임
		// ------------------------------------------------------------------------------
		/** @brief 기본 에디터 윈도우를 등록합니다. */
		void registerDefaultWindows();
		/** @brief ImGui 폰트를 설정합니다. */
		void setupFonts();
		/** @brief ImGui 프레임을 시작합니다. */
		void beginFrame();
		/** @brief ImGui 프레임을 종료합니다. */
		void endFrame();
		/** @brief ImGui 렌더러 백엔드로 그립니다. */
		void renderBackend( IRHIDevice* pRhiDevice );
		/** @brief 멀티 뷰포트 플랫폼 윈도우를 렌더합니다. */
		void renderPlatformWindows( IRHIDevice* pRhiDevice );

		// ------------------------------------------------------------------------------
		// 4) 도킹 · 레이아웃 저장
		//    기본 레이아웃은 최초 1회, 이후 imgui.ini / windows.ini
		// ------------------------------------------------------------------------------
		/** @brief 메인 메뉴바를 그립니다. */
		void drawMainMenuBar( const EditorUIContext& ctx );
		/** @brief 도크스페이스를 시작합니다. */
		void beginDockspace();
		/** @brief 기본 도킹 레이아웃을 적용합니다. */
		void applyDefaultDockLayout( uint32 dockspaceId );
		/** @brief 레이아웃 저장 경로를 설정합니다. */
		void setupLayoutPersistencePaths();
		/** @brief 윈도우 표시 여부를 불러옵니다. */
		void loadWindowVisibility();
		/** @brief 에디터 레이아웃을 저장합니다. */
		void saveEditorLayout();

	private:
		unique_ptr<IImGuiPlatformBackend> _platformBackend;
		unique_ptr<IImGuiRendererBackend> _rendererBackend;
		unique_ptr<EditorData>			  _editorData;
		unique_ptr<EditorContext>		  _editorContext;

		string _imguiIniPath;
		string _windowsIniPath;

		uint8				   _bInitialized	   : 1;
		uint8				   _bDockLayoutApplied : 1;
		[[maybe_unused]] uint8 _reservedFlags	   : 6;
	};
} // namespace sw
