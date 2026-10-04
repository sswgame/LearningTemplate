#include "pch.h"

#include "Engine/Scene/SceneCooker.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    namespace
    {
        struct SceneCookerInternal
        {
            /** @brief 파일 id → 그 엔티티로 지은 오브젝트 표를 만듭니다(씬의 런타임 id → 파일 id 표를 뒤집습니다). */
            static void makeObjectByFileId( const Scene& scene, unordered_map<uint64, GameObject*>& outMap )
            {
                outMap.clear();
                const GameObjectManager* pManager = scene.getObjectManager();
                if ( pManager == nullptr )
                    return;
                ObjectSavedIdMap mapSavedId;
                scene.collectSavedIdMap( mapSavedId );
                for ( const auto& [objectId, fileId] : mapSavedId )
                {
                    GameObject* pObject = pManager->findGameObjectById( objectId );
                    if ( pObject != nullptr )
                        outMap.emplace( fileId, pObject );
                }
            }

            /** @brief 오브젝트의 상태를 파일 id 로 적은 XML 입니다. 쿠킹 전 · 쿠킹된 뒤의 상태를 견주는 기준입니다. */
            static string makeStateText( const Scene& scene, const GameObject* pObject )
            {
                ObjectSavedIdMap mapSavedId;
                scene.collectSavedIdMap( mapSavedId );
                ObjectSaveOptions options{};
                options._pSavedIdMap = &mapSavedId;
                return ObjectStateSerializer::saveToXmlString( pObject, options );
            }

            /**
             * @brief 씬에서 모르는 타입으로 지어진 컴포넌트(`MissingComponent`)를 오브젝트 · 원래 타입 이름과 함께 오류로 알리고 그 수를 돌려줍니다.
             * @details 그 컴포넌트는 원문 그대로 쿠킹돼 배포본에서도 `MissingComponent` 로 읽힌다 — 동작하지 않는 컴포넌트가 실린다.
             */
            static uint32 reportMissingComponents( const Scene& scene )
            {
                const GameObjectManager* pManager = scene.getObjectManager();
                if ( pManager == nullptr )
                    return 0;
                uint32              missingCount{ 0 };
                vector<GameObject*> listObject;
                pManager->getAllGameObjects( listObject );
                for ( const GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr )
                        continue;
                    for ( const Component* pComp : pObject->getComponents() )
                    {
                        if ( pComp == nullptr || pComp->getTypeInfo() != MissingComponent::StaticType() )
                            continue;
                        SW_LOG_ERROR( "Scene cook: '%#' has a component of type '%#' that no loaded module registers", pObject->getName().c_str(),
                                      static_cast<const MissingComponent*>( pComp )->getOriginalTypeName().c_str() );
                        ++missingCount;
                    }
                }
                return missingCount;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneCooker" );

    uint32 SceneCooker::cookEntityState( SceneDocument& inoutDoc, uint32* pOutMissingComponentCount )
    {
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = 0;
        // **런타임이 읽는 그대로 짓고 쿠킹한다.** 엔티티를 하나씩 따로 읽으면 부모가 문서에서 뒤에 있는 자식이 부모를 찾지 못하고 저장이
        // 그 연결을 지운다(배포본에서 자식이 루트가 된다). 그래서 문서 전체를 `Scene::instantiate`(프리팹 스폰 · 묶음 부착까지 런타임과
        // 같은 길)로 짓고, 쿠킹된 문서를 다시 지어 엔티티마다 상태 전체를 견준다(컴포넌트 타입 목록만 보면 값이 어긋나도 통과한다).
        // 엔티티는 파일 id 로 찾는다 — 쿠킹된 상태의 부착도 파일 id 로만 부모를 가리킨다. id 없는 엔티티는 읽는 쪽이 받지 않는 문서다.
        for ( const SceneDocument::SceneObjectNode& entity : inoutDoc._listSceneObjectNode )
        {
            if ( entity._fileId == 0 )
            {
                SW_LOG_ERROR( "Scene cook: entity '%#' of '%#' has no id - nothing is cooked", entity._name, inoutDoc._name );
                return 0;
            }
        }

        // 쿠킹 전용 씬이다. 실제 씬 매니저의 것을 쓰면 쿠킹하는 동안 만든 임시 오브젝트가 실제 씬에 남는다.
        Scene source{ "SceneCooker.Source" };
        if ( source.instantiate( inoutDoc ) == false )
            return 0;
        const uint32 missingComponentCount = SceneCookerInternal::reportMissingComponents( source );
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = missingComponentCount;
        unordered_map<uint64, GameObject*> mapSourceByFileId;
        SceneCookerInternal::makeObjectByFileId( source, mapSourceByFileId );

        ObjectSavedIdMap mapSourceSavedId;
        source.collectSavedIdMap( mapSourceSavedId );
        ObjectSaveOptions saveOptions{};
        saveOptions._pSavedIdMap = &mapSourceSavedId;

        SceneDocument cooked = inoutDoc;
        for ( SceneDocument::SceneObjectNode& entity : cooked._listSceneObjectNode )
        {
            if ( entity._embeddedXml.empty() )
                continue;
            const auto  sourceIt = mapSourceByFileId.find( entity._fileId );
            GameObject* pSource  = ( sourceIt != mapSourceByFileId.end() ) ? sourceIt->second : nullptr;
            if ( pSource == nullptr )
                continue;
            vector<uint8> stateBytes;
            if ( ObjectStateSerializer::saveToBinaryBuffer( pSource, stateBytes, saveOptions ) && stateBytes.empty() == false )
            {
                entity._embeddedStateBytes = std::move( stateBytes );
                entity._embeddedXml.clear();
            }
        }

        // **쿠킹된 것을 다시 지어 본다.** 모르는 컴포넌트 타입 · 값이 바뀌는 필드 · 부모를 잃는 부착이 있으면 그 엔티티의 상태가 어긋나고, 그
        // 엔티티는 XML 로 남는다. 쿠킹이 조용히 무언가를 떨어뜨리지 않는다.
        Scene verify{ "SceneCooker.Verify" };
        if ( verify.instantiate( cooked ) == false )
            return 0;
        unordered_map<uint64, GameObject*> mapVerifyByFileId;
        SceneCookerInternal::makeObjectByFileId( verify, mapVerifyByFileId );

        uint32 cookedCount{ 0 };
        for ( size_t entityIndex = 0; entityIndex < inoutDoc._listSceneObjectNode.size(); ++entityIndex )
        {
            SceneDocument::SceneObjectNode&       entity      = inoutDoc._listSceneObjectNode[entityIndex];
            const SceneDocument::SceneObjectNode& cookedState = cooked._listSceneObjectNode[entityIndex];
            if ( cookedState._embeddedStateBytes.empty() )
            {
                if ( entity._embeddedXml.empty() == false )
                    SW_LOG_WARNING( "Entity '%#' could not be cooked to binary state - keeping XML.", entity._name );
                continue;
            }
            const auto        sourceIt = mapSourceByFileId.find( entity._fileId );
            const auto        verifyIt = mapVerifyByFileId.find( entity._fileId );
            const GameObject* pSource  = ( sourceIt != mapSourceByFileId.end() ) ? sourceIt->second : nullptr;
            const GameObject* pVerify  = ( verifyIt != mapVerifyByFileId.end() ) ? verifyIt->second : nullptr;
            if ( pSource == nullptr || pVerify == nullptr ||
                 SceneCookerInternal::makeStateText( source, pSource ) != SceneCookerInternal::makeStateText( verify, pVerify ) )
            {
                SW_LOG_WARNING( "Entity '%#' changes when its cooked state is read back - keeping XML.", entity._name );
                continue;
            }

            entity._embeddedStateBytes = cookedState._embeddedStateBytes;
            // 둘 다 실으면 파일만 커진다. 바이너리가 기준이 된 순간 XML 은 뺀다.
            entity._embeddedXml.clear();
            ++cookedCount;
        }

        verify.shutdown();
        source.shutdown();
        return cookedCount;
    }

    uint32 SceneCooker::cookAllScenes( string_view sourceRoot, string_view cookedDir, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR( "Scene cook needs an output directory (--cooked-dir)." );
            ++outFailedCount;
            return 0;
        }

        const string resourceRoot( sourceRoot );
        if ( resourceRoot.empty() )
        {
            SW_LOG_ERROR( "Scene cook could not resolve the resource root." );
            ++outFailedCount;
            return 0;
        }

        // 쿠킹하는 것은 씬을 짓는 일이다 — 모든 타입 공급자가 등록을 끝낸 뒤(기동 단계 `ModuleTypes`)라야 컴포넌트가 제 타입으로 지어진다.
        if ( engine::getTypeRegistry().areAllModuleTypesRegistered() == false )
        {
            SW_LOG_ERROR( "Scene cook ran before every module registered its types - nothing is cooked (cook after the ModuleTypes startup step)" );
            ++outFailedCount;
            return 0;
        }

        vector<string> listSceneFile;
        FileUtil::collectFiles( resourceRoot, ".xml", listSceneFile, true );

        const string normalizedRoot = FileUtil::normalizeSeparators( resourceRoot );
        const string normalizedOut  = FileUtil::normalizeSeparators( cookedDir );

        uint32 writtenCount{ 0 };
        for ( const string& scenePath : listSceneFile )
        {
            if ( StringUtil::endsWith( scenePath, ".scene.xml", true ) == false )
                continue;

            SceneDocument doc{};
            if ( doc.loadXml( scenePath ) == false )
            {
                SW_LOG_ERROR( "Scene cook failed to read '%#'.", scenePath );
                ++outFailedCount;
                continue;
            }

            // 쿠킹 전에 "상태가 있는 엔티티" 수를 세 둔다. 쿠킹하고 나면 XML 이 비워져 셀 수 없다.
            uint32 statefulCount{ 0 };
            for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
            {
                if ( entity._embeddedXml.empty() == false )
                    ++statefulCount;
            }

            uint32       missingComponentCount{ 0 };
            const uint32 cookedCount = cookEntityState( doc, &missingComponentCount );
            // 모르는 타입의 컴포넌트는 원문 그대로 쿠킹돼 배포본에서도 동작하지 않는다. 그 씬은 쓰지 않고 실패로 센다 — 빌드가 선다.
            if ( missingComponentCount > 0 )
            {
                SW_LOG_ERROR( "Scene cook: '%#' has %# components of unknown type - not cooked", scenePath, missingComponentCount );
                ++outFailedCount;
                continue;
            }
            // 이 비교는 **모든 빌드에서** 돌아야 한다. 아래 요약은 `SW_LOG_INFO` 라 Shipping 에서
            // 통째로 사라지는데, "쿠킹했다고 했지만 실은 XML 그대로" 는 그때도 알아야 할 일이다.
            if ( cookedCount < statefulCount )
            {
                SW_LOG_WARNING( "Scene '%#': %# of %# entities could not be cooked to binary state - they keep XML.",
                                scenePath, statefulCount - cookedCount, statefulCount );
            }

            // 출력은 `<cookedDir>/<Resource 기준 상대 경로>` 에 같은 이름으로, 확장자만 .bin 이다.
            const string normalizedScene = FileUtil::normalizeSeparators( scenePath );
            string       relativePath    = normalizedScene;
            if ( normalizedScene.size() > normalizedRoot.size() && normalizedScene.compare( 0, normalizedRoot.size(), normalizedRoot ) == 0 )
            {
                relativePath = normalizedScene.substr( normalizedRoot.size() );
                while ( relativePath.empty() == false && relativePath.front() == '/' )
                    relativePath.erase( relativePath.begin() );
            }

            string outputPath = normalizedOut;
            if ( outputPath.empty() == false && outputPath.back() != '/' )
                outputPath += '/';
            outputPath = AssetCookPath::toCookedPath( outputPath + relativePath );

            if ( doc.saveBinary( outputPath ) == false )
            {
                SW_LOG_ERROR( "Scene cook failed to write '%#'.", outputPath );
                ++outFailedCount;
                continue;
            }

            SW_LOG_INFO( "Cooked '%#' -> '%#' (%# entities, %# as binary state)", scenePath, outputPath,
                         static_cast<uint32>( doc._listSceneObjectNode.size() ), cookedCount );
            ++writtenCount;
        }

        return writtenCount;
    }

} // namespace sw
