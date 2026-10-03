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
    string EditorGlobalVariableCommands::getTypeString( const GlobalVariableInfo& info )
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
        }
        return "Unknown";
    }

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
        root.appendAttribute( "name", presetName );

        const vector<string> listAllName = pGvm->collectVariableNames();
        for ( const string& varName : listAllName )
        {
            // 테스트용은 저장하지 않는다. 프리셋에 `gv_profileFrames` 가 들어가면 불러온 에디터가 N 프레임 뒤 스스로 꺼지고,
            // `gv_crashTest` 가 들어가면 일부러 죽는다. 실행 한 번에만 줄 스위치다.
            const GlobalVariableInfo* pInfo = pGvm->findVariable( varName );
            if ( pInfo == nullptr || pInfo->_pData == nullptr || pInfo->_bTestOnly )
                continue;

            XmlNode varNode = root.appendChild( "Var" );
            varNode.appendAttribute( "name", pInfo->_name );
            varNode.appendAttribute( "type", getTypeString( *pInfo ) );
            varNode.appendAttribute( "value", pInfo->getValueAsString() );
        }

        FileUtil::ensureParentDirectoryExists( filePath );
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

        XmlNode rootNode = doc.getRoot();
        if ( rootNode.isValid() == false )
            return false;

        for ( XmlNode varNode = rootNode.findChild( "Var" ); varNode.isValid(); varNode = varNode.findNextSibling( "Var" ) )
        {
            const utf8* pName = varNode.findAttribute( "name" );
            const utf8* pVal  = varNode.findAttribute( "value" );
            if ( pName == nullptr || pVal == nullptr )
                continue;

            // 테스트용을 담은 옛 프리셋(이 필터 전에 저장한 것)도 그 값은 적용하지 않는다.
            GlobalVariableInfo* pInfo = pGvm->findVariable( pName );
            if ( pInfo == nullptr || pInfo->_bTestOnly )
                continue;
            pInfo->setValueFromString( pVal );
        }
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
