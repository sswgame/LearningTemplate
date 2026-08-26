#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class IRHIDevice;
} // namespace sw

namespace sw::editor
{
	class SelectionManager;
	class EditorNotificationManager;
	class EditorContextMenuRegistry;
	class CommandPalettePopup;
	class EditorPanelRegistry;
	class AssetEditorRegistry;
	class InspectorComponentRegistry;
	class InspectorPropertyRegistry;
	class IImGuiRendererBackend;

	/** @brief 에디터가 소유하는 Game View RT. App은 매 프레임 핸들만 조회합니다. */
	struct EditorGameView
	{
		uint64 _renderTarget{ 0 };
		void*  _pTextureId{ nullptr };
		uint32 _width{ 0 };
		uint32 _height{ 0 };
	};

	/**
	 * @class EditorContext
	 * @brief 에디터 셸(ImGuiEditor)이 생명주기를 직접 생성/소멸 관리하는 에디터 중앙 컨텍스트
	 */
	class EditorContext
	{
	public:
		EditorContext();
		~EditorContext();

		/** @brief 에디터 서브시스템들을 생성 및 초기화합니다. */
		void initialize();
		/** @brief 에디터 서브시스템들을 정리 및 해제합니다. */
		void shutdown();

		/** @brief 현재 활성화된 전역 에디터 컨텍스트 포인터를 반환합니다. */
		static EditorContext* get() { return s_pActiveContext; }
		/** @brief 활성 에디터 컨텍스트 포인터를 설정합니다. */
		static void setActive( EditorContext* pContext ) { s_pActiveContext = pContext; }

		SelectionManager&			getSelectionManager() { return *_pSelectionManager; }
		EditorNotificationManager&	getNotificationManager() { return *_pNotificationManager; }
		EditorContextMenuRegistry&	getContextMenuRegistry() { return *_pContextMenuRegistry; }
		CommandPalettePopup&		getCommandPalette() { return *_pCommandPalette; }
		EditorPanelRegistry&		getPanelRegistry() { return *_pPanelRegistry; }
		AssetEditorRegistry&		getAssetEditorRegistry() { return *_pAssetEditorRegistry; }
		InspectorComponentRegistry& getInspectorComponentRegistry() { return *_pInspectorComponentRegistry; }
		InspectorPropertyRegistry&	getInspectorPropertyRegistry() { return *_pInspectorPropertyRegistry; }

		void		setRhiDevice( IRHIDevice* pDevice ) { _pRhiDevice = pDevice; }
		IRHIDevice* getRhiDevice() const { return _pRhiDevice; }
		void		setRendererBackend( IImGuiRendererBackend* pBackend ) { _pRendererBackend = pBackend; }
		void		setGameViewHovered( bool bHovered ) { _bGameViewHovered = bHovered ? 1 : 0; }
		void		setGameViewFocused( bool bFocused ) { _bGameViewFocused = bFocused ? 1 : 0; }
		bool		isGameViewHovered() const { return _bGameViewHovered != 0; }
		bool		isGameViewFocused() const { return _bGameViewFocused != 0; }

		const EditorGameView& getGameView() const { return _gameView; }
		void				  ensureGameViewSize( uint32 width, uint32 height );
		void				  destroyGameView();

	private:
		unique_ptr<SelectionManager>		   _pSelectionManager;
		unique_ptr<EditorNotificationManager>  _pNotificationManager;
		unique_ptr<EditorContextMenuRegistry>  _pContextMenuRegistry;
		unique_ptr<CommandPalettePopup>		   _pCommandPalette;
		unique_ptr<EditorPanelRegistry>		   _pPanelRegistry;
		unique_ptr<AssetEditorRegistry>		   _pAssetEditorRegistry;
		unique_ptr<InspectorComponentRegistry> _pInspectorComponentRegistry;
		unique_ptr<InspectorPropertyRegistry>  _pInspectorPropertyRegistry;
		IRHIDevice*							   _pRhiDevice;
		IImGuiRendererBackend*				   _pRendererBackend;
		EditorGameView						   _gameView;

		uint8				   _bGameViewHovered : 1;
		uint8				   _bGameViewFocused : 1;
		[[maybe_unused]] uint8 _reserved		 : 6;

		static EditorContext* s_pActiveContext;
	};
} // namespace sw::editor
