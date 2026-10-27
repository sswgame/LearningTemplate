#include "pch.h"

#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    string EditorGlobalVariableCommands::getPresetFolderPath()
    {
        // 앱이 쓰는 상태라 팩 밖이다 — 팩 안(`data/presets/globalvars`)에 두면 쿠킹이 게임 팩에 싣는다. 팩마다 폴더를 나눈다.
        const string stateDir = EditorPaths::getEditorStateDirectory();
        if ( stateDir.empty() )
            return {};
        const string packName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( editor::getSubsystem<const GameConfig>()._packRoot.get() ) );
        return FileUtil::joinPath( FileUtil::joinPath( stateDir, "GlobalVariablePresets" ), packName );
    }

    string EditorGlobalVariableCommands::getComponentPresetFolderPath()
    {
        return ResourcePaths::findLooseDomainFolder(
                   editor::getSubsystem<const GameConfig>()._packRoot.get(),
                   FileUtil::joinPath( path::kDataFolder, path::kPresetsFolder ) )
            .getDiskPath();
    }

    bool EditorGlobalVariableCommands::savePreset( const string& filePath, const string& presetName )
    {
        GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "GlobalVariablesPreset" );
        root.appendAttribute( "name", presetName );

        vector<GlobalVariableInfo*> listVariable;
        gvm.collectVariables( listVariable );
        for ( const GlobalVariableInfo* pInfo : listVariable )
        {
            // 테스트용은 저장하지 않는다. 프리셋에 `gv_profileFrames` 가 들어가면 불러온 에디터가 N 프레임 뒤 스스로 꺼지고,
            // `gv_crashTest` 가 들어가면 일부러 죽는다. 실행 한 번에만 줄 스위치다.
            const bool bSkipVariable = pInfo->_pData == nullptr || pInfo->_bTestOnly;
            if ( bSkipVariable )
                continue;

            XMLNode varNode = root.appendChild( "Var" );
            varNode.appendAttribute( "name", pInfo->_name );
            varNode.appendAttribute( "type", pInfo->getTypeName() );
            varNode.appendAttribute( "value", pInfo->getValueAsString() );
        }

        return FileUtil::ensureParentDirectoryExists( filePath ) && doc.saveFile( filePath );
    }

    bool EditorGlobalVariableCommands::loadPreset( const string& filePath )
    {
        GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();

        XMLDocument doc;
        if ( doc.loadFile( filePath ) == false )
            return false;

        XMLNode rootNode = doc.getRoot();
        if ( rootNode.isValid() == false )
            return false;

        for ( XMLNode varNode = rootNode.findChild( "Var" ); varNode.isValid(); varNode = varNode.findNextSibling( "Var" ) )
        {
            const utf8* pName = varNode.findAttribute( "name" );
            const utf8* pVal  = varNode.findAttribute( "value" );
            if ( pName == nullptr || pVal == nullptr )
                continue;

            // 프리셋 파일에 테스트용 변수가 들어 있어도 그 값은 적용하지 않는다.
            GlobalVariableInfo* pInfo = gvm.findVariable( pName );
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
