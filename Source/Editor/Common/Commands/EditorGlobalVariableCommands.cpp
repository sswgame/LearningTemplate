#include "pch.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw::editor
{
    namespace
    {
        struct EditorGlobalVariableCommandsInternal
        {
            static string getTypeString( const GlobalVariableInfo& info )
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string EditorGlobalVariableCommands::getPresetFolderPath()
    {
        return ResourceUtil::getDomainFolderPath(
            GameConfig::getActive()._packRoot,
            FileUtil::joinPath( FileUtil::joinPath( path::kDataFolder, path::kPresetsFolder ), path::kGlobalVarsFolder ) );
    }

    string EditorGlobalVariableCommands::getComponentPresetFolderPath()
    {
        return ResourceUtil::getDomainFolderPath(
            GameConfig::getActive()._packRoot,
            FileUtil::joinPath( path::kDataFolder, path::kPresetsFolder ) );
    }

    bool EditorGlobalVariableCommands::savePreset( const string& filePath, const string& presetName )
    {
        GlobalVariableManager* pGvm = editor::getService<GlobalVariableManager>();
        if ( pGvm == nullptr )
            return false;

        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "GlobalVariablesPreset" );
        root.appendAttr( "name", presetName );

        const vector<string> listAllName = pGvm->collectVariableNames();
        for ( const string& varName : listAllName )
        {
            const GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
            if ( pInfo == nullptr || pInfo->_pData == nullptr )
                continue;

            XmlNode varNode = root.appendChild( "Var" );
            varNode.appendAttr( "name", pInfo->_name );
            varNode.appendAttr( "type", EditorGlobalVariableCommandsInternal::getTypeString( *pInfo ) );
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

    string EditorGlobalVariableCommands::getSessionPresetPath()
    {
        return FileUtil::joinPath( getPresetFolderPath(), "editor_session.gvpreset.xml" );
    }

    bool EditorGlobalVariableCommands::saveSessionPreset()
    {
        return savePreset( getSessionPresetPath(), "editor_session" );
    }
} // namespace sw::editor
