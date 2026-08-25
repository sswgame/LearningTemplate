#include "pch.h"

#include "Editor/EditorUtil.h"
#include "Editor/Tools/BaseNodeGraphEditor.h"

#include "Core/File/FileUtil.h"

#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw
{
	BaseNodeGraphEditor::BaseNodeGraphEditor( bool bDefaultOpen )
		: IEditorWindow{ bDefaultOpen }
		, _pEditor{ nullptr }
		, _settingsPath{}
		, _bNavigatedToContent{ false }
	{
	}

	BaseNodeGraphEditor::~BaseNodeGraphEditor()
	{
		destroyEditorContext();
	}

	void BaseNodeGraphEditor::shutdown( IRHIDevice* /*pRhiDevice*/ )
	{
		destroyEditorContext();
	}

	void BaseNodeGraphEditor::ensureEditorContext( const utf8* pSettingsFileName )
	{
		if ( _pEditor != nullptr )
			return;

		ed::Config config{};
		if ( pSettingsFileName != nullptr && pSettingsFileName[0] != '\0' )
		{
			const string settingsPath = EditorUtil::resolveEditorConfigFile( pSettingsFileName );
			if ( settingsPath.empty() == false )
			{
				_settingsPath		= settingsPath;
				config.SettingsFile = _settingsPath.c_str();
			}
		}

		_pEditor = ed::CreateEditor( &config );
	}

	void BaseNodeGraphEditor::destroyEditorContext()
	{
		if ( _pEditor != nullptr )
		{
			ed::DestroyEditor( _pEditor );
			_pEditor = nullptr;
		}
	}

	bool BaseNodeGraphEditor::beginGraphCanvas( const utf8* pCanvasId, const utf8* pSettingsFileName )
	{
		ensureEditorContext( pSettingsFileName );
		if ( _pEditor == nullptr )
			return false;

		ed::SetCurrentEditor( _pEditor );
		ed::Begin( pCanvasId );
		return true;
	}

	void BaseNodeGraphEditor::endGraphCanvas()
	{
		ed::End();
		ed::SetCurrentEditor( nullptr );
	}

	void BaseNodeGraphEditor::navigateToContentIfNeeded()
	{
		if ( _bNavigatedToContent == false )
		{
			ed::NavigateToContent();
			_bNavigatedToContent = true;
		}
	}

} // namespace sw
