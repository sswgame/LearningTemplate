#include "pch.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

namespace sw::editor
{
	EditorDocumentPanel::EditorDocumentPanel( EditorAssetKind kind, bool bLoadOnOpen )
		: IEditorPanel{ false }
		, _kind{ kind }
		, _loadedAssetPath{}
		, _bLoaded{ SW_FALSE }
		, _reserved{ 0 }
	{
		if ( bLoadOnOpen == false )
			_bLoaded = SW_TRUE;
	}

	bool EditorDocumentPanel::hasNewFocusedDocument() const
	{
		const string_view focused = getMatchingFocusedPath();
		if ( focused.empty() )
			return false;
		return focused != _loadedAssetPath;
	}

	string_view EditorDocumentPanel::getMatchingFocusedPath() const
	{
		return EditorAssetTypeRegistry::matchingFocusedPath( _kind );
	}

	const utf8* EditorDocumentPanel::getPanelTitle() const
	{
		return EditorAssetTypeRegistry::getPanelTitle( _kind );
	}

	void EditorDocumentPanel::acceptFocusedDocument()
	{
		const string_view focused = getMatchingFocusedPath();
		if ( focused.empty() )
			return;
		_loadedAssetPath = string{ focused };
		_bLoaded		 = SW_FALSE;
	}

	void EditorDocumentPanel::markDocumentLoaded()
	{
		_bLoaded = SW_TRUE;
	}

	bool EditorDocumentPanel::isDocumentLoaded() const
	{
		return _bLoaded == SW_TRUE;
	}
} // namespace sw::editor
