#pragma once
/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 셸 (백엔드 / 도킹 / 패널 오케스트레이션)
 */
#include "IEditor.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class IImGuiPlatformBackend;
	class IImGuiRendererBackend;
	class IEditorPanel;

	/** @brief ImGui 도킹 셸: 플랫폼/렌더러 백엔드와 기본 패널을 오케스트레이션 */
	class ImGuiEditor : public IEditor
	{
	public:
		ImGuiEditor();
		~ImGuiEditor() override;

		/** @brief ImGui 컨텍스트·백엔드·폰트·기본 패널을 초기화합니다. */
		bool initialize( IWindow* window, IRHIDevice* rhiDevice ) override;
		/** @brief 패널·백엔드·ImGui 컨텍스트를 종료합니다. */
		void shutdown() override;
		/** @brief 각 패널의 preRender를 호출합니다. */
		void preRender( IRHIDevice* rhiDevice ) override;
		/** @brief 도킹 스페이스와 패널을 그립니다. */
		void render( const EditorUIContext& context ) override;
		/** @brief Present 이후 멀티 뷰포트 보조 윈도우를 렌더합니다. */
		void postPresent( IRHIDevice* rhiDevice ) override;
		/** @brief 네이티브 이벤트를 플랫폼 백엔드로 전달합니다. */
		bool processEvent( const NativeWindowEvent& event ) override;
		/** @brief 렌더러 백엔드에 텍스처를 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;

	private:
		/** @brief Console/GameView 등 기본 패널을 등록합니다. */
		void registerDefaultPanels();
		/** @brief 에디터/시스템 폰트를 로드합니다. */
		void setupFonts();
		/** @brief ImGui NewFrame을 시작합니다. */
		void beginFrame();
		/** @brief ImGui 프레임을 끝냅니다. */
		void endFrame();
		/** @brief 메인 뷰포트 ImGui draw data를 RHI로 그립니다. */
		void renderBackend( IRHIDevice* rhiDevice );
		/** @brief 플랫폼 보조 윈도우(멀티 뷰포트)를 렌더합니다. */
		void renderPlatformWindows( IRHIDevice* rhiDevice );
		/** @brief 전체 화면 도킹 스페이스를 시작합니다. */
		void beginDockspace();
		/** @brief 최초 1회 기본 도킹 레이아웃을 적용합니다. */
		void applyDefaultDockLayout( uint32 dockspaceId );

		std::unique_ptr<IImGuiPlatformBackend>	   _platformBackend;
		std::unique_ptr<IImGuiRendererBackend>	   _rendererBackend;
		std::vector<std::unique_ptr<IEditorPanel>> _panels;

		uint8				   _bInitialized	   : 1;
		uint8				   _bDockLayoutApplied : 1;
		[[maybe_unused]] uint8 _reservedFlags	   : 6;
	};
} // namespace sw
