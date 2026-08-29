#include "pch.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>

namespace sw::editor
{
	EditorDocumentPanel::EditorDocumentPanel( EditorAssetKind kind, bool bLoadOnOpen )
		: IEditorPanel{ false }
		, _kind{ kind }
		, _loadedAssetPath{}
		, _pendingFocusPath{}
		, _bLoaded{ SW_FALSE }
		, _bDocumentDirty{ SW_FALSE }
		, _bConfirmSwitch{ SW_FALSE }
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
		const string_view focused = _pendingFocusPath.empty() ? getMatchingFocusedPath() : string_view{ _pendingFocusPath };
		if ( focused.empty() )
			return;
		_loadedAssetPath = string{ focused };
		_pendingFocusPath.clear();
		_bLoaded		= SW_FALSE;
		_bDocumentDirty = SW_FALSE;
		_bConfirmSwitch = SW_FALSE;
	}

	void EditorDocumentPanel::updateFocusedDocument()
	{
		if ( _bConfirmSwitch == SW_TRUE )
		{
			drawUnsavedDocumentPopup();
			return;
		}

		if ( hasNewFocusedDocument() == false )
			return;

		if ( isDocumentDirty() )
		{
			_pendingFocusPath = string{ getMatchingFocusedPath() };
			_bConfirmSwitch	  = SW_TRUE;
			drawUnsavedDocumentPopup();
			return;
		}

		acceptFocusedDocument();
	}

	void EditorDocumentPanel::markDocumentLoaded()
	{
		_bLoaded		= SW_TRUE;
		_bDocumentDirty = SW_FALSE;
	}

	bool EditorDocumentPanel::isDocumentLoaded() const
	{
		return _bLoaded == SW_TRUE;
	}

	void EditorDocumentPanel::markDocumentDirty()
	{
		_bDocumentDirty = SW_TRUE;
	}

	void EditorDocumentPanel::clearDocumentDirty()
	{
		_bDocumentDirty = SW_FALSE;
	}

	bool EditorDocumentPanel::trySaveDirtyDocument()
	{
		if ( isDocumentDirty() == false )
			return false;
		return saveDocument();
	}

	EditorPanelFlags EditorDocumentPanel::getPanelFlags() const
	{
		if ( isDocumentDirty() )
			return EditorPanelFlags::UnsavedDocument;
		return EditorPanelFlags::None;
	}

	void EditorDocumentPanel::drawUnsavedDocumentPopup()
	{
		if ( ImGui::IsPopupOpen( "##UnsavedDocumentSwitch" ) == false )
			ImGui::OpenPopup( "##UnsavedDocumentSwitch" );
		const EditorUnsavedChoice choice =
			EditorWidgets::drawUnsavedChangesModal( "##UnsavedDocumentSwitch",
													"This document has unsaved changes. Switch anyway?" );
		if ( choice == EditorUnsavedChoice::None )
			return;

		if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
		{
			if ( saveDocument() == false )
			{
				_bConfirmSwitch = SW_FALSE;
				_pendingFocusPath.clear();
				return;
			}
		}
		if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
			clearDocumentDirty();

		if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
		{
			acceptFocusedDocument();
			return;
		}

		_bConfirmSwitch = SW_FALSE;
		_pendingFocusPath.clear();
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr && _loadedAssetPath.empty() == false )
			pContext->getWorkspace().setFocusedAssetPath( _loadedAssetPath.c_str() );
	}
} // namespace sw::editor
