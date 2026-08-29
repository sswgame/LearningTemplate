#include "pch.h"

#include "Editor/Panels/MaterialPanel.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

namespace sw::editor
{
	SW_LOG_CALLER( "MaterialPanel" );

	MaterialPanel::MaterialPanel()
		: EditorDocumentPanel{ EditorAssetKind::Material, false }
		, _material{}
		, _arrName{}
		, _arrShaderPath{}
		, _status{}
	{
	}

	void MaterialPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
			loadFromFocusedPath();

		if ( getLoadedAssetPath().empty() )
		{
			ImGui::TextDisabled( "Focus a .mat / .material asset to edit." );
			return;
		}

		ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
		if ( ImGui::Button( "Reload" ) )
			loadFromFocusedPath();
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
			saveDocument();

		ImGui::InputText( "Name", _arrName, sizeof( _arrName ) );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
		{
			_material.getDesc()._name = _arrName;
			notifyDocumentEdited( "Edit Material Name", "material-name" );
		}
		ImGui::InputText( "Shader", _arrShaderPath, sizeof( _arrShaderPath ) );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
		{
			_material.getDesc()._shaderPath = _arrShaderPath;
			notifyDocumentEdited( "Edit Material Shader", "material-shader" );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Properties" );
		const vector<MaterialProperty>& listProp = _material.getProperties();
		for ( uint32 propIndex = 0; propIndex < static_cast<uint32>( listProp.size() ); ++propIndex )
		{
			const MaterialProperty& src = listProp[propIndex];
			if ( src._bHidden == SW_TRUE )
				continue;
			MaterialProperty* pProp = _material.findProperty( hashed_string( src._name.c_str() ) );
			if ( pProp == nullptr )
				continue;
			ImGui::PushID( static_cast<int32>( propIndex ) );
			utf8 arrValue[256];
			StringUtil::strncpy( arrValue, pProp->_value.empty() ? pProp->_defaultValue.c_str() : pProp->_value.c_str(),
								 sizeof( arrValue ) - 1 );
			arrValue[sizeof( arrValue ) - 1] = '\0';
			const utf8* pLabel				 = pProp->_displayName.empty() ? pProp->_name.c_str() : pProp->_displayName.c_str();
			if ( ImGui::InputText( pLabel, arrValue, sizeof( arrValue ) ) )
				pProp->_value = arrValue;
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Material Property", "material-prop" );
			ImGui::PopID();
		}

		EditorWidgets::drawPanelStatus( _status.c_str() );
	}

	bool MaterialPanel::saveDocument()
	{
		if ( getLoadedAssetPath().empty() )
			return false;
		_material.getDesc()._name		= _arrName;
		_material.getDesc()._shaderPath = _arrShaderPath;
		if ( _material.saveToFile( getLoadedAssetPath() ) == false )
		{
			_status = "Save failed";
			return false;
		}
		_status = "Saved";
		clearDocumentDirty();
		syncDocumentUndoBaseline();
		return true;
	}

	void MaterialPanel::loadFromFocusedPath()
	{
		string path = getLoadedAssetPath();
		if ( path.empty() )
			path = string{ getMatchingFocusedPath() };
		if ( path.empty() )
			return;
		if ( getLoadedAssetPath().empty() )
			acceptFocusedDocument();
		if ( _material.loadFromFile( path ) == false )
		{
			_status = "Load failed";
			markDocumentLoaded();
			return;
		}
		syncNameBuffers();
		_status = "Loaded";
		markDocumentLoaded();
	}

	void MaterialPanel::syncNameBuffers()
	{
		StringUtil::strncpy( _arrName, _material.getName().c_str(), sizeof( _arrName ) - 1 );
		_arrName[sizeof( _arrName ) - 1] = '\0';
		StringUtil::strncpy( _arrShaderPath, _material.getShaderPath().c_str(), sizeof( _arrShaderPath ) - 1 );
		_arrShaderPath[sizeof( _arrShaderPath ) - 1] = '\0';
	}

	string MaterialPanel::captureDocumentText() const
	{
		return _material.saveToString();
	}

	void MaterialPanel::applyDocumentText( string_view text )
	{
		if ( text.empty() )
			return;
		_material.loadFromXml( text );
		syncNameBuffers();
	}
} // namespace sw::editor
