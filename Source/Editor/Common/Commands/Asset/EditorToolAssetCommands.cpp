#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/TileMap/TileMapXML.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct ToolDocumentDesc
         * @brief 도구 문서 종류 하나입니다. 로그와 상태 문구에 쓰는 이름, 경로가 비었을 때 여는 에디터 기본 파일을 가지고 있습니다.
         * @details 읽기, 쓰기, 실패 알림은 `loadToolDocument` 와 `saveToolDocument` 하나씩만 있다. 종류마다 다른 것은 이 기술자와
         *          파일 IO 오버로드(`readDocument` / `writeDocument`)뿐이다.
         */
        struct ToolDocumentDesc
        {
            const utf8* _pLabel;           ///< "animation graph" 처럼 로그와 상태 문구에 들어가는 이름
            const utf8* _pDefaultFileName; ///< 경로가 비었을 때 여는 기본 파일(`Saved/Editor` 아래 이름). nullptr 이면 기본 문서가 없다
        };

        constexpr ToolDocumentDesc kAnimationGraphDocument{ "animation graph", EditorPaths::kAnimationGraphDocumentFileName };
        constexpr ToolDocumentDesc kDialogueGraphDocument{ "dialogue graph", EditorPaths::kDialogueGraphDocumentFileName };
        constexpr ToolDocumentDesc kSpriteClipDocument{ "sprite clip", EditorPaths::kSpriteClipDocumentFileName };
        constexpr ToolDocumentDesc kTileMapDocument{ "tile map", nullptr };
        constexpr ToolDocumentDesc kSequenceDocument{ "sequence", nullptr };

        struct EditorToolAssetInternal
        {
            static string resolveExistingOrRelativePath( string_view path )
            {
                if ( path.empty() )
                    return {};
                string absPath = ResourcePaths::findLooseFile( path ).getDiskPath();
                if ( absPath.empty() )
                    absPath = string{ path };
                return absPath;
            }

            /**
             * @brief 문서 경로입니다. 열린 문서 경로가 비면 기본 문서 경로이고, **둘 다 없으면 빈 문자열**입니다.
             * @details 빈 경로를 파일 계층까지 넘기면 `File not found: ` 처럼 이름이 빈 오류가 남는다. 그래서 부르는 쪽(`loadToolDocument`,
             *          `saveToolDocument`)이 빈 경로를 먼저 거른다.
             */
            static string resolveDocumentPath( const ToolDocumentDesc& desc, string_view path )
            {
                if ( path.empty() == false )
                    return resolveExistingOrRelativePath( path );
                if ( desc._pDefaultFileName == nullptr )
                    return {};
                return EditorPaths::resolveEditorStateFile( desc._pDefaultFileName );
            }

            // 도구 문서는 디스크 파일이다(받은 리소스 id 를 낱개 파일로 풀었거나 `Saved/Editor` 기본 문서) — 디스크 입구로 읽는다. 저장(`writeDocument`)도 디스크다.
            template <typename TAsset>
            [[nodiscard]] static bool readDocument( TAsset& outData, const string& resolved )
            {
                return outData.loadFromDiskFile( resolved );
            }

            template <typename TAsset>
            [[nodiscard]] static bool writeDocument( const TAsset& data, const string& resolved )
            {
                return data.saveToFile( resolved );
            }

            [[nodiscard]] static bool writeDocument( const TileMapXMLData& data, const string& resolved ) { return data.save( resolved ); }

            /** @brief 상태 문구에 보일 경로입니다. 받은 경로(리소스 id)가 있으면 그것이고, 없으면 해석한 경로입니다. */
            static string makeShownPath( string_view path, const string& resolved ) { return path.empty() ? resolved : string{ path }; }

            /**
             * @brief 도구 문서를 읽고 **어떻게 됐는지** 돌려줍니다. 파일이 없음(새 문서), 읽음, 있는데 읽지 못함(깨졌거나 새 형식) 중 하나입니다.
             * @details 다섯 종류가 같은 형태로 알린다. 읽지 못하면 경고 한 줄(`Could not read the <종류> '<경로>'`)을 남기고, @p pOutStatus 가
             *          있으면 패널 상태 문구(`No file yet: …`, `Failed to read …`, `Loaded …`)를 쓴다.
             */
            template <typename TAsset>
            static ToolAssetLoadResult loadToolDocument( const ToolDocumentDesc& desc, TAsset& outData, string_view path, string* pOutStatus )
            {
                const string resolved = resolveDocumentPath( desc, path );
                if ( resolved.empty() || FileUtil::isRegularFile( resolved ) == false )
                {
                    if ( pOutStatus != nullptr )
                        *pOutStatus = resolved.empty() ? ( string( "No " ) + desc._pLabel + " file yet" ) : ( "No file yet: " + makeShownPath( path, resolved ) );
                    return ToolAssetLoadResult::Missing;
                }
                if ( readDocument( outData, resolved ) == false )
                {
                    SW_LOG_WARNING( "Could not read the %# '%#' (malformed or a newer format)", desc._pLabel, resolved );
                    if ( pOutStatus != nullptr )
                        *pOutStatus = "Failed to read " + makeShownPath( path, resolved );
                    return ToolAssetLoadResult::Malformed;
                }
                if ( pOutStatus != nullptr )
                    *pOutStatus = "Loaded " + makeShownPath( path, resolved );
                return ToolAssetLoadResult::Loaded;
            }

            /** @brief 도구 문서를 씁니다. 경로 없음과 쓰기 실패를 모두 오류 한 줄로 알리므로, 호출부가 반환값을 버려도 조용히 넘어가지 않습니다. */
            template <typename TAsset>
            [[nodiscard]] static bool saveToolDocument( const ToolDocumentDesc& desc, const TAsset& data, string_view path )
            {
                const string resolved = resolveDocumentPath( desc, path );
                if ( resolved.empty() )
                {
                    SW_LOG_ERROR( "Failed to save the %# - no file path and no default file", desc._pLabel );
                    return false;
                }
                if ( writeDocument( data, resolved ) == false )
                {
                    SW_LOG_ERROR( "Failed to save the %# to '%#'", desc._pLabel, resolved );
                    return false;
                }
                SW_LOG_INFO( "Saved the %# to '%#'", desc._pLabel, resolved );
                return true;
            }

            /** @brief 스프라이트 클립 문서가 아닌 이미지인지입니다. 그런 경로는 문서로 읽지 않고 아틀라스로 씁니다. */
            static bool isAtlasImagePath( string_view path )
            {
                return EditorAssetTypeRegistry::matches( EditorAssetType::SpriteClip, path ) &&
                       EditorAssetTypeRegistry::matches( EditorAssetType::Texture, path );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorToolAssetCommands" );

    ToolAssetLoadResult EditorToolAssetCommands::loadAnimationGraph( AnimationGraphAsset& outData, string_view path )
    {
        return EditorToolAssetInternal::loadToolDocument( kAnimationGraphDocument, outData, path, nullptr );
    }

    bool EditorToolAssetCommands::saveAnimationGraph( const AnimationGraphAsset& data, string_view path )
    {
        return EditorToolAssetInternal::saveToolDocument( kAnimationGraphDocument, data, path );
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadDialogueGraph( DialogueGraphAsset& outData, string_view path )
    {
        return EditorToolAssetInternal::loadToolDocument( kDialogueGraphDocument, outData, path, nullptr );
    }

    bool EditorToolAssetCommands::saveDialogueGraph( const DialogueGraphAsset& data, string_view path )
    {
        return EditorToolAssetInternal::saveToolDocument( kDialogueGraphDocument, data, path );
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadTileMap( string_view assetRelativePath, TileMapXMLData& outData, string& outStatus )
    {
        return EditorToolAssetInternal::loadToolDocument( kTileMapDocument, outData, assetRelativePath, &outStatus );
    }

    bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const TileMapXMLData& data )
    {
        return EditorToolAssetInternal::saveToolDocument( kTileMapDocument, data, assetRelativePath );
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadSpriteClip( SpriteClipAsset& outData, string& outStatus, string_view path )
    {
        outData.clear();
        // 클립 문서가 아닌 이미지는 문서로 읽지 않는다. 그 이미지를 아틀라스로 삼아 새 클립을 시작한다.
        if ( EditorToolAssetInternal::isAtlasImagePath( path ) )
        {
            outData._atlasPath = string{ path };
            outStatus          = "Atlas from focused texture";
            return ToolAssetLoadResult::Loaded;
        }
        return EditorToolAssetInternal::loadToolDocument( kSpriteClipDocument, outData, path, &outStatus );
    }

    bool EditorToolAssetCommands::saveSpriteClip( const SpriteClipAsset& data, string_view path )
    {
        return EditorToolAssetInternal::saveToolDocument( kSpriteClipDocument, data, path );
    }

    ToolAssetLoadResult EditorToolAssetCommands::loadSequence( SequenceAsset& outAsset, string_view path )
    {
        return EditorToolAssetInternal::loadToolDocument( kSequenceDocument, outAsset, path, nullptr );
    }

    bool EditorToolAssetCommands::saveSequence( const SequenceAsset& asset, string_view path )
    {
        return EditorToolAssetInternal::saveToolDocument( kSequenceDocument, asset, path );
    }

    void EditorToolAssetCommands::collectPrefabOverrides( GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
                                                          string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
                                                          vector<string>& outNestedPrefab )
    {
        outOverride.clear();
        outNestedPrefab.clear();
        outPrefabPath   = string{ prefabPath };
        outInstanceName = pInstance != nullptr ? string{ pInstance->getName().c_str() } : string{};

        EditorContext* pContext             = editor::findSubsystem<EditorContext>();
        const bool     bCanLookUpPrefabPath = outPrefabPath.empty() && pInstance != nullptr && pContext != nullptr;
        if ( bCanLookUpPrefabPath )
            outPrefabPath = pContext->getSubsystem<EditorWorkspace>().findPrefabPath( pInstance );
        // 포커스된 에셋으로 넘어가지 않는다. 머티리얼이나 씬일 수 있어 프리팹으로 읽으면 [Error] 를 남긴다. 프리팹 포커스는 부르는 쪽이 종류를 보고 넘긴다.

        if ( outPrefabPath.empty() )
            return;

        const shared_ptr<const PrefabAsset> pLoaded = editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().loadPrefab( outPrefabPath );
        if ( pLoaded == nullptr || pLoaded->isValid() == false )
            return;

        pLoaded->collectReferencedPrefabPaths( outNestedPrefab );

        if ( pInstance == nullptr )
            return;

        // 비교용 원형(CDO)은 **씬 밖**에서 만든다. 언리얼 CDO 처럼 월드에 들어가지 않는다. 활성 씬 매니저 안에 임시 오브젝트로 만들면
        // 지울 때까지 틱, 계층, 저장에 보인다.
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
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
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
                if ( prop._metadata->_bTransient == SW_TRUE )
                    return;
                PrefabOverrideItem item{};
                item._componentName = pTypeInfo->getDisplayName();
                item._componentKey  = key;
                item._propertyName  = prop._name.c_str();
#if !defined( SW_SHIPPING )
                // 표시 이름 메타데이터는 Shipping 에서 빠진다. EditorTest 는 Shipping 에서도 이 파일을 빌드한다.
                item._propertyLabel = EditorDisplayNameUtil::chooseLabel( prop._metadata->_displayName, prop._name.c_str() );
#else
                item._propertyLabel = EditorDisplayNameUtil::makeDisplayName( prop._name.c_str() );
#endif
                item._defaultValue    = SerializerUtil::formatPropertyText( prop, ObjectAddress{ pCdoComp }, context );
                item._overriddenValue = SerializerUtil::formatPropertyText( prop, ObjectAddress{ pInstanceComponent }, context );
                item._bModified       = ( SerializerUtil::arePropertyValuesEqual( prop, ObjectAddress{ pCdoComp }, ObjectAddress{ pInstanceComponent }, context ) == false );
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

        const shared_ptr<const PrefabAsset> pLoaded = editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().loadPrefab( prefabPath );
        if ( pLoaded == nullptr )
            return;

        // 비교용 원형(CDO)은 씬 밖에서 만든다(위 `collectPrefabOverrides` 설명). 형식은 프리팹이 알고 있다. XML 로만 읽으면 JSON 프리팹의
        // 원형이 비어 되돌릴 값을 찾지 못한다.
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
        Component* pInstanceComponent   = ComponentStableKey::findComponent( pInstance, item._componentKey );
        Component* pCdoComp             = ComponentStableKey::findComponent( pCdo, item._componentKey );
        const bool bNoMatchingComponent = pInstanceComponent == nullptr || pCdoComp == nullptr || pInstanceComponent->getTypeInfo() == nullptr ||
                                          pInstanceComponent->getTypeInfo() != pCdoComp->getTypeInfo();
        if ( bNoMatchingComponent )
        {
            SW_LOG_WARNING( "Prefab override '%#.%#' has no matching component to revert", item._componentKey.c_str(), item._propertyName.c_str() );
            return false;
        }
        const PropertyInfo* pProp = pInstanceComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( item._propertyName.c_str() ) );
        if ( pProp == nullptr )
            return false;

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pInstance );
        // 값을 옮기지 못했으면 되돌렸다고 표시하지 않고 되돌리기 기록도 남기지 않는다.
        if ( SerializerUtil::copyPropertyValue( *pProp, ObjectAddress{ pCdoComp }, MutableObjectAddress{ pInstanceComponent }, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
        {
            SW_LOG_WARNING( "Prefab override '%#.%#' could not be reverted", item._componentKey.c_str(), item._propertyName.c_str() );
            return false;
        }
        pInstanceComponent->onPropertyChanged( pProp->_name );
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pInstance );
        EditorTransaction::recordModify( pInstance, beforeSnapshot, afterSnapshot, "Revert Prefab Override" );
        item._overriddenValue = item._defaultValue;
        item._bModified       = false;
        return true;
    }

    bool EditorToolAssetCommands::applyPrefabOverridesToTemplate( GameObject* pInstance, const ResourceID& resourceID )
    {
        return EditorInspectorCommands::applyToPrefab( pInstance, resourceID );
    }

    bool EditorToolAssetCommands::revertAllPrefabOverrides( GameObject* pInstance, string_view prefabPath )
    {
        return EditorInspectorCommands::revertToPrefab( pInstance, prefabPath );
    }
} // namespace sw::editor
