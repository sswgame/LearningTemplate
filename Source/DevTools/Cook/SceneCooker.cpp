#include "pch.h"

#include "DevTools/Cook/SceneCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Cook/CookStore.h"
#include "DevTools/Cook/SceneHLODMeshCooker.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCellSplitter.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/ScenePreloadList.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SceneCookerInternal
        {
            /** @brief 오브젝트의 상태를 파일 id 로 적은 XML 입니다. 쿠킹 전 · 쿠킹된 뒤의 상태를 비교하는 기준입니다. */
            [[nodiscard]] static expected<string> makeStateText( const Scene& scene, const GameObject* pObject )
            {
                ObjectSavedIDMap mapSavedID;
                scene.collectSavedIDMap( mapSavedID );
                ObjectSaveOptions options{ SerializePurpose::Authoring };
                options._pSavedIDMap = &mapSavedID;
                return ObjectStateSerializer::saveToXMLString( pObject, options );
            }

            /**
             * @brief 씬에서 모르는 타입으로 지어진 컴포넌트(`MissingComponent`)를 오브젝트 · 원래 타입 이름과 함께 오류로 알리고 그 수를 돌려줍니다.
             * @details 그 컴포넌트는 원문 그대로 쿠킹돼 배포본에서도 `MissingComponent` 로 읽힌다 — 동작하지 않는 컴포넌트가 실린다.
             */
            static uint32 reportMissingComponents( SceneIterationKey key, const Scene& scene )
            {
                const GameObjectManager* pManager = scene.getObjectManager();
                if ( pManager == nullptr )
                    return 0;
                uint32              missingCount{ 0 };
                vector<GameObject*> listObject;
                pManager->getAllGameObjects( key, listObject );
                for ( const GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr )
                        continue;
                    for ( const Component* pComp : pObject->getComponents() )
                    {
                        if ( pComp == nullptr || pComp->getTypeInfo() != MissingComponent::StaticType() )
                            continue;
                        SW_LOG_ERROR_ID( "SceneCooker.SceneCookComponentType", "Scene cook: '%#' has a component of type '%#' that no loaded module registers", pObject->getName().c_str(),
                                         static_cast<const MissingComponent*>( pComp )->getOriginalTypeName().c_str() );
                        ++missingCount;
                    }
                }
                return missingCount;
            }

            /**
             * @brief @p relativePath(리소스 기준)가 에디터만 여는 씬이면 true 입니다 — 에디터 팩(`editor/`)과 에디터 시나리오 픽스처(`…/automation/editor/`).
             * @details 에디터는 Dev 에만 있고 Dev 런타임은 쿠킹 폴더를 마운트하지 않으므로 쓰일 곳이 없다(언리얼 `DirectoriesToNeverCook`).
             *          에디터 시나리오 픽스처에는 일부러 틀린 씬(`mapcheck_expect_error`)이 있어 쿠킹하면 검증 경고가 빌드 로그에 남는다.
             */
            static bool isEditorOnlyScene( string_view relativePath )
            {
                return StringUtil::startsWith( relativePath, "editor/", true ) || relativePath.find( "/automation/editor/" ) != string_view::npos;
            }

            /**
             * @brief 문서의 엔티티 상태에 지금 등록되지 않은 컴포넌트 타입이 있으면 true 입니다(짓지 않고 원소 이름만 본다).
             * @details 다른 게임의 팩은 그 게임 모듈의 컴포넌트를 쓴다 — 이 빌드에는 없는 모듈이다. 그런 씬을 지으면 모르는 타입마다 오류가 난다.
             */
            static bool usesUnregisteredComponentTypes( const TypeRegistry& registry, const SceneDocument& doc )
            {
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    XMLDocument xml;
                    if ( entity._embeddedXML.empty() || xml.parse( entity._embeddedXML ) == false )
                        continue;
                    const XMLNode listComponent = xml.getRoot().findChild( "_listComponent" );
                    for ( XMLNode component = listComponent.findChild(); component.isValid(); component = component.findNextSibling() )
                    {
                        if ( registry.findType( hashed_string( component.getName() ) ) == nullptr )
                            return true;
                    }
                }
                return false;
            }

            /** @brief @p layerSet 이 @p targetLayers 를 모두 품으면 true 입니다(둘 다 정렬돼 있다). */
            static bool containsLayers( const vector<string>& layerSet, const vector<string>& targetLayers )
            {
                for ( const string& layer : targetLayers )
                {
                    bool bFound = false;
                    for ( const string& owned : layerSet )
                    {
                        bFound = bFound || owned == layer;
                    }
                    if ( bFound == false )
                        return false;
                }
                return true;
            }

            /**
             * @brief 데이터 레이어를 넘는 참조를 찾습니다. 대상의 레이어가 가리키는 쪽의 레이어에 모두 들어 있어야 한다 — 그래야 가리키는 쪽이 오를 때 대상이 늘 올라 있다. 셀을 넘는 참조와 같은 규칙이다.
             * @return 위반이 있으면 엔티티마다 오류를 남기고 false 입니다. 레이어를 쓰지 않는 씬은 짓지 않고 true 입니다.
             */
            [[nodiscard]] static bool validateDataLayerReferences( const SceneEngineSubsystems& services, const SceneDocument& doc, string_view scenePath )
            {
                bool                                                         bHasLayered = false;
                unordered_map<uint64, const SceneDocument::SceneObjectNode*> mapNodeByFileID;
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    bHasLayered = bHasLayered || entity._listDataLayer.empty() == false;
                    mapNodeByFileID.emplace( entity._fileID, &entity );
                }
                if ( bHasLayered == false )
                    return true;

                Scene placement{ "SceneCooker.DataLayers", services };
                placement.setBuildAllDataLayers();
                if ( placement.instantiate( doc ) == false )
                    return false;
                unordered_map<uint64, GameObject*> mapObjectByFileID;
                placement.collectObjectsByFileID( mapObjectByFileID );
                ObjectSavedIDMap mapSavedID;
                placement.collectSavedIDMap( mapSavedID );

                bool           bValid = true;
                vector<uint64> listReferencedID;
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    const auto objectIt = mapObjectByFileID.find( entity._fileID );
                    if ( objectIt == mapObjectByFileID.end() )
                        continue;
                    listReferencedID.clear();
                    ObjectSaveOptions options{ SerializePurpose::Authoring };
                    options._pSavedIDMap          = &mapSavedID;
                    options._pOutListReferencedID = &listReferencedID;
                    vector<uint8> stateBytes;
                    (void)ObjectStateSerializer::saveToBinaryBuffer( objectIt->second, stateBytes, options ); // 참조만 모은다 — 바이트는 버린다
                    for ( const uint64 referencedObjectID : listReferencedID )
                    {
                        const auto targetIDIt = mapSavedID.find( referencedObjectID );
                        const auto targetIt   = targetIDIt != mapSavedID.end() ? mapNodeByFileID.find( targetIDIt->second ) : mapNodeByFileID.end();
                        if ( targetIt == mapNodeByFileID.end() || containsLayers( entity._listDataLayer, targetIt->second->_listDataLayer ) )
                            continue;
                        SW_LOG_ERROR_ID( "SceneCooker.DataLayerCrossReference", "Scene cook: '%#' - entity '%#' refers to '%#' on a data layer it is not on; put the referrer on that layer too or move the target", scenePath, entity._name, targetIt->second->_name );
                        bValid = false;
                    }
                }
                return bValid;
            }

            /** @brief 씬 하나를 쿠킹해 @p outputPath 에 씁니다. 작업 범위 안에서 부릅니다 — 씬 · 프리팹 · 그 밖의 읽기가 모두 입력이 된다. */
            static CookStep cookScene( const SceneEngineSubsystems& services, const TypeRegistry& registry, const AbsolutePath& scenePath, string_view relativeScene,
                                       string_view activePackRoot, const AbsolutePath& outputPath )
            {
                SceneDocument doc{};
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookFailedRead", "Scene cook failed to read '%#'.", scenePath.str() );
                    return CookStep::Failed;
                }
                // 다른 게임의 씬이 그 게임 모듈의 컴포넌트를 쓰면 건너뛴다 — 이 빌드에 없는 모듈이고, 배포본은 활성 게임의 팩만 연다.
                // 엔진 · 공용 타입만 쓰는 씬은 그대로 쿠킹한다(활성 팩의 모르는 타입은 아래에서 실패로 센다).
                if ( AssetCookPath::isInInactiveGamePack( relativeScene, activePackRoot ) && usesUnregisteredComponentTypes( registry, doc ) )
                {
                    SW_LOG_INFO_ID( "SceneCooker.SceneCookUsesComponents", "Scene cook: '%#' uses components of another game's module - skipped", relativeScene );
                    return CookStep::Skipped;
                }

                // Packed Level: 묶는 씬을 넣은 앵커의 정적 메시를 인스턴스 메시로 바꾼다(에디터는 원본을 편집한다). 바뀐 엔티티도 아래에서 바이너리 상태로 쿠킹된다.
                if ( SceneCooker::packStaticMeshes( services, doc, scenePath.str() ) == false )
                    return CookStep::Failed;
                // 쿠킹 전에 "상태가 있는 엔티티" 수를 세 둔다. 쿠킹하고 나면 XML 이 비워져 셀 수 없다.
                uint32 statefulCount{ 0 };
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    if ( entity._embeddedXML.empty() == false )
                        ++statefulCount;
                }

                uint32       missingComponentCount{ 0 };
                const uint32 cookedCount = SceneCooker::cookEntityState( services, doc, &missingComponentCount );
                // 모르는 타입의 컴포넌트는 원문 그대로 쿠킹돼 배포본에서도 동작하지 않는다. 그 씬은 쓰지 않고 실패로 센다 — 빌드가 선다.
                if ( missingComponentCount > 0 )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookComponentsUnknown", "Scene cook: '%#' has %# components of unknown type - not cooked", scenePath.str(), missingComponentCount );
                    return CookStep::Failed;
                }
                // 이 비교는 **모든 빌드에서** 돌아야 한다. 아래 요약은 `SW_LOG_INFO` 라 Shipping 에서
                // 통째로 사라지는데, "쿠킹했다고 했지만 실은 XML 그대로" 는 그때도 알아야 할 일이다.
                if ( cookedCount < statefulCount )
                {
                    SW_LOG_WARNING_ID( "SceneCooker.SceneEntitiesCouldNot", "Scene '%#': %# of %# entities could not be cooked to binary state - they keep XML.", scenePath.str(), statefulCount - cookedCount,
                                       statefulCount );
                }
                // 레이어를 넘는 참조도 쿠킹 실패다 — 레이어를 끈 채 오른 쪽의 참조가 조용히 끊긴다.
                if ( validateDataLayerReferences( services, doc, scenePath.str() ) == false )
                    return CookStep::Failed;
                // 스트리밍하는 씬은 엔티티를 셀로 나눈다. 셀을 넘는 참조가 있으면 쿠킹 실패다 — 런타임에 조용히 끊기는 참조를 싣지 않는다.
                if ( SceneCellSplitter::split( services, doc, scenePath.str() ) == false )
                    return CookStep::Failed;
                // 합친 HLOD 대리는 옆에 굽는 메시(`cookHLODMesh`)의 구간을 그린다 — 실행 때 합치지 않는다.
                if ( SceneHLODMeshCooker::replaceMergedProxies( doc, SceneHLODMeshCooker::makeMeshPath( AssetCookPath::toCookedPath( relativeScene ) ) ).has_value() == false )
                    return CookStep::Failed;
                // 쿠킹본은 펼친 결과만 싣는다 — 런타임의 짓는 길이 하나다.
                doc.stripAuthoringInstanceData();
                if ( doc.saveBinary( outputPath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookFailedWrite", "Scene cook failed to write '%#'.", outputPath.str() );
                    return CookStep::Failed;
                }
                SW_LOG_INFO_ID( "SceneCooker.CookedEntitiesBinaryState", "Cooked '%#' -> '%#' (%# entities, %# as binary state)", scenePath.str(), outputPath.str(), static_cast<uint32>( doc.computeTotalEntityCount() ),
                                cookedCount );
                return CookStep::Written;
            }

            /**
             * @brief 씬 하나의 합친 HLOD 대리를 메시 하나로 구워 @p outputPath 에 씁니다(`SceneHLODMeshCooker`). 합칠 대리가 없는 씬은 건너뜁니다.
             * @details 대리를 만드는 길은 씬 쿠킹과 같다(묶기 → 셀 나누기) — 입력은 씬이 지어지며 읽은 것과 부품 메시다. 부품 메시를 고치면 이 파일만 다시 굽는다.
             */
            static CookStep cookHLODMesh( const SceneEngineSubsystems& services, const AbsolutePath& scenePath, const AbsolutePath& outputPath )
            {
                // 합치기를 켜지 않은 씬은 세우지 않는다 — 글에 설정 칸이 없으면 넘긴다. 작업 안에서 읽어 씬이 이 작업의 입력이 된다.
                string text;
                if ( AssetIO::readTextFile( scenePath, text ) == false || text.find( SceneHLODMeshCooker::kSettingMarker.data() ) == string::npos )
                    return CookStep::Skipped;
                SceneDocument doc{};
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneHLODMeshCookFailedRead", "Scene HLOD mesh cook failed to read '%#'.", scenePath.str() );
                    return CookStep::Failed;
                }
                MeshAssetData data{};
                if ( SceneCooker::packStaticMeshes( services, doc, scenePath.str() ) == false || SceneCellSplitter::split( services, doc, scenePath.str() ) == false ||
                     SceneHLODMeshCooker::buildMesh( services, doc, scenePath.str(), data ) == false )
                    return CookStep::Failed;
                if ( data._listSection.empty() )
                    return CookStep::Skipped;
                if ( MeshAssetFormat::saveToFile( outputPath, data ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneHLODMeshCookFailedWrite", "Scene HLOD mesh cook failed to write '%#'.", outputPath.str() );
                    return CookStep::Failed;
                }
                SW_LOG_INFO_ID( "SceneCooker.CookedHLODMesh", "Cooked HLOD mesh '%#' (%# merged proxies, %# triangles)", outputPath.str(), static_cast<uint32>( data._listSection.size() ),
                                static_cast<uint32>( data._listIndex.size() / 3 ) );
                return CookStep::Written;
            }

            /**
             * @brief 씬 하나의 프리로드 목록(`ScenePreloadList`)을 만들어 @p outputPath 에 씁니다. 씬을 런타임과 같은 길로 지어 컴포넌트의 에셋 경로 프로퍼티를 모은다.
             * @details 씬 쿠킹과 따로 도는 작업이다(쿠킹 기록은 작업 하나에 출력 하나). 입력은 씬이 지어지며 읽은 모든 것이라, 프리팹을 고치면 목록도 다시 구워진다.
             */
            static CookStep cookPreloadList( SceneIterationKey key, const SceneEngineSubsystems& services, const AbsolutePath& scenePath, const AbsolutePath& outputPath )
            {
                SceneDocument doc{};
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookFailed", "Scene preload cook failed to read '%#'.", scenePath.str() );
                    return CookStep::Failed;
                }
                Scene source{ "ScenePreload.Source", services };
                source.setBuildAllDataLayers(); // 끈 레이어의 에셋도 쿠킹본이 켜면 쓴다
                if ( source.instantiate( doc ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookCould", "Scene preload cook could not build '%#'.", scenePath.str() );
                    return CookStep::Failed;
                }
                vector<string> listPath;
                ScenePreloadList::collect( key, *source.getObjectManager(), listPath );
                if ( ScenePreloadList::write( outputPath, listPath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookFailed2", "Scene preload cook failed to write '%#'.", outputPath.str() );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 SceneCooker::cookEntityState( const SceneEngineSubsystems& services, SceneDocument& inoutDoc, uint32* pOutMissingComponentCount )
    {
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = 0;
        if ( services._pTypeRegistry == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookNotCooked", "Scene cook: '%#' is not cooked - the engine services have no type registry", inoutDoc._name );
            return 0;
        }
        // **런타임이 읽는 그대로 짓고 쿠킹한다.** 엔티티를 하나씩 따로 읽으면 부모가 문서에서 뒤에 있는 자식이 부모를 찾지 못하고 저장이
        // 그 연결을 지운다(배포본에서 자식이 루트가 된다). 그래서 문서 전체를 `Scene::instantiate`(프리팹 스폰 · 묶음 부착까지 런타임과
        // 같은 길)로 짓고, 쿠킹된 문서를 다시 지어 엔티티마다 상태 전체를 비교한다(컴포넌트 타입 목록만 보면 값이 어긋나도 통과한다).
        // 엔티티는 파일 id 로 찾는다 — 쿠킹된 상태의 부착도 파일 id 로만 부모를 가리킨다. id 없는 엔티티는 읽는 쪽이 받지 않는 문서다.
        for ( const SceneDocument::SceneObjectNode& entity : inoutDoc._listSceneObjectNode )
        {
            if ( entity._fileID == 0 )
            {
                SW_LOG_ERROR_ID( "SceneCooker.SceneCookEntityNo", "Scene cook: entity '%#' of '%#' has no id - nothing is cooked", entity._name, inoutDoc._name );
                return 0;
            }
        }

        // 쿠킹 전용 씬이다. 실제 씬 매니저의 것을 쓰면 쿠킹하는 동안 만든 임시 오브젝트가 실제 씬에 남는다.
        Scene source{ "SceneCooker.Source", services };
        source.setBuildAllDataLayers(); // 끈 레이어의 엔티티도 굽는다
        if ( source.instantiate( inoutDoc ) == false )
            return 0;
        const uint32 missingComponentCount = SceneCookerInternal::reportMissingComponents( SceneIterationKey{}, source );
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = missingComponentCount;
        unordered_map<uint64, GameObject*> mapSourceByFileID;
        source.collectObjectsByFileID( mapSourceByFileID );

        ObjectSavedIDMap mapSourceSavedID;
        source.collectSavedIDMap( mapSourceSavedID );
        ObjectSaveOptions saveOptions{ SerializePurpose::Authoring };
        saveOptions._pSavedIDMap   = &mapSourceSavedID;
        saveOptions._bSchemaLayout = SW_TRUE; // 쿠킹 키가 리플렉션 지문을 들어(`makeCookJobDesc`) 배치가 바뀌면 다시 굽는다

        SceneDocument cooked = inoutDoc;
        for ( SceneDocument::SceneObjectNode& entity : cooked._listSceneObjectNode )
        {
            if ( entity._embeddedXML.empty() )
                continue;
            const auto  sourceIt = mapSourceByFileID.find( entity._fileID );
            GameObject* pSource  = ( sourceIt != mapSourceByFileID.end() ) ? sourceIt->second : nullptr;
            if ( pSource == nullptr )
                continue;
            vector<uint8> stateBytes;
            if ( ObjectStateSerializer::saveToBinaryBuffer( pSource, stateBytes, saveOptions ) && stateBytes.empty() == false )
            {
                entity._embeddedStateBytes = std::move( stateBytes );
                entity._embeddedXML.clear();
            }
        }

        // **쿠킹된 것을 다시 지어 본다.** 모르는 컴포넌트 타입 · 값이 바뀌는 필드 · 부모를 잃는 부착이 있으면 그 엔티티의 상태가 어긋나고, 그
        // 엔티티는 XML 로 남는다. 쿠킹이 조용히 무언가를 떨어뜨리지 않는다.
        Scene verify{ "SceneCooker.Verify", services };
        verify.setBuildAllDataLayers();
        if ( verify.instantiate( cooked ) == false )
            return 0;
        unordered_map<uint64, GameObject*> mapVerifyByFileID;
        verify.collectObjectsByFileID( mapVerifyByFileID );

        uint32 cookedCount{ 0 };
        for ( size_t entityIndex = 0; entityIndex < inoutDoc._listSceneObjectNode.size(); ++entityIndex )
        {
            SceneDocument::SceneObjectNode&       entity      = inoutDoc._listSceneObjectNode[entityIndex];
            const SceneDocument::SceneObjectNode& cookedState = cooked._listSceneObjectNode[entityIndex];
            if ( cookedState._embeddedStateBytes.empty() )
            {
                if ( entity._embeddedXML.empty() == false )
                    SW_LOG_WARNING_ID( "SceneCooker.EntityCouldNotCooked", "Entity '%#' could not be cooked to binary state - keeping XML.", entity._name );
                continue;
            }
            const auto        sourceIt = mapSourceByFileID.find( entity._fileID );
            const auto        verifyIt = mapVerifyByFileID.find( entity._fileID );
            const GameObject* pSource  = ( sourceIt != mapSourceByFileID.end() ) ? sourceIt->second : nullptr;
            const GameObject* pVerify  = ( verifyIt != mapVerifyByFileID.end() ) ? verifyIt->second : nullptr;
            // 어느 쪽이든 글로 쓰지 못하면 같다고 볼 수 없다 — XML 을 둔다.
            const expected<string> sourceState    = pSource != nullptr ? SceneCookerInternal::makeStateText( source, pSource ) : expected<string>{ unexpected( Error{ ErrorKind::NotFound, "no source entity" } ) };
            const expected<string> verifyState    = pVerify != nullptr ? SceneCookerInternal::makeStateText( verify, pVerify ) : expected<string>{ unexpected( Error{ ErrorKind::NotFound, "no cooked entity" } ) };
            const bool             bStateMismatch = sourceState.has_value() == false || verifyState.has_value() == false || *sourceState != *verifyState;
            if ( bStateMismatch )
            {
                SW_LOG_WARNING_ID( "SceneCooker.EntityChangesWhenCooked", "Entity '%#' changes when its cooked state is read back - keeping XML.", entity._name );
                continue;
            }

            entity._embeddedStateBytes = cookedState._embeddedStateBytes;
            // 둘 다 실으면 파일만 커진다. 바이너리가 기준이 된 순간 XML 은 뺀다. 인스턴스 덮어쓰기도 이미 얹힌 값이다.
            entity._embeddedXML.clear();
            entity._listInstanceOverrideXML.clear();
            ++cookedCount;
        }

        return cookedCount;
    }

    uint32 SceneCooker::cookAllScenes( const SceneEngineSubsystems& services, const AbsolutePath& sourceRoot, const AbsolutePath& cookedDir, const CookStore& store,
                                       const uint64 reflectionFingerprint, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookNeedsOutput", "Scene cook needs an output directory (--cooked-dir)." );
            ++outFailedCount;
            return 0;
        }

        const AbsolutePath resourceRoot( sourceRoot );
        if ( resourceRoot.empty() )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookCouldNot2", "Scene cook could not resolve the content root." );
            ++outFailedCount;
            return 0;
        }

        // 쿠킹하는 것은 씬을 짓는 일이다 — 모든 타입 공급자가 등록을 끝낸 뒤(기동 단계 `ModuleLoadStage`)라야 컴포넌트가 제 타입으로 지어진다.
        if ( services._pTypeRegistry == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookNoType", "Scene cook has no type registry in the engine services - nothing is cooked" );
            ++outFailedCount;
            return 0;
        }
        const TypeRegistry& registry = *services._pTypeRegistry;
        if ( registry.areAllModuleTypesRegistered() == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookRanBefore", "Scene cook ran before every module registered its types - nothing is cooked (cook after the ModuleLoadStage startup step)" );
            ++outFailedCount;
            return 0;
        }

        vector<AbsolutePath> listSceneFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listSceneFile ) == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookCouldNot3", "Scene cook could not list '%#' - nothing is cooked", resourceRoot.str() );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listSceneFile );

        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot.str() ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ).str() );
        const CookJobDesc desc           = makeCookJobDesc( reflectionFingerprint );
        const CookJobDesc preloadDesc    = makePreloadCookJobDesc( reflectionFingerprint );
        const CookJobDesc meshDesc       = makeHLODMeshCookJobDesc( reflectionFingerprint );

        // 다시 구운 씬의 시간 합과 가장 느린 씬(요약 로그). 씬은 차례로 굽는다 — 에셋 캐시와 결과가 프로세스 하나의 것이라, 나눈다면 언리얼 멀티프로세스 쿡처럼 프로세스를 가른다.
        CookStats stats;
        int64     cookedNanoseconds  = 0;
        int64     slowestNanoseconds = 0;
        string    slowestScene;
        for ( const AbsolutePath& scenePath : listSceneFile )
        {
            if ( StringUtil::endsWith( scenePath.str(), ".scene.xml", true ) == false )
                continue;
            const string relativeScene = FileUtil::normalizeSeparators( scenePath.str() ).substr( MathUtil::min( normalizedRoot.size() + 1, scenePath.str().size() ) );
            if ( SceneCookerInternal::isEditorOnlyScene( relativeScene ) )
                continue;

            // 출력은 `<cookedDir>/<Resource 기준 상대 경로>` 에 같은 이름으로, 확장자만 .bin 이다.
            const string       outputID   = AssetCookPath::toCookedPath( relativeScene );
            const AbsolutePath outputPath = cookedDir.join( outputID );

            const Stopwatch  sceneStopwatch;
            const CookResult result = store.cookFile( desc, outputID, outputPath, [&]()
            { return SceneCookerInternal::cookScene( services, registry, scenePath, relativeScene, activePackRoot, outputPath ); } );
            outFailedCount += result == CookResult::Failed ? 1u : 0u;
            stats.add( result );
            const int64 sceneNanoseconds = sceneStopwatch.getElapsedNanoseconds();
            if ( result == CookResult::Cooked )
                cookedNanoseconds += sceneNanoseconds;
            if ( result == CookResult::Cooked && sceneNanoseconds > slowestNanoseconds )
            {
                slowestNanoseconds = sceneNanoseconds;
                slowestScene       = relativeScene;
            }

            // 쿠킹한 씬마다 프리로드 목록을 옆에 굽는다(레벨 전환이 `PreloadSet` 으로 연다). 씬 쿠킹이 실패했거나 건너뛴 씬은 목록도 없다.
            const bool bSceneWritten = result == CookResult::Cooked || result == CookResult::Reused;
            if ( bSceneWritten == false )
                continue;
            const string       preloadID     = ScenePreloadList::makeListPath( outputID );
            const AbsolutePath preloadPath   = cookedDir.join( preloadID );
            const CookResult   preloadResult = store.cookFile( preloadDesc, preloadID, preloadPath, [&]()
              { return SceneCookerInternal::cookPreloadList( SceneIterationKey{}, services, scenePath, preloadPath ); } );
            outFailedCount += preloadResult == CookResult::Failed ? 1u : 0u;

            const string     meshID     = SceneHLODMeshCooker::makeMeshPath( outputID );
            const CookResult meshResult = store.cookFile( meshDesc, meshID, cookedDir.join( meshID ), [&]()
            { return SceneCookerInternal::cookHLODMesh( services, scenePath, cookedDir.join( meshID ) ); } );
            outFailedCount += meshResult == CookResult::Failed ? 1u : 0u;
        }

        SW_LOG_INFO_ID( "SceneCooker.SceneCookReusedDDC", "Scene cook: %# reused from the DDC, %# cooked", stats._reusedCount, stats._cookedCount );
        SW_LOG_INFO_ID( "SceneCooker.SceneCookTime", "Scene cook time: %# ms over %# cooked scenes, slowest %# ms ('%#')", cookedNanoseconds / constant::time::kNanosecondsPerMillisecond,
                        stats._cookedCount, slowestNanoseconds / constant::time::kNanosecondsPerMillisecond, slowestScene );
        return stats.getWrittenCount();
    }

    bool SceneCooker::packStaticMeshes( const SceneEngineSubsystems& services, SceneDocument& inoutDoc, string_view scenePath )
    {
        // 묶을 주인: 묶는 씬을 넣은 앵커(파일 id)와, 문서 자신이 묶는 씬이면 맨 위(0).
        unordered_set<uint64> uniquePackedAnchor;
        for ( const SceneDocument::SceneObjectNode& node : inoutDoc._listSceneObjectNode )
        {
            if ( node._instancePacking == SceneInstancePacking::StaticMeshes )
                uniquePackedAnchor.insert( node._fileID );
        }
        const bool bPackTop = inoutDoc._sceneSettings.usesStaticMeshPacking();
        if ( bPackTop == false && uniquePackedAnchor.empty() )
            return true;

        // 층: 메시 형식은 위 층(Visual)이라 타입 표로 찾는다. 이름이 바뀌면 여기서 멈춘다(묶지 않고 넘어가지 않는다).
        static constexpr const utf8* kMeshTypeName      = "MeshComponent";
        static constexpr const utf8* kInstancedTypeName = "InstancedMeshComponent";
        static constexpr const utf8* kTransformTypeName = "InstancedMeshTransform";
        const TypeInfo* const        pMeshType          = services._pTypeRegistry != nullptr ? services._pTypeRegistry->findType( hashed_string( kMeshTypeName ) ) : nullptr;
        const TypeInfo* const        pInstancedType     = services._pTypeRegistry != nullptr ? services._pTypeRegistry->findType( hashed_string( kInstancedTypeName ) ) : nullptr;
        if ( pMeshType == nullptr || pInstancedType == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneCooker.PackTypesMissing", "Scene cook: '%#' packs static meshes but '%#' or '%#' is not in the type table", scenePath, kMeshTypeName, kInstancedTypeName );
            return false;
        }
        // 읽는 칸의 차례: 메시 · 머티리얼 · 구간 · 정적 · 사용자 값 둘 · 로컬 위치 · 회전 · 스케일.
        constexpr uint32             kMesh                      = 0;
        constexpr uint32             kMaterial                  = 1;
        constexpr uint32             kSection                   = 2;
        constexpr uint32             kStatic                    = 3;
        constexpr uint32             kCustomData0               = 4;
        constexpr uint32             kCustomData1               = 5;
        constexpr uint32             kPosition                  = 6;
        constexpr uint32             kRotation                  = 7;
        constexpr uint32             kScale                     = 8;
        constexpr uint32             kMeshKeyCount              = 9;
        static constexpr const utf8* kArrMeshKey[kMeshKeyCount] = { "_meshID", "_materialPath", "_sectionName", "_bStatic", "_customData0", "_customData1", "_localPosition", "_localRotation", "_localScale" };
        const PropertyInfo*          arrMeshProp[kMeshKeyCount] = {};
        bool                         bPropertiesKnown           = true;
        for ( uint32 keyIndex = 0; keyIndex < kMeshKeyCount; ++keyIndex )
        {
            arrMeshProp[keyIndex] = pMeshType->findPropertyInHierarchy( hashed_string( kArrMeshKey[keyIndex] ) );
            bPropertiesKnown      = bPropertiesKnown && arrMeshProp[keyIndex] != nullptr;
        }
        if ( bPropertiesKnown == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.PackPropertiesMissing", "Scene cook: '%#' packs static meshes but '%#' lacks a property the packer reads", scenePath, kMeshTypeName );
            return false;
        }

        Scene packing{ "SceneCooker.Pack", services };
        packing.setBuildAllDataLayers();
        if ( packing.instantiate( inoutDoc ) == false || packing.getUnresolvedEntityCount() > 0 )
        {
            SW_LOG_ERROR_ID( "SceneCooker.PackCouldNotBuild", "Scene cook: '%#' could not be built to pack its static meshes", scenePath );
            return false;
        }
        unordered_map<uint64, GameObject*> mapObjectByFileID;
        packing.collectObjectsByFileID( mapObjectByFileID );
        ObjectSavedIDMap mapSavedID;
        packing.collectSavedIDMap( mapSavedID );

        // 가리켜지는 엔티티는 묶지 않는다 — 묶으면 그 오브젝트가 사라져 참조가 빈다(부착된 자식도 부모를 가리킨다).
        unordered_set<uint64> uniqueReferenced;
        vector<uint64>        listReferencedID;
        for ( const SceneDocument::SceneObjectNode& node : inoutDoc._listSceneObjectNode )
        {
            const auto objectIt = mapObjectByFileID.find( node._fileID );
            if ( objectIt == mapObjectByFileID.end() )
                continue;
            listReferencedID.clear();
            ObjectSaveOptions options{ SerializePurpose::Authoring };
            options._pSavedIDMap          = &mapSavedID;
            options._pOutListReferencedID = &listReferencedID;
            vector<uint8> stateBytes;
            (void)ObjectStateSerializer::saveToBinaryBuffer( objectIt->second, stateBytes, options ); // 참조만 모은다
            for ( const uint64 referencedObjectID : listReferencedID )
            {
                const auto targetIt = mapSavedID.find( referencedObjectID );
                if ( targetIt != mapSavedID.end() && targetIt->second != node._fileID )
                    uniqueReferenced.insert( targetIt->second );
            }
        }

        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        const float4            zeroData{};
        const string            zeroDataText = SerializerUtil::formatValueText( ValueAddress{ &zeroData }, arrMeshProp[kCustomData0]->_typeName, context );
        struct PackGroup
        {
            uint64         _ownerFileID{ 0 };
            vector<string> _listDataLayer;
            string         _meshText;
            string         _materialText;
            string         _sectionText;
            vector<string> _listTransformText; ///< 인스턴스마다 위치 · 회전 · 스케일 글 셋
        };
        vector<PackGroup>     listGroup;
        unordered_set<uint64> uniquePackedFileID;
        string                arrText[kMeshKeyCount];
        for ( const SceneDocument::SceneObjectNode& node : inoutDoc._listSceneObjectNode )
        {
            const bool bAnchorMember = node._instanceAnchorFileID != 0 && uniquePackedAnchor.count( node._instanceAnchorFileID ) != 0;
            const bool bTopEntity    = bPackTop && node._instanceAnchorFileID == 0;
            const auto objectIt      = mapObjectByFileID.find( node._fileID );
            if ( ( bAnchorMember == false && bTopEntity == false ) || node._sceneInstance.empty() == false || uniqueReferenced.count( node._fileID ) != 0 || objectIt == mapObjectByFileID.end() )
                continue;
            const GameObject* pObject  = objectIt->second;
            const auto        ownerIt  = mapObjectByFileID.find( node._instanceAnchorFileID );
            const GameObject* pOwner   = bAnchorMember && ownerIt != mapObjectByFileID.end() ? ownerIt->second : nullptr;
            const bool        bOneMesh = pObject->getComponents().size() == 1 && pObject->getComponents()[0] != nullptr && pObject->getComponents()[0]->getTypeInfo() == pMeshType;
            if ( bOneMesh == false || pObject->getParent() != pOwner )
                continue;
            const Component* pMesh = pObject->getComponents()[0];
            for ( uint32 keyIndex = 0; keyIndex < kMeshKeyCount; ++keyIndex )
            {
                arrText[keyIndex] = SerializerUtil::formatPropertyText( *arrMeshProp[keyIndex], ObjectAddress{ pMesh }, context );
            }
            // 묶은 인스턴스는 정적 그리기만 싣는다 — 움직이는 메시 · 사용자 값(인스턴스마다 다른 셰이더 값)은 그대로 둔다.
            if ( arrText[kStatic] != "true" || arrText[kCustomData0] != zeroDataText || arrText[kCustomData1] != zeroDataText )
                continue;
            const uint64 ownerFileID = bAnchorMember ? node._instanceAnchorFileID : 0;
            PackGroup*   pGroup      = nullptr;
            for ( PackGroup& group : listGroup )
            {
                const bool bSame = group._ownerFileID == ownerFileID && group._listDataLayer == node._listDataLayer && group._meshText == arrText[kMesh] && group._materialText == arrText[kMaterial] &&
                                   group._sectionText == arrText[kSection];
                pGroup = pGroup == nullptr && bSame ? &group : pGroup;
            }
            if ( pGroup == nullptr )
                pGroup = &listGroup.emplace_back( PackGroup{ ownerFileID, node._listDataLayer, arrText[kMesh], arrText[kMaterial], arrText[kSection], {} } );
            pGroup->_listTransformText.push_back( arrText[kPosition] );
            pGroup->_listTransformText.push_back( arrText[kRotation] );
            pGroup->_listTransformText.push_back( arrText[kScale] );
            uniquePackedFileID.insert( node._fileID );
        }
        if ( listGroup.empty() )
            return true;

        // 묶은 엔티티 하나 — 인스턴스 메시 컴포넌트 하나를 가진 오브젝트. 앵커 멤버면 앵커 범위의 넓은 id(내부 파일 id 와 겹치지 않는 높은 자리)로 앵커에 붙는다.
        constexpr uint64                       kPackedInnerIDBase = uint64{ 1 } << 62;
        uint64                                 nextTopFileID      = SceneDocument::computeMaxFileID( inoutDoc._listSceneObjectNode ) + 1;
        vector<SceneDocument::SceneObjectNode> listPacked;
        for ( size_t groupIndex = 0; groupIndex < listGroup.size(); ++groupIndex )
        {
            const PackGroup& group = listGroup[groupIndex];
            string_view      anchorName;
            bool             bHasAnchor = false;
            for ( const SceneDocument::SceneObjectNode& anchor : inoutDoc._listSceneObjectNode )
            {
                if ( group._ownerFileID != 0 && anchor._fileID == group._ownerFileID )
                {
                    anchorName = anchor._name;
                    bHasAnchor = true;
                }
            }
            StringBuilder<constant::buffer::kMax256> nodeName;
            if ( bHasAnchor )
                nodeName.appendFormat( "%#/", anchorName );
            nodeName.appendFormat( "Packed%#", static_cast<uint64>( groupIndex ) );
            SceneDocument::SceneObjectNode node{};
            node._name                 = string( nodeName.view() );
            node._fileID               = group._ownerFileID != 0 ? ( SceneDocument::computeInstanceIDScope( group._ownerFileID ) ^ ( kPackedInnerIDBase + groupIndex ) ) : nextTopFileID++;
            node._instanceAnchorFileID = group._ownerFileID;
            node._listDataLayer        = group._listDataLayer;

            XMLDocument state;
            XMLNode     root = state.appendRoot( "GameObject" );
            root.setAttribute( "_schemaVersion", "0" );
            root.setAttribute( "_name", string_view( node._name ) );
            root.setAttribute( "_bActive", "true" );
            XMLNode component = root.appendChild( "_listComponent" ).appendChild( kInstancedTypeName );
            component.setAttribute( "_meshID", string_view( group._meshText ) );
            component.setAttribute( "_materialPath", string_view( group._materialText ) );
            component.setAttribute( "_sectionName", string_view( group._sectionText ) );
            const XMLNode listInstance = component.appendChild( "_listInstance" );
            for ( size_t textIndex = 0; textIndex + 2 < group._listTransformText.size(); textIndex += 3 )
            {
                const XMLNode instance = listInstance.appendChild( kTransformTypeName );
                instance.setAttribute( "_position", string_view( group._listTransformText[textIndex] ) );
                instance.setAttribute( "_rotation", string_view( group._listTransformText[textIndex + 1] ) );
                instance.setAttribute( "_scale", string_view( group._listTransformText[textIndex + 2] ) );
            }
            node._embeddedXML = root.toString();
            listPacked.push_back( std::move( node ) );
        }

        vector<SceneDocument::SceneObjectNode> listKept;
        listKept.reserve( inoutDoc._listSceneObjectNode.size() + listPacked.size() );
        for ( SceneDocument::SceneObjectNode& node : inoutDoc._listSceneObjectNode )
        {
            if ( uniquePackedFileID.count( node._fileID ) == 0 )
                listKept.push_back( std::move( node ) );
        }
        for ( SceneDocument::SceneObjectNode& node : listPacked )
        {
            listKept.push_back( std::move( node ) );
        }
        inoutDoc._listSceneObjectNode = std::move( listKept );
        SW_LOG_INFO_ID( "SceneCooker.PackedStaticMeshes", "Scene cook: '%#' packed %# static mesh entities into %# instanced meshes", scenePath, static_cast<uint32>( uniquePackedFileID.size() ),
                        static_cast<uint32>( listPacked.size() ) );
        return true;
    }

    CookJobDesc SceneCooker::makePreloadCookJobDesc( const uint64 reflectionFingerprint )
    {
        CookJobDesc desc;
        desc._cookerName      = "ScenePreload";
        desc._codeFingerprint = CookCodeFingerprint::find( "ScenePreload" );
        desc._target          = "Any";
        desc._settingsHash    = reflectionFingerprint; // 어느 칸이 경로인지 · 파일에 없는 칸의 기본값은 타입 표가 정한다 — 입력 파일만으로는 모른다
        return desc;
    }

    CookJobDesc SceneCooker::makeHLODMeshCookJobDesc( const uint64 reflectionFingerprint )
    {
        CookJobDesc desc;
        desc._cookerName      = "SceneHLODMesh";
        desc._codeFingerprint = CookCodeFingerprint::find( "SceneHLODMesh" );
        desc._target          = "Any";
        desc._settingsHash    = reflectionFingerprint; // 대리를 짓는 씬의 값이 타입 표에서 온다(씬 쿠킹과 같다)
        return desc;
    }

    CookJobDesc SceneCooker::makeCookJobDesc( const uint64 reflectionFingerprint )
    {
        CookJobDesc desc;
        desc._cookerName      = "Scene";
        desc._codeFingerprint = CookCodeFingerprint::find( "Scene" );
        desc._target          = "Any";
        // 쿠킹된 상태는 리플렉션 표로 쓴다 — 타입 · 프로퍼티가 바뀌면 같은 XML 이라도 다시 베이크한다.
        desc._settingsHash = reflectionFingerprint;
        return desc;
    }

} // namespace sw
