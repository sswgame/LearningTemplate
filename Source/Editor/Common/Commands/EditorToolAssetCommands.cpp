#include "pch.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Utility/Xml/TileMapXml.h"

namespace sw::editor
{
    namespace
    {
        struct EditorToolAssetInternal
        {
            static string resolveExistingOrRelativePath( string_view path )
            {
                if ( path.empty() )
                    return {};
                string absPath = ResourceUtil::getResourcePath( path );
                if ( absPath.empty() )
                    absPath = string{ path };
                return absPath;
            }

            /**
             * @brief 도구 문서를 읽고 **어떻게 됐는지** 답합니다 — 파일이 없음(새 문서) · 읽음 · 있는데 읽지 못함(깨졌거나 새 형식).
             * @details 예전에는 셋 다 bool 하나였고 실패는 로그도 없었다. 패널은 "없음" 과 "깨짐" 을 가를 수 없어 둘 다 앞 문서의 데이터를
             *          든 채 저장할 수 있게 두었다 — 깨진 파일을 앞 문서로 덮는 길이었다.
             */
            template <typename TAsset>
            static ToolAssetLoadResult loadToolAssetFile( TAsset& outData, const string& resolved, const utf8* pKind )
            {
                if ( resolved.empty() || FileUtil::fileExists( resolved ) == false )
                    return ToolAssetLoadResult::Missing;
                if ( outData.loadFromFile( resolved ) )
                    return ToolAssetLoadResult::Loaded;
                SW_LOG_WARNING( "Could not read %# '%#' (malformed or a newer format)", pKind, resolved );
                return ToolAssetLoadResult::Malformed;
            }

            /**
             * @brief 열린 문서 경로가 비면 에디터 기본 문서 경로를 씁니다. **둘 다 없으면 빈 문자열**이고, 그대로 파일 계층까지
             *        내려가면 `File not found: ` 처럼 이름이 빈 에러가 남습니다. 부르는 쪽에서 빈 경로를 먼저 걸러야 합니다.
             */
            static string resolveAnimGraphPath( string_view path )
            {
                if ( path.empty() == false )
                    return resolveExistingOrRelativePath( path );
                return EditorUtil::resolveEditorConfigFile( getEditorData()._animationGraphDataFile.c_str() );
            }

            static string resolveDialogueGraphPath( string_view path )
            {
                if ( path.empty() == false )
                    return resolveExistingOrRelativePath( path );
                return EditorUtil::resolveEditorConfigFile( getEditorData()._dialogueGraphDataFile.c_str() );
            }

            static string resolveSpriteClipPath( string_view path )
            {
                if ( path.empty() == false )
                    return resolveExistingOrRelativePath( path );
                return EditorUtil::resolveEditorConfigFile( getEditorData()._spriteClipFile.c_str() );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorToolAssetCommands" );

    ToolAssetLoadResult EditorToolAssetCommands::loadAnimationGraph( AnimationGraphAsset& outData, string_view path )
    {
        return EditorToolAssetInternal::loadToolAssetFile( outData, EditorToolAssetInternal::resolveAnimGraphPath( path ), "animation graph" );
    }

    bool EditorToolAssetCommands::saveAnimationGraph( const AnimationGraphAsset& data, string_view path )
    {
        // **실패도 성공만큼 분명하게 알린다.** 예전에는 성공에만 로그가 있고 실패 두 경우는 조용히 `false` 만 반환했다.
        // 호출부가 그 값을 버리고 있어서 아무 일도 일어나지 않은 것처럼 보였다.
        const string resolved = EditorToolAssetInternal::resolveAnimGraphPath( path );
        if ( resolved.empty() )
        {
            SW_LOG_ERROR( "애니메이션 그래프 저장 경로를 만들 수 없습니다: '%#'", string( path ).c_str() );
            return false;
        }
        if ( data.saveToFile( resolved ) == false )
        {
            SW_LOG_ERROR( "애니메이션 그래프 저장 실패: %#", resolved.c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved %#", resolved.c_str() );
        return true;
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadDialogueGraph( DialogueGraphAsset& outData, string_view path )
    {
        return EditorToolAssetInternal::loadToolAssetFile( outData, EditorToolAssetInternal::resolveDialogueGraphPath( path ), "dialogue graph" );
    }

    bool EditorToolAssetCommands::saveDialogueGraph( const DialogueGraphAsset& data, string_view path )
    {
        const string resolved = EditorToolAssetInternal::resolveDialogueGraphPath( path );
        if ( resolved.empty() )
        {
            SW_LOG_ERROR( "대화 그래프 저장 경로를 만들 수 없습니다: '%#'", string( path ).c_str() );
            return false;
        }
        if ( data.saveToFile( resolved ) == false )
        {
            SW_LOG_ERROR( "대화 그래프 저장 실패: %#", resolved.c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved %zu nodes, %zu links -> %#", data._listNode.size(), data._listLink.size(), resolved.c_str() );
        return true;
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadTileMap( string_view assetRelativePath, TileMapXmlData& outData, string& outStatus )
    {
        if ( assetRelativePath.empty() || ResourceUtil::hasResource( assetRelativePath ) == false )
        {
            outStatus = "No file yet: " + string{ assetRelativePath };
            return ToolAssetLoadResult::Missing;
        }
        if ( outData.load( assetRelativePath ) == false )
        {
            outStatus = "Failed to read " + string{ assetRelativePath };
            SW_LOG_WARNING( "Could not read tile map '%#' (malformed or a newer format)", assetRelativePath );
            return ToolAssetLoadResult::Malformed;
        }
        outStatus = string( "Loaded " ) + string( assetRelativePath );
        return ToolAssetLoadResult::Loaded;
    }

    bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const TileMapXmlData& data )
    {
        if ( data.save( assetRelativePath ) == false )
        {
            SW_LOG_ERROR( "타일맵 저장 실패: %#", string( assetRelativePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved %#", string( assetRelativePath ).c_str() );
        return true;
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadSpriteClip( SpriteClipAsset& outData, string& outStatus, string_view path )
    {
        outData.clear();

        const string resolved = EditorToolAssetInternal::resolveSpriteClipPath( path );
        if ( resolved.empty() || FileUtil::fileExists( resolved ) == false )
        {
            const bool bAtlasImage = EditorAssetTypeRegistry::matches( EditorAssetKind::SpriteClip, path ) &&
                                     EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, path );
            if ( bAtlasImage )
            {
                outData._atlasPath = string{ path };
                outStatus          = "Atlas from focused texture";
                return ToolAssetLoadResult::Loaded;
            }
            outStatus = resolved.empty() ? string{ "No sprite clip file yet" } : ( "No file yet: " + resolved );
            return ToolAssetLoadResult::Missing;
        }

        if ( outData.loadFromFile( resolved ) == false )
        {
            // 이유(경로:줄:열)는 런타임 로더가 이미 경고로 남겼다.
            outStatus = "Failed to read " + resolved;
            return ToolAssetLoadResult::Malformed;
        }
        outStatus = "Loaded " + resolved;
        return ToolAssetLoadResult::Loaded;
    }

    bool EditorToolAssetCommands::saveSpriteClip( const SpriteClipAsset& data, string_view path )
    {
        const string resolved = EditorToolAssetInternal::resolveSpriteClipPath( path );
        if ( resolved.empty() )
        {
            SW_LOG_ERROR( "스프라이트 클립 저장 경로를 만들 수 없습니다: '%#'", string( path ).c_str() );
            return false;
        }
        if ( data.saveToFile( resolved ) == false )
        {
            SW_LOG_ERROR( "스프라이트 클립 저장 실패: %#", resolved.c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved %#", resolved.c_str() );
        return true;
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadSequence( SequenceAsset& outAsset, string_view path )
    {
        return EditorToolAssetInternal::loadToolAssetFile( outAsset, EditorToolAssetInternal::resolveExistingOrRelativePath( path ), "sequence" );
    }

    bool EditorToolAssetCommands::saveSequence( const SequenceAsset& asset, string_view path )
    {
        // 저장 커맨드 다섯이 실패를 알리는 방식이 제각각이었다. 이것과 `saveTileMap` 은 로그가 아예 없었고, `saveSpriteClip` 은
        // 성공만 알렸다. 호출부는 반환값을 자주 버리므로 **실패가 조용하면 아무 일도 없었던 것처럼 보인다.** 다섯을 같은 모양으로
        // 맞춘다.
        const string resolved = EditorToolAssetInternal::resolveExistingOrRelativePath( path );
        if ( resolved.empty() )
        {
            SW_LOG_ERROR( "시퀀스 저장 경로를 만들 수 없습니다: '%#'", string( path ).c_str() );
            return false;
        }
        if ( asset.saveToFile( resolved ) == false )
        {
            SW_LOG_ERROR( "시퀀스 저장 실패: %#", resolved.c_str() );
            return false;
        }
        SW_LOG_INFO( "Saved %#", resolved.c_str() );
        return true;
    }

    void EditorToolAssetCommands::collectPrefabOverrides( GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
                                                          string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
                                                          vector<string>& outNestedPrefab )
    {
        outOverride.clear();
        outNestedPrefab.clear();
        outPrefabPath   = string{ prefabPath };
        outInstanceName = pInstance != nullptr ? string{ pInstance->getName().c_str() } : string{};

        EditorContext* pContext = EditorContext::get();
        if ( outPrefabPath.empty() && pInstance != nullptr && pContext != nullptr )
            outPrefabPath = pContext->getWorkspace().getGameObjectPrefabPath( pInstance->getObjectId() );
        if ( outPrefabPath.empty() && pContext != nullptr )
            outPrefabPath = pContext->getWorkspace().getFocusedAssetPath();

        ResourceManager* pResources = editor::getService<ResourceManager>();
        if ( pResources == nullptr || outPrefabPath.empty() )
            return;

        PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( outPrefabPath );
        if ( pLoaded == nullptr || pLoaded->isValid() == false )
            return;

        pLoaded->collectReferencedPrefabPaths( outNestedPrefab );

        if ( pInstance == nullptr )
            return;

        // 비교용 원형(CDO)은 **씬 밖**에서 만든다 — 언리얼 CDO 처럼 월드에 들지 않는다. 예전에는 활성 씬 매니저 안에 임시 오브젝트로 만들어
        // 지울 때까지 씬 쪽(틱 · 계층 · 저장)에 보였다.
        GameObjectManager scratch;
        GameObject*       pCdo = scratch.createGameObject( hashed_string( "__PrefabDiffCdo" ) );
        if ( pLoaded->applyStateTo( pCdo ) == false )
            return;

        collectComponentOverrides( pInstance, pCdo, outOverride );
    }

    void EditorToolAssetCommands::collectComponentOverrides( GameObject* pInstance, GameObject* pCdo, vector<PrefabOverrideItem>& outListOverride )
    {
        if ( pInstance == nullptr || pCdo == nullptr )
            return;
        const SerializeContext& ctx = SerializeContext::getDefault();
        for ( Component* pInstanceComponent : pInstance->getComponents() )
        {
            if ( pInstanceComponent == nullptr || pInstanceComponent->getTypeInfo() == nullptr )
                continue;
            const TypeInfo* pTypeInfo = pInstanceComponent->getTypeInfo();
            const string    key       = ComponentStableKey::makeKey( pInstanceComponent );
            Component*      pCdoComp  = ComponentStableKey::findComponent( pCdo, key );
            if ( pCdoComp == nullptr || pCdoComp->getTypeInfo() != pTypeInfo )
                continue;

            pTypeInfo->forEachProperty(
                [&]( const PropertyInfo& prop )
            {
                if ( prop._metadata._bTransient == SW_TRUE )
                    return;
                PrefabOverrideItem item{};
                item._componentName   = pTypeInfo->_name.c_str();
                item._componentKey    = key;
                item._propertyName    = prop._name.c_str();
                item._defaultValue    = SerializerUtil::formatPropertyText( prop, pCdoComp, ctx );
                item._overriddenValue = SerializerUtil::formatPropertyText( prop, pInstanceComponent, ctx );
                item._bModified       = ( SerializerUtil::arePropertyValuesEqual( prop, pCdoComp, pInstanceComponent, ctx ) == false );
                outListOverride.push_back( std::move( item ) );
            },
                true );
        }
    }

    void EditorToolAssetCommands::revertPrefabOverride( GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath )
    {
        if ( pInstance == nullptr || prefabPath.empty() )
        {
            item._overriddenValue = item._defaultValue;
            item._bModified       = false;
            return;
        }

        ResourceManager* pResources = editor::getService<ResourceManager>();
        if ( pResources == nullptr )
            return;
        PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( prefabPath );
        if ( pLoaded == nullptr )
            return;

        // 비교용 원형(CDO)은 씬 밖에서 만든다(위 `collectPrefabOverrides` 설명). 형식은 프리팹이 안다 — 예전에는 XML 로만 읽어 JSON 프리팹의
        // 원형이 비었고, 되돌릴 값을 찾지 못했다.
        GameObjectManager scratch;
        GameObject*       pCdo = scratch.createGameObject( hashed_string( "__PrefabRevertCdo" ) );
        if ( pLoaded->applyStateTo( pCdo ) == false )
            return;
        (void)revertComponentOverride( pInstance, pCdo, item ); // 실패는 그대로 남은 항목과 경고가 알린다
    }

    bool EditorToolAssetCommands::revertComponentOverride( GameObject* pInstance, GameObject* pCdo, PrefabOverrideItem& item )
    {
        if ( pInstance == nullptr || pCdo == nullptr )
            return false;
        Component* pInstanceComponent = ComponentStableKey::findComponent( pInstance, item._componentKey );
        Component* pCdoComp           = ComponentStableKey::findComponent( pCdo, item._componentKey );
        if ( pInstanceComponent == nullptr || pCdoComp == nullptr || pInstanceComponent->getTypeInfo() == nullptr ||
             pInstanceComponent->getTypeInfo() != pCdoComp->getTypeInfo() )
        {
            SW_LOG_WARNING( "Prefab override '%#.%#' has no matching component to revert", item._componentKey.c_str(), item._propertyName.c_str() );
            return false;
        }
        const PropertyInfo* pProp = pInstanceComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( item._propertyName.c_str() ) );
        if ( pProp == nullptr )
            return false;

        const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pInstance );
        // 값을 옮기지 못했으면 되돌렸다고 표시하지 않는다(예전에는 실패해도 "되돌림" 으로 표시하고 되돌리기 기록을 남겼다).
        if ( SerializerUtil::copyPropertyValue( *pProp, pCdoComp, pInstanceComponent, SerializeContext::getDefault() ) == false )
        {
            SW_LOG_WARNING( "Prefab override '%#.%#' could not be reverted", item._componentKey.c_str(), item._propertyName.c_str() );
            return false;
        }
        pInstanceComponent->onPropertyChanged( pProp->_name );
        const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pInstance );
        EditorTransaction::recordModify( pInstance, beforeSnapshot, afterSnapshot, "Revert Prefab Override" );
        item._overriddenValue = item._defaultValue;
        item._bModified       = false;
        return true;
    }

    bool EditorToolAssetCommands::applyPrefabOverridesToTemplate( GameObject* pInstance, string_view prefabPath )
    {
        return EditorInspectorCommands::applyToPrefab( pInstance, prefabPath );
    }

    bool EditorToolAssetCommands::revertAllPrefabOverrides( GameObject* pInstance, string_view prefabPath )
    {
        return EditorInspectorCommands::revertToPrefab( pInstance, prefabPath );
    }
} // namespace sw::editor
