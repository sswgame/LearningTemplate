/**
 * @file AssetEditorManager.h
 * @brief 애셋 확장자별 에디터 윈도우 매핑 및 열기 관리 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

namespace sw::editor
{
	/**
	 * @class AssetEditorManager
	 * @brief 애셋 확장자별 에디터 윈도우 매핑 및 열기 디스패처 (EditorContext 소유)
	 */
	class AssetEditorManager
	{
	public:
		AssetEditorManager()  = default;
		~AssetEditorManager() = default;

		void		registerAssetEditor( string_view extension, string_view windowTitle );
		string_view findEditorForExtension( string_view extension ) const;
		bool		openAssetInEditor( string_view assetPath );
		void		registerDefaultMappings();

	private:
		map<string, string> _mapExtToWindowTitle;
	};
} // namespace sw::editor
