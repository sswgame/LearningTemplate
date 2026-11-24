#include "pch.h"

#include "Editor/Common/Commands/Diagnostics/EditorGlobalVariableCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Commands/Asset/EditorContentWrite.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        struct EditorGlobalVariableCommandsInternal
        {
            static constexpr const utf8* kSessionPresetName = "editor_session";
            static constexpr const utf8* kRootName          = "GlobalVariablesPreset";

            static void appendVariable( XMLNode& root, const GlobalVariableInfo& info )
            {
                XMLNode varNode = root.appendChild( "Var" );
                varNode.appendAttribute( "name", info._name );
                varNode.appendAttribute( "type", info.getTypeName() );
                varNode.appendAttribute( "value", info.getValueAsString() );
            }

            /** @brief 문서의 변수 줄마다 `func( info, value )` 를 부릅니다. 등록되지 않은 이름은 건너뜁니다. */
            template <typename TFunc>
            static void forEachVariable( const XMLDocument& doc, TFunc&& func )
            {
                const XMLNode root = doc.getRoot();
                if ( root.isValid() == false )
                    return;
                GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();
                for ( XMLNode varNode = root.findChild( "Var" ); varNode.isValid(); varNode = varNode.findNextSibling( "Var" ) )
                {
                    const utf8* pName  = varNode.findAttribute( "name" );
                    const utf8* pValue = varNode.findAttribute( "value" );
                    if ( pName == nullptr || pValue == nullptr )
                        continue;
                    GlobalVariableInfo* pInfo = gvm.findVariable( pName );
                    if ( pInfo != nullptr )
                        func( *pInfo, pValue );
                }
            }
        };
    } // namespace

    string EditorGlobalVariableCommands::getPresetFolderPath()
    {
        // 앱이 쓰는 상태라 팩 밖이다 — 팩 안(`data/presets/globalvars`)에 두면 쿠킹이 게임 팩에 싣는다. 팩마다 폴더를 나눈다.
        const AbsolutePath stateDir = EditorPaths::getEditorStateDirectory();
        if ( stateDir.empty() )
            return {};
        const string packName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( editor::getSubsystem<const GameConfig>()._packRoot.get() ) );
        return FileUtil::joinPath( FileUtil::joinPath( stateDir.str(), "GlobalVariablePresets" ), packName );
    }

    string EditorGlobalVariableCommands::getComponentPresetFolderPath()
    {
        return EditorPaths::findContentFolder( FileUtil::joinPath( FileUtil::joinPath( editor::getSubsystem<const GameConfig>()._packRoot.get(), constant::path::kDataFolder ), constant::path::kPresetsFolder ) );
    }

    string EditorGlobalVariableCommands::makePresetText( string_view presetName )
    {
        GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( EditorGlobalVariableCommandsInternal::kRootName );
        root.appendAttribute( "name", string{ presetName } );

        vector<GlobalVariableInfo*> listVariable;
        gvm.collectVariables( listVariable );
        for ( const GlobalVariableInfo* pInfo : listVariable )
        {
            // 테스트용은 저장하지 않는다. 프리셋에 `gv_profileFrames` 가 들어가면 불러온 에디터가 N 프레임 뒤 스스로 꺼지고,
            // `gv_crashTest` 가 들어가면 일부러 죽는다. 실행 한 번에만 줄 스위치다.
            const bool bSkipVariable = pInfo->_pData == nullptr || pInfo->_bTestOnly;
            if ( bSkipVariable )
                continue;

            EditorGlobalVariableCommandsInternal::appendVariable( root, *pInfo );
        }

        return doc.saveToString();
    }

    bool EditorGlobalVariableCommands::savePreset( const string& filePath, const string& presetName )
    {
        if ( EditorContentWrite::warnIfReadOnly( filePath, EditorContentWriteKind::Save ) ) // 프리셋 단추 · 패널 저장이 모두 지나는 쓰기 입구다
            return false;
        return FileUtil::ensureParentDirectoryExists( FileUtil::toDiskPath( filePath ) ) && ResourceComposer::saveText( FileUtil::toDiskPath( filePath ), ResourceSaveText::makeComplete( makePresetText( presetName ) ) );
    }

    bool EditorGlobalVariableCommands::loadPreset( const string& filePath )
    {
        string presetText;
        return FileUtil::readTextFile( FileUtil::toDiskPath( filePath ), presetText ) && applyPresetText( presetText );
    }

    bool EditorGlobalVariableCommands::applyPresetText( string_view presetText )
    {
        XMLDocument doc;
        if ( doc.parse( presetText ) == false || doc.getRoot().isValid() == false )
            return false;
        EditorGlobalVariableCommandsInternal::forEachVariable( doc, []( GlobalVariableInfo& info, const utf8* pValue )
        {
            // 프리셋 파일에 테스트용 변수가 들어 있어도 그 값은 적용하지 않는다.
            if ( info._bTestOnly == false )
                info.setValueFromString( pValue );
        } );
        return true;
    }

    vector<string> EditorGlobalVariableCommands::collectPresetVariables( const string& filePath )
    {
        vector<string> listName;
        XMLDocument    doc;
        if ( doc.loadFromFile( filePath ) )
        {
            EditorGlobalVariableCommandsInternal::forEachVariable( doc, [&listName]( const GlobalVariableInfo& info, const utf8* /*pValue*/ )
            {
                if ( info._bTestOnly == false )
                    listName.push_back( info._name );
            } );
        }
        return listName;
    }

    string EditorGlobalVariableCommands::captureValues( const vector<string>& listName )
    {
        GlobalVariableManager& gvm = editor::getSubsystem<GlobalVariableManager>();
        XMLDocument            doc;
        XMLNode                root = doc.appendRoot( EditorGlobalVariableCommandsInternal::kRootName );
        for ( const string& name : listName )
        {
            const GlobalVariableInfo* pInfo = gvm.findVariable( name );
            if ( pInfo != nullptr )
                EditorGlobalVariableCommandsInternal::appendVariable( root, *pInfo );
        }
        return doc.saveToString();
    }

    void EditorGlobalVariableCommands::restoreValues( string_view text )
    {
        XMLDocument doc;
        if ( doc.parse( text, "global variable undo" ) == false )
            return;
        EditorGlobalVariableCommandsInternal::forEachVariable( doc, []( GlobalVariableInfo& info, const utf8* pValue )
        { info.setValueFromString( pValue ); } );
    }

    string EditorGlobalVariableCommands::getSessionPresetPath()
    {
        return FileUtil::joinPath( getPresetFolderPath(), "editor_session.gvpreset.xml" );
    }

    string EditorGlobalVariableCommands::makeSessionPresetText()
    {
        return makePresetText( EditorGlobalVariableCommandsInternal::kSessionPresetName );
    }

    bool EditorGlobalVariableCommands::saveSessionPreset()
    {
        return savePreset( getSessionPresetPath(), EditorGlobalVariableCommandsInternal::kSessionPresetName );
    }
} // namespace sw::editor
