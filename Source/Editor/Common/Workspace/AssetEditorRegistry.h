#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

namespace sw::editor
{
	/**
	 * @class AssetEditorRegistry
	 * @brief 애셋 확장자별 에디터 윈도우 매핑 및 열기 디스패처 (정적 클래스)
	 */
	class AssetEditorRegistry
	{
	public:
		AssetEditorRegistry()  = default;
		~AssetEditorRegistry() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void		   registerAssetEditor( string_view extension, string_view windowTitle );
		static string_view findEditorForExtension( string_view extension );
		static bool		   openAssetInEditor( string_view assetPath );
		static void		   registerDefaultMappings();

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void		registerAssetEditorImpl( string_view extension, string_view windowTitle );
		string_view findEditorForExtensionImpl( string_view extension ) const;
		bool		openAssetInEditorImpl( string_view assetPath );
		void		registerDefaultMappingsImpl();

	private:
		map<string, string> _mapExtToWindowTitle;
	};
} // namespace sw::editor
