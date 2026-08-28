#include "pch.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

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

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "GlobalVariablesPreset" );
		root.appendAttr( "name", presetName );

		const vector<string> listAllNames = pGvm->collectVariableNames();
		for ( const string& varName : listAllNames )
		{
			const GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
			if ( pInfo == nullptr || pInfo->_pData == nullptr )
				continue;

			XmlNode varNode = root.appendChild( "Var" );
			varNode.appendAttr( "name", pInfo->_name );
			varNode.appendAttr( "type", getTypeString( *pInfo ) );
			varNode.appendAttr( "value", pInfo->getValueAsString() );
		}

		const string dir = FileUtil::getDirectoryPart( filePath );
		FileUtil::ensureDirectoryExists( dir );
		return doc.saveFile( filePath );
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
			if ( pInfo == nullptr )
				continue;
			pInfo->setValueFromString( pVal );
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
