#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	class SelectionManager;
	class EditorNotificationManager;
	class EditorContextMenuRegistry;
	class CommandPaletteWindow;
	class EditorWindowRegistry;
	class AssetEditorRegistry;
	class ComponentDrawerRegistry;
	class PropertyDrawerRegistry;

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

		SelectionManager&		   getSelectionManager() { return *_pSelectionManager; }
		EditorNotificationManager& getNotificationManager() { return *_pNotificationManager; }
		EditorContextMenuRegistry& getContextMenuRegistry() { return *_pContextMenuRegistry; }
		CommandPaletteWindow&	   getCommandPalette() { return *_pCommandPalette; }
		EditorWindowRegistry&	   getWindowRegistry() { return *_pWindowRegistry; }
		AssetEditorRegistry&	   getAssetEditorRegistry() { return *_pAssetEditorRegistry; }
		ComponentDrawerRegistry&   getComponentDrawerRegistry() { return *_pComponentDrawerRegistry; }
		PropertyDrawerRegistry&	   getPropertyDrawerRegistry() { return *_pPropertyDrawerRegistry; }

	private:
		unique_ptr<SelectionManager>		  _pSelectionManager;
		unique_ptr<EditorNotificationManager> _pNotificationManager;
		unique_ptr<EditorContextMenuRegistry> _pContextMenuRegistry;
		unique_ptr<CommandPaletteWindow>	  _pCommandPalette;
		unique_ptr<EditorWindowRegistry>	  _pWindowRegistry;
		unique_ptr<AssetEditorRegistry>		  _pAssetEditorRegistry;
		unique_ptr<ComponentDrawerRegistry>	  _pComponentDrawerRegistry;
		unique_ptr<PropertyDrawerRegistry>	  _pPropertyDrawerRegistry;

		static EditorContext* s_pActiveContext;
	};
} // namespace sw
