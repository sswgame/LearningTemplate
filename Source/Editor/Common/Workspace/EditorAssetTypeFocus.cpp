#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
	string_view EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetKind kind )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return {};

		const string& focused = pContext->getWorkspace().getFocusedAssetPath();
		if ( matches( kind, focused ) == false )
			return {};
		return focused;
	}

	bool EditorAssetTypeRegistry::consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken )
	{
		EditorContext* pContext = EditorContext::get();
		string		   scanKey;
		if ( pContext != nullptr )
		{
			scanKey = pContext->getWorkspace().getFocusedAssetPath();
			scanKey += '|';
			scanKey += to_string( extraToken );
		}
		if ( scanKey == ioLastKey )
			return false;
		ioLastKey = std::move( scanKey );
		return true;
	}
} // namespace sw::editor
