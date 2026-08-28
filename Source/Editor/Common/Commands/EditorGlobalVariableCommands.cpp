#include "pch.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <cstdlib>

namespace sw::editor
{
	namespace
	{
		string getTypeString( const GlobalVariableInfo& info )
		{
			switch ( info._type )
			{
				case GlobalVariableType::Boolean:
					return "Bool";
				case GlobalVariableType::Int32:
					return "Int32";
				case GlobalVariableType::Float:
					return "Float";
				case GlobalVariableType::String:
					return "String";
				case GlobalVariableType::Enum:
					return info._enumType.empty() == false ? info._enumType : "Enum";
				default:
					return "Unknown";
			}
		}
	} // namespace

	string EditorGlobalVariableCommands::getPresetFolderPath()
	{
		return FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource/game/demo/data/presets/globalvars" );
	}

	string EditorGlobalVariableCommands::getComponentPresetFolderPath()
	{
		return FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource/game/demo/data/presets" );
	}

	bool EditorGlobalVariableCommands::savePreset( const string& filePath, const string& presetName )
	{
		GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
		if ( pGvm == nullptr )
			return false;

		const vector<string> listAllNames = pGvm->collectVariableNames();
		string				 xml		  = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
		xml += "<GlobalVariablesPreset name=\"" + presetName + "\">\n";

		for ( const string& varName : listAllNames )
		{
			const GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
			if ( pInfo == nullptr || pInfo->_pData == nullptr )
				continue;

			string valStr;
			switch ( pInfo->_type )
			{
				case GlobalVariableType::Boolean:
					valStr = ( *static_cast<const bool*>( pInfo->_pData ) ) ? "1" : "0";
					break;
				case GlobalVariableType::Int32:
				case GlobalVariableType::Enum:
					valStr = to_string( *static_cast<const int32*>( pInfo->_pData ) );
					break;
				case GlobalVariableType::Float:
					valStr = to_string( *static_cast<const float32*>( pInfo->_pData ) );
					break;
				case GlobalVariableType::String:
					valStr = *static_cast<const string*>( pInfo->_pData );
					break;
			}

			xml += "    <Var name=\"" + pInfo->_name + "\" type=\"" + getTypeString( *pInfo ) +
				   "\" value=\"" + valStr + "\" />\n";
		}

		xml += "</GlobalVariablesPreset>\n";

		const string dir = FileUtil::getDirectoryPart( filePath );
		FileUtil::ensureDirectoryExists( dir );
		return FileUtil::writeFile( filePath, reinterpret_cast<const uint8*>( xml.c_str() ), xml.size() );
	}

	bool EditorGlobalVariableCommands::loadPreset( const string& filePath )
	{
		GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
		if ( pGvm == nullptr )
			return false;

		XmlDocument doc;
		if ( doc.loadFile( filePath ) == false )
			return false;

		XmlNode rootNode = doc.root();
		if ( rootNode.isValid() == false )
			return false;

		for ( XmlNode varNode = rootNode.child( "Var" ); varNode.isValid(); varNode = varNode.next( "Var" ) )
		{
			const utf8* pName = varNode.attr( "name" );
			const utf8* pVal  = varNode.attr( "value" );
			if ( pName == nullptr || pVal == nullptr )
				continue;

			GlobalVariableInfo* pInfo = pGvm->findVariable( pName );
			if ( pInfo == nullptr || pInfo->_pData == nullptr )
				continue;

			switch ( pInfo->_type )
			{
				case GlobalVariableType::Boolean:
				{
					bool* pValPtr = static_cast<bool*>( pInfo->_pData );
					*pValPtr	  = ( StringUtil::stristr( pVal, "1" ) != nullptr ||
									  StringUtil::stristr( pVal, "true" ) != nullptr );
					if ( pInfo->_onValueChanged.isBound() )
						pInfo->_onValueChanged( pInfo );
					break;
				}
				case GlobalVariableType::Int32:
				case GlobalVariableType::Enum:
				{
					int32* pValPtr = static_cast<int32*>( pInfo->_pData );
					*pValPtr	   = static_cast<int32>( std::strtol( pVal, nullptr, 10 ) );
					if ( pInfo->_onValueChanged.isBound() )
						pInfo->_onValueChanged( pInfo );
					break;
				}
				case GlobalVariableType::Float:
				{
					float32* pValPtr = static_cast<float32*>( pInfo->_pData );
					*pValPtr		 = static_cast<float32>( std::strtod( pVal, nullptr ) );
					if ( pInfo->_onValueChanged.isBound() )
						pInfo->_onValueChanged( pInfo );
					break;
				}
				case GlobalVariableType::String:
				{
					string* pValPtr = static_cast<string*>( pInfo->_pData );
					*pValPtr		= pVal;
					if ( pInfo->_onValueChanged.isBound() )
						pInfo->_onValueChanged( pInfo );
					break;
				}
			}
		}
		return true;
	}

	bool EditorGlobalVariableCommands::collectPresetFiles( vector<string>& outList )
	{
		outList.clear();
		FileUtil::collectFiles( getPresetFolderPath(), ".gvpreset.xml", outList, false, false );
		return true;
	}

	bool EditorGlobalVariableCommands::collectComponentPresetFiles( vector<string>& outList )
	{
		outList.clear();
		FileUtil::collectFiles( getComponentPresetFolderPath(), ".preset.xml", outList, false, false );
		return true;
	}
} // namespace sw::editor
