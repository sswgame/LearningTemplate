#include "pch.h"

#include "Editor/Panels/MaterialPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct MaterialPanelInternal
		{
			static uint32 parseFloats( string_view text, float32* pOut, uint32 count )
			{
				if ( pOut == nullptr || count == 0 )
					return 0;
				for ( uint32 index = 0; index < count; ++index )
					pOut[index] = 0.0f;
				if ( text.empty() )
					return 0;
				string_splitter splitter( text, { ",", " ", "\t" } );
				const uint32	tokenCount = splitter.getCount();
				uint32			filled{ 0 };
				for ( ; filled < count && filled < tokenCount; ++filled )
				{
					StringUtil::parseFloat( splitter.getSplitList()[filled], pOut[filled] );
				}
				return filled;
			}

			static void writeFloats( string& out, const float32* pVal, uint32 count )
			{
				fixed_string<constant::kMaxBuffer128> buf;
				if ( count == 1 )
					formatstring( buf.data(), buf.capacity(), "%g", pVal[0] );
				else if ( count == 2 )
					formatstring( buf.data(), buf.capacity(), "%g,%g", pVal[0], pVal[1] );
				else if ( count == 3 )
					formatstring( buf.data(), buf.capacity(), "%g,%g,%g", pVal[0], pVal[1], pVal[2] );
				else if ( count >= 4 )
					formatstring( buf.data(), buf.capacity(), "%g,%g,%g,%g", pVal[0], pVal[1], pVal[2], pVal[3] );
				out = buf.c_str();
			}

			static bool drawTypedProperty( MaterialProperty& prop )
			{
				const utf8* pLabel = prop._displayName.empty() ? prop._name.c_str() : prop._displayName.c_str();
				if ( prop._value.empty() )
					prop._value = prop._defaultValue;

				switch ( prop._type )
				{
					case MaterialPropertyType::Bool:
					{
						bool bVal = StringUtil::parseBool( prop._value, false );
						if ( ImGui::Checkbox( pLabel, &bVal ) )
						{
							prop._value = bVal ? "true" : "false";
							return true;
						}
						return false;
					}
					case MaterialPropertyType::Float:
					case MaterialPropertyType::Range:
					{
						float32 fVal{ 0.0f };
						parseFloats( prop._value, &fVal, 1 );
						bool bChanged{ false };
						if ( prop._type == MaterialPropertyType::Range && prop._min < prop._max )
							bChanged = ImGui::SliderFloat( pLabel, &fVal, prop._min, prop._max );
						else
							bChanged = ImGui::DragFloat( pLabel, &fVal, 0.01f );
						if ( bChanged )
							writeFloats( prop._value, &fVal, 1 );
						return bChanged;
					}
					case MaterialPropertyType::Float2:
					{
						float32 arrVal[2]{ 0.0f, 0.0f };
						parseFloats( prop._value, arrVal, 2 );
						const bool bChanged = ImGui::DragFloat2( pLabel, arrVal, 0.01f );
						if ( bChanged )
							writeFloats( prop._value, arrVal, 2 );
						return bChanged;
					}
					case MaterialPropertyType::Float3:
					{
						float32 arrVal[3]{ 0.0f, 0.0f, 0.0f };
						parseFloats( prop._value, arrVal, 3 );
						const bool bChanged = ImGui::DragFloat3( pLabel, arrVal, 0.01f );
						if ( bChanged )
							writeFloats( prop._value, arrVal, 3 );
						return bChanged;
					}
					case MaterialPropertyType::Color:
					case MaterialPropertyType::Float4:
					{
						float32 arrVal[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
						parseFloats( prop._value, arrVal, 4 );
						Color4	   color{ arrVal[0], arrVal[1], arrVal[2], arrVal[3] };
						const bool bChanged = EditorWidgets::drawColorEdit( pLabel, color );
						if ( bChanged )
						{
							arrVal[0] = color._r;
							arrVal[1] = color._g;
							arrVal[2] = color._b;
							arrVal[3] = color._a;
							writeFloats( prop._value, arrVal, 4 );
						}
						return bChanged;
					}
					case MaterialPropertyType::Int:
					{
						int32 iVal{ 0 };
						StringUtil::parseInt( prop._value, iVal );
						const bool bChanged = ImGui::DragInt( pLabel, &iVal );
						if ( bChanged )
							prop._value = to_string( iVal );
						return bChanged;
					}
					case MaterialPropertyType::Enum:
					{
						int32 selected{ 0 };
						StringUtil::parseInt( prop._value, selected );
						const utf8* pPreview = prop._value.c_str();
						for ( const MaterialEnumEntry& entry : prop._listEnumEntry )
						{
							if ( static_cast<int32>( entry._value ) == selected )
								pPreview = entry._name.c_str();
						}
						bool bChanged{ false };
						if ( ImGui::BeginCombo( pLabel, pPreview ) )
						{
							for ( const MaterialEnumEntry& entry : prop._listEnumEntry )
							{
								const bool bSelected = ( static_cast<int32>( entry._value ) == selected );
								if ( ImGui::Selectable( entry._name.c_str(), bSelected ) )
								{
									prop._value = to_string( entry._value );
									bChanged	= true;
								}
							}
							ImGui::EndCombo();
						}
						return bChanged;
					}
					case MaterialPropertyType::Texture2D:
					case MaterialPropertyType::TextureCube:
					case MaterialPropertyType::Texture3D:
					case MaterialPropertyType::Texture2DArray:
					{
						string	   path		= prop._assetPath.empty() ? prop._value : prop._assetPath;
						const bool bChanged = EditorWidgets::drawAssetSlot( pLabel, path, ".png" );
						if ( bChanged )
						{
							prop._assetPath = path;
							prop._value		= path;
						}
						return bChanged;
					}
					case MaterialPropertyType::Float4x4:
					case MaterialPropertyType::Uint:
					case MaterialPropertyType::Uint2:
					case MaterialPropertyType::Uint3:
					case MaterialPropertyType::Uint4:
					case MaterialPropertyType::Int2:
					case MaterialPropertyType::Int3:
					case MaterialPropertyType::Int4:
					case MaterialPropertyType::BitFlag:
					case MaterialPropertyType::ChannelMask:
					case MaterialPropertyType::Keyword:
					case MaterialPropertyType::Unknown:
					default:
					{
						fixed_string<constant::kMaxBuffer256> arrValue{ prop._value.c_str() };
						if ( ImGui::InputText( pLabel, arrValue.data(), arrValue.capacity() ) )
						{
							prop._value = arrValue.c_str();
							return true;
						}
						return false;
					}
				}
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "MaterialPanel" );

	MaterialPanel::MaterialPanel()
		: EditorDocumentPanel{ EditorAssetKind::Material, false }
		, _material{}
		, _name{}
		, _shaderPath{}
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
		ImGui::SameLine();
		if ( ImGui::Button( "Apply to Selection" ) )
			applyLivePreview();

		ImGui::InputText( "Name", _name.data(), _name.capacity() );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
		{
			_material.getDesc()._name = _name.c_str();
			notifyDocumentEdited( "Edit Material Name", "material-name" );
			applyLivePreview();
		}
		string shaderPath = _shaderPath.c_str();
		if ( EditorWidgets::drawAssetSlot( "Shader", shaderPath, ".hlsl" ) )
		{
			_shaderPath						= shaderPath;
			_material.getDesc()._shaderPath = _shaderPath.c_str();
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
			const bool bChanged = MaterialPanelInternal::drawTypedProperty( *pProp );
			if ( bChanged )
			{
				fixed_string<constant::kMaxBuffer128> key;
				formatstring( key.data(), key.capacity(), "material-prop:%s", pProp->_name.c_str() );
				notifyDocumentEdited( "Edit Material Property", key.c_str() );
				applyLivePreview();
			}
			ImGui::PopID();
		}

		EditorWidgets::drawPanelStatus( _status.c_str() );
	}

	void MaterialPanel::applyLivePreview()
	{
		EditorViewportPreview::applyMaterial( &_material, getLoadedAssetPath() );
	}

	bool MaterialPanel::saveDocument()
	{
		if ( getLoadedAssetPath().empty() )
			return false;
		_material.getDesc()._name		= _name.c_str();
		_material.getDesc()._shaderPath = _shaderPath.c_str();
		if ( _material.saveToFile( getLoadedAssetPath() ) == false )
		{
			_status = "Save failed";
			return false;
		}
		ResourceManager* pResources = editor::getService<ResourceManager>();
		if ( pResources != nullptr )
			pResources->getMaterialManager().reload( getLoadedAssetPath() );
		applyLivePreview();
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
		_name		= _material.getName().c_str();
		_shaderPath = _material.getShaderPath().c_str();
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
		applyLivePreview();
	}
} // namespace sw::editor
