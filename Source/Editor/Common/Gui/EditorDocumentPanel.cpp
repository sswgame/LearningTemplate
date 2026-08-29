#include "pch.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

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
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return {};

		const string& focused = pContext->getWorkspace().getFocusedAssetPath();
		if ( focused.empty() )
			return {};
		if ( EditorAssetTypeRegistry::matches( _kind, focused ) == false )
			return {};
		return focused;
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
