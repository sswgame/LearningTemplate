#include "pch.h"

#include "Engine/Scene/SceneCooker.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/ScenePreloadList.h"
#include "Engine/Scene/WorldCellGrid.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SceneCookerInternal
        {
            /** @brief 파일 id → 그 엔티티로 지은 오브젝트 표를 만듭니다(씬의 런타임 id → 파일 id 표를 뒤집습니다). */
            static void makeObjectByFileID( const Scene& scene, unordered_map<uint64, GameObject*>& outMap )
            {
                outMap.clear();
                const GameObjectManager* pManager = scene.getObjectManager();
                if ( pManager == nullptr )
                    return;
                ObjectSavedIDMap mapSavedID;
                scene.collectSavedIDMap( mapSavedID );
                for ( const auto& [objectID, fileID] : mapSavedID )
                {
                    GameObject* pObject = pManager->findGameObjectByID( objectID );
                    if ( pObject != nullptr )
                        outMap.emplace( fileID, pObject );
                }
            }

            /** @brief 오브젝트의 상태를 파일 id 로 적은 XML 입니다. 쿠킹 전 · 쿠킹된 뒤의 상태를 비교하는 기준입니다. */
            static string makeStateText( const Scene& scene, const GameObject* pObject )
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

            /** @brief @p relativePath(리소스 기준)가 활성 게임이 아닌 게임 팩(`game/<다른 게임>/`)에 있으면 true 입니다. 활성 팩을 모르면 false 입니다. */
            static bool isInInactiveGamePack( string_view relativePath, string_view activePackRoot )
            {
                if ( activePackRoot.empty() || StringUtil::startsWith( relativePath, "game/", true ) == false )
                    return false;
                const string activePrefix = string( activePackRoot ) + "/";
                return StringUtil::startsWith( relativePath, activePrefix, true ) == false;
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

            /**
             * @brief 등록된 타입 · 프로퍼티 · 열거자의 지문입니다. 쿠킹된 상태의 바이트가 이 표로 정해지므로 작업 키의 설정 해시로 쓴다.
             * @details 이름순으로 늘어놓아 등록 순서(링크 순서)와 관계없다. 프로퍼티는 선언 순서 그대로다.
             */
            static uint64 computeReflectionFingerprint( const TypeRegistry& registry )
            {
                vector<string> listTypeText;
                registry.forEachType( [&listTypeText]( const TypeInfo& type )
                {
                    string text( type._fullyQualifiedName.view() );
                    for ( const PropertyInfo& property : type._listProperty )
                    {
                        text.append( "|" ).append( property._name.view() ).append( ":" ).append( property._typeName.view() );
                        text.append( "<" ).append( property._keyTypeName.view() ).append( "," ).append( property._elementTypeName.view() ).append( ">" );
                    }
                    listTypeText.push_back( std::move( text ) );
                } );
                registry.forEachEnum( [&listTypeText]( const EnumInfo& info )
                {
                    vector<string> listEntry;
                    for ( const auto& [name, value] : info._mapNameToValue )
                    {
                        listEntry.push_back( string( name.view() ) + "=" + sw::to_string( value ) );
                    }
                    std::sort( listEntry.begin(), listEntry.end() );
                    string text = "enum " + string( info._fullyQualifiedName.view() );
                    for ( const string& entry : listEntry )
                    {
                        text.append( "|" ).append( entry );
                    }
                    listTypeText.push_back( std::move( text ) );
                } );
                std::sort( listTypeText.begin(), listTypeText.end() );
                string canonical;
                for ( const string& text : listTypeText )
                {
                    canonical.append( text ).append( "\n" );
                }
                return HashUtil::computeStringHash64( canonical.c_str(), canonical.size(), false );
            }

            /**
             * @brief 셀 배치를 정할 때의 오브젝트입니다 — 계층의 루트입니다. 자식은 루트의 셀을 따른다(부착은 같은 셀 안의 참조여야 한다).
             * @details 루트가 늘 올라 있거나 씬 컴포넌트가 없으면(자리가 없다) 기본 묶음입니다.
             */
            static const GameObject* findPlacementRoot( const GameObject* pObject )
            {
                const GameObject* pRoot = pObject;
                while ( pRoot->getParent() != nullptr )
                {
                    pRoot = pRoot->getParent();
                }
                return pRoot;
            }

            /**
             * @brief 지어 둔 오브젝트마다 셀 · 플래그를 정하고, 그 오브젝트가 가리키는 다른 엔티티(파일 id)를 모읍니다.
             * @return 엔티티가 지어지지 않았으면 오류를 남기고 false 입니다.
             */
            static bool collectCellRecords( const SceneDocument& doc, const WorldCellGrid& grid, const unordered_map<uint64, GameObject*>& mapObjectByFileID, const ObjectSavedIDMap& mapSavedID,
                                            string_view scenePath, vector<WorldCellObjectRecord>& outListRecord, vector<WorldCellReference>& outListReference )
            {
                outListRecord.reserve( doc._listSceneObjectNode.size() );
                vector<uint64> listReferencedID;
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    const auto objectIt = mapObjectByFileID.find( entity._fileID );
                    if ( objectIt == mapObjectByFileID.end() )
                    {
                        SW_LOG_ERROR( "Scene cook: entity '%#' of '%#' was not built - it cannot be placed in a cell", entity._name, scenePath );
                        return false;
                    }
                    const GameObject*     pObject    = objectIt->second;
                    const GameObject*     pRoot      = findPlacementRoot( pObject );
                    const SceneComponent* pRootScene = pRoot->getPrimarySceneComponent();
                    WorldCellObjectRecord record{};
                    record._objectID      = entity._fileID;
                    record._bAlwaysLoaded = pObject->isAlwaysLoaded() || pRoot->isAlwaysLoaded() || pRootScene == nullptr;
                    record._cell          = pRootScene != nullptr ? grid.computeCell( pRootScene->getWorldPosition() ) : WorldCellCoord{};
                    outListRecord.push_back( record );

                    listReferencedID.clear();
                    ObjectSaveOptions options{ SerializePurpose::Authoring };
                    options._pSavedIDMap          = &mapSavedID;
                    options._pOutListReferencedID = &listReferencedID;
                    vector<uint8> stateBytes;
                    (void)ObjectStateSerializer::saveToBinaryBuffer( pObject, stateBytes, options ); // 참조만 모은다 — 바이트는 버린다
                    for ( const uint64 referencedObjectID : listReferencedID )
                    {
                        const auto targetIt = mapSavedID.find( referencedObjectID );
                        // 파일에 없는 대상(중첩 프리팹 부품)은 그 주인 엔티티와 함께 지어진다 — 엔티티 사이의 참조만 본다. 자기 자신도 뺀다.
                        if ( targetIt == mapSavedID.end() || targetIt->second == entity._fileID )
                            continue;
                        outListReference.push_back( WorldCellReference{ entity._fileID, targetIt->second } );
                    }
                }
                return true;
            }

            /** @brief 셀을 넘는 참조마다 오류를 남깁니다. */
            static void reportCrossCellViolations( const vector<WorldCellReference>& listViolation, const unordered_map<uint64, GameObject*>& mapObjectByFileID, string_view scenePath )
            {
                for ( const WorldCellReference& violation : listViolation )
                {
                    const auto fromIt = mapObjectByFileID.find( violation._fromObjectID );
                    const auto toIt   = mapObjectByFileID.find( violation._toObjectID );
                    SW_LOG_ERROR( "Scene cook: '%#' - entity '%#' (id %#) refers to '%#' (id %#) in another cell; mark the target always loaded or move it", scenePath,
                                  fromIt != mapObjectByFileID.end() ? fromIt->second->getName().c_str() : "?", violation._fromObjectID,
                                  toIt != mapObjectByFileID.end() ? toIt->second->getName().c_str() : "?", violation._toObjectID );
                }
            }

            /** @brief 기본 목록에 늘 올라 있는 엔티티를 남기고 나머지를 셀로 옮깁니다. 셀은 z, x 오름차순, 셀 안의 엔티티는 문서 순서다(결정적). */
            static void splitIntoCells( SceneDocument& inoutDoc, const vector<WorldCellObjectRecord>& listRecord )
            {
                vector<SceneDocument::SceneObjectNode> listBase;
                vector<SceneDocument::StreamingCell>   listCell;
                unordered_map<int64, size_t>           mapCellIndex;
                for ( size_t entityIndex = 0; entityIndex < inoutDoc._listSceneObjectNode.size(); ++entityIndex )
                {
                    SceneDocument::SceneObjectNode& entity = inoutDoc._listSceneObjectNode[entityIndex];
                    const WorldCellObjectRecord&    record = listRecord[entityIndex];
                    if ( record._bAlwaysLoaded )
                    {
                        listBase.push_back( std::move( entity ) );
                        continue;
                    }
                    // 셀 키 → 목록 인덱스 — 엔티티마다 셀 목록을 훑지 않는다(엔티티 수 × 셀 수).
                    const int64 cellKey   = ( static_cast<int64>( record._cell._z ) << 32 ) | static_cast<int64>( static_cast<uint32>( record._cell._x ) );
                    const auto  found     = mapCellIndex.find( cellKey );
                    size_t      cellIndex = 0;
                    if ( found == mapCellIndex.end() )
                    {
                        cellIndex = listCell.size();
                        mapCellIndex.emplace( cellKey, cellIndex );
                        listCell.push_back( SceneDocument::StreamingCell{ record._cell, {} } );
                    }
                    else
                    {
                        cellIndex = found->second;
                    }
                    listCell[cellIndex]._listSceneObjectNode.push_back( std::move( entity ) );
                }
                std::sort( listCell.begin(), listCell.end(), []( const SceneDocument::StreamingCell& lhs, const SceneDocument::StreamingCell& rhs )
                { return lhs._cell._z != rhs._cell._z ? lhs._cell._z < rhs._cell._z : lhs._cell._x < rhs._cell._x; } );
                inoutDoc._listSceneObjectNode = std::move( listBase );
                inoutDoc._listStreamingCell   = std::move( listCell );
            }

            /**
             * @brief 스트리밍하는 씬의 엔티티를 셀로 나눕니다. 늘 올라 있는 엔티티는 `_listSceneObjectNode` 에 남고 나머지는 `_listStreamingCell` 로 갑니다.
             * @details 쿠킹된 문서를 한 번 더 지어 오브젝트마다 자리 · 플래그 · 참조를 읽는다. 셀을 넘는 참조(`WorldCellGrid::findCrossCellReferences`)나
             *          프리팹을 찾지 못한 엔티티가 있으면 오류를 남기고 false 다 — 런타임에 조용히 끊기는 참조 · 빠지는 엔티티를 쿠킹본에 싣지 않는다.
             */
            static bool partitionStreamingCells( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view scenePath,
                                                 vector<WorldCellReference>* pOutListViolation )
            {
                WorldCellGrid grid;
                if ( WorldCellGrid::tryCreate( inoutDoc._worldSettings._cellGrid, grid ) == false )
                {
                    SW_LOG_ERROR( "Scene cook: '%#' streams but its cell grid is not usable", scenePath );
                    return false;
                }

                Scene      placement{ "SceneCooker.Placement", services };
                const bool bBuilt = placement.instantiate( inoutDoc );
                if ( bBuilt == false || placement.getUnresolvedEntityCount() > 0 )
                {
                    SW_LOG_ERROR( "Scene cook: '%#' could not be built to place its entities in cells (%# entities without a prefab)", scenePath,
                                  static_cast<uint32>( placement.getUnresolvedEntityCount() ) );
                    placement.shutdown();
                    return false;
                }
                unordered_map<uint64, GameObject*> mapObjectByFileID;
                makeObjectByFileID( placement, mapObjectByFileID );
                ObjectSavedIDMap mapSavedID;
                placement.collectSavedIDMap( mapSavedID );

                vector<WorldCellObjectRecord> listRecord;
                vector<WorldCellReference>    listReference;
                if ( collectCellRecords( inoutDoc, grid, mapObjectByFileID, mapSavedID, scenePath, listRecord, listReference ) == false )
                {
                    placement.shutdown();
                    return false;
                }

                vector<WorldCellReference> listViolation;
                WorldCellGrid::findCrossCellReferences( listRecord, listReference, listViolation );
                if ( listViolation.empty() == false )
                {
                    reportCrossCellViolations( listViolation, mapObjectByFileID, scenePath );
                    if ( pOutListViolation != nullptr )
                        *pOutListViolation = std::move( listViolation );
                    placement.shutdown();
                    return false;
                }
                placement.shutdown();

                splitIntoCells( inoutDoc, listRecord );
                SW_LOG_INFO( "Scene cook: '%#' split into %# cells (%# entities always loaded)", scenePath, static_cast<uint32>( inoutDoc._listStreamingCell.size() ),
                             static_cast<uint32>( inoutDoc._listSceneObjectNode.size() ) );
                return true;
            }

            /** @brief 씬 하나를 쿠킹해 @p outputPath 에 씁니다. 작업 범위 안에서 부릅니다 — 씬 · 프리팹 · 그 밖의 읽기가 모두 입력이 된다. */
            static CookStep cookScene( const SceneEngineServices& services, const TypeRegistry& registry, const string& scenePath, string_view relativeScene,
                                       string_view activePackRoot, const string& outputPath )
            {
                SceneDocument doc{};
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR( "Scene cook failed to read '%#'.", scenePath );
                    return CookStep::Failed;
                }
                // 다른 게임의 씬이 그 게임 모듈의 컴포넌트를 쓰면 건너뛴다 — 이 빌드에 없는 모듈이고, 배포본은 활성 게임의 팩만 연다.
                // 엔진 · 공용 타입만 쓰는 씬은 그대로 쿠킹한다(활성 팩의 모르는 타입은 아래에서 실패로 센다).
                if ( isInInactiveGamePack( relativeScene, activePackRoot ) && usesUnregisteredComponentTypes( registry, doc ) )
                {
                    SW_LOG_INFO( "Scene cook: '%#' uses components of another game's module - skipped", relativeScene );
                    return CookStep::Skipped;
                }

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
                    SW_LOG_ERROR( "Scene cook: '%#' has %# components of unknown type - not cooked", scenePath, missingComponentCount );
                    return CookStep::Failed;
                }
                // 이 비교는 **모든 빌드에서** 돌아야 한다. 아래 요약은 `SW_LOG_INFO` 라 Shipping 에서
                // 통째로 사라지는데, "쿠킹했다고 했지만 실은 XML 그대로" 는 그때도 알아야 할 일이다.
                if ( cookedCount < statefulCount )
                {
                    SW_LOG_WARNING( "Scene '%#': %# of %# entities could not be cooked to binary state - they keep XML.", scenePath, statefulCount - cookedCount,
                                    statefulCount );
                }
                // 스트리밍하는 씬은 엔티티를 셀로 나눈다. 셀을 넘는 참조가 있으면 쿠킹 실패다 — 런타임에 조용히 끊기는 참조를 싣지 않는다.
                if ( SceneCooker::splitStreamingCells( services, doc, scenePath ) == false )
                    return CookStep::Failed;
                if ( doc.saveBinary( outputPath ) == false )
                {
                    SW_LOG_ERROR( "Scene cook failed to write '%#'.", outputPath );
                    return CookStep::Failed;
                }
                SW_LOG_INFO( "Cooked '%#' -> '%#' (%# entities, %# as binary state)", scenePath, outputPath, static_cast<uint32>( doc.computeTotalEntityCount() ),
                             cookedCount );
                return CookStep::Written;
            }

            /**
             * @brief 씬 하나의 프리로드 목록(`ScenePreloadList`)을 만들어 @p outputPath 에 씁니다. 씬을 런타임과 같은 길로 지어 컴포넌트의 에셋 경로 프로퍼티를 모은다.
             * @details 씬 쿠킹과 따로 도는 작업이다(쿠킹 기록은 작업 하나에 출력 하나). 입력은 씬이 지어지며 읽은 모든 것이라, 프리팹을 고치면 목록도 다시 구워진다.
             */
            static CookStep cookPreloadList( const SceneEngineServices& services, const string& scenePath, const string& outputPath )
            {
                SceneDocument doc{};
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR( "Scene preload cook failed to read '%#'.", scenePath );
                    return CookStep::Failed;
                }
                Scene source{ "ScenePreload.Source", services };
                if ( source.instantiate( doc ) == false )
                {
                    SW_LOG_ERROR( "Scene preload cook could not build '%#'.", scenePath );
                    return CookStep::Failed;
                }
                vector<string> listPath;
                ScenePreloadList::collect( *source.getObjectManager(), listPath );
                if ( ScenePreloadList::write( outputPath, listPath ) == false )
                {
                    SW_LOG_ERROR( "Scene preload cook failed to write '%#'.", outputPath );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SceneCooker" );

    uint32 SceneCooker::cookEntityState( const SceneEngineServices& services, SceneDocument& inoutDoc, uint32* pOutMissingComponentCount )
    {
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = 0;
        if ( services._pTypeRegistry == nullptr )
        {
            SW_LOG_ERROR( "Scene cook: '%#' is not cooked - the engine services have no type registry", inoutDoc._name );
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
                SW_LOG_ERROR( "Scene cook: entity '%#' of '%#' has no id - nothing is cooked", entity._name, inoutDoc._name );
                return 0;
            }
        }

        // 쿠킹 전용 씬이다. 실제 씬 매니저의 것을 쓰면 쿠킹하는 동안 만든 임시 오브젝트가 실제 씬에 남는다.
        Scene source{ "SceneCooker.Source", services };
        if ( source.instantiate( inoutDoc ) == false )
            return 0;
        const uint32 missingComponentCount = SceneCookerInternal::reportMissingComponents( source );
        if ( pOutMissingComponentCount != nullptr )
            *pOutMissingComponentCount = missingComponentCount;
        unordered_map<uint64, GameObject*> mapSourceByFileID;
        SceneCookerInternal::makeObjectByFileID( source, mapSourceByFileID );

        ObjectSavedIDMap mapSourceSavedID;
        source.collectSavedIDMap( mapSourceSavedID );
        ObjectSaveOptions saveOptions{ SerializePurpose::Authoring };
        saveOptions._pSavedIDMap = &mapSourceSavedID;

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
        if ( verify.instantiate( cooked ) == false )
            return 0;
        unordered_map<uint64, GameObject*> mapVerifyByFileID;
        SceneCookerInternal::makeObjectByFileID( verify, mapVerifyByFileID );

        uint32 cookedCount{ 0 };
        for ( size_t entityIndex = 0; entityIndex < inoutDoc._listSceneObjectNode.size(); ++entityIndex )
        {
            SceneDocument::SceneObjectNode&       entity      = inoutDoc._listSceneObjectNode[entityIndex];
            const SceneDocument::SceneObjectNode& cookedState = cooked._listSceneObjectNode[entityIndex];
            if ( cookedState._embeddedStateBytes.empty() )
            {
                if ( entity._embeddedXML.empty() == false )
                    SW_LOG_WARNING( "Entity '%#' could not be cooked to binary state - keeping XML.", entity._name );
                continue;
            }
            const auto        sourceIt       = mapSourceByFileID.find( entity._fileID );
            const auto        verifyIt       = mapVerifyByFileID.find( entity._fileID );
            const GameObject* pSource        = ( sourceIt != mapSourceByFileID.end() ) ? sourceIt->second : nullptr;
            const GameObject* pVerify        = ( verifyIt != mapVerifyByFileID.end() ) ? verifyIt->second : nullptr;
            const bool        bStateMismatch = pSource == nullptr || pVerify == nullptr ||
                                        SceneCookerInternal::makeStateText( source, pSource ) != SceneCookerInternal::makeStateText( verify, pVerify );
            if ( bStateMismatch )
            {
                SW_LOG_WARNING( "Entity '%#' changes when its cooked state is read back - keeping XML.", entity._name );
                continue;
            }

            entity._embeddedStateBytes = cookedState._embeddedStateBytes;
            // 둘 다 실으면 파일만 커진다. 바이너리가 기준이 된 순간 XML 은 뺀다.
            entity._embeddedXML.clear();
            ++cookedCount;
        }

        verify.shutdown();
        source.shutdown();
        return cookedCount;
    }

    uint32 SceneCooker::cookAllScenes( const SceneEngineServices& services, string_view sourceRoot, string_view cookedDir, const CookStore& store,
                                       uint32& outFailedCount )
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
        if ( services._pTypeRegistry == nullptr )
        {
            SW_LOG_ERROR( "Scene cook has no type registry in the engine services - nothing is cooked" );
            ++outFailedCount;
            return 0;
        }
        const TypeRegistry& registry = *services._pTypeRegistry;
        if ( registry.areAllModuleTypesRegistered() == false )
        {
            SW_LOG_ERROR( "Scene cook ran before every module registered its types - nothing is cooked (cook after the ModuleTypes startup step)" );
            ++outFailedCount;
            return 0;
        }

        vector<string> listSceneFile;
        FileUtil::collectFiles( resourceRoot, ".xml", listSceneFile, true );
        std::sort( listSceneFile.begin(), listSceneFile.end() );

        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      normalizedOut  = FileUtil::normalizeSeparators( cookedDir );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        const CookJobDesc desc           = makeCookJobDesc( registry );
        const CookJobDesc preloadDesc    = makePreloadCookJobDesc();

        CookTally tally;
        for ( const string& scenePath : listSceneFile )
        {
            if ( StringUtil::endsWith( scenePath, ".scene.xml", true ) == false )
                continue;
            const string relativeScene = FileUtil::normalizeSeparators( scenePath ).substr( std::min( normalizedRoot.size() + 1, scenePath.size() ) );
            if ( SceneCookerInternal::isEditorOnlyScene( relativeScene ) )
                continue;

            // 출력은 `<cookedDir>/<Resource 기준 상대 경로>` 에 같은 이름으로, 확장자만 .bin 이다.
            const string outputID   = AssetCookPath::toCookedPath( relativeScene );
            string       outputPath = normalizedOut;
            if ( outputPath.empty() == false && outputPath.back() != '/' )
                outputPath += '/';
            outputPath += outputID;

            const CookOutcome outcome = store.cookFile( desc, outputID, outputPath, [&]()
            { return SceneCookerInternal::cookScene( services, registry, scenePath, relativeScene, activePackRoot, outputPath ); } );
            outFailedCount += outcome == CookOutcome::Failed ? 1u : 0u;
            tally.add( outcome );

            // 쿠킹한 씬마다 프리로드 목록을 옆에 굽는다(레벨 전환이 `PreloadSet` 으로 연다). 씬 쿠킹이 실패했거나 건너뛴 씬은 목록도 없다.
            const bool bSceneWritten = outcome == CookOutcome::Cooked || outcome == CookOutcome::Reused;
            if ( bSceneWritten == false )
                continue;
            const string      preloadID      = ScenePreloadList::makeListPath( outputID );
            const string      preloadPath    = ScenePreloadList::makeListPath( outputPath );
            const CookOutcome preloadOutcome = store.cookFile( preloadDesc, preloadID, preloadPath, [&]()
            { return SceneCookerInternal::cookPreloadList( services, scenePath, preloadPath ); } );
            outFailedCount += preloadOutcome == CookOutcome::Failed ? 1u : 0u;
        }

        SW_LOG_INFO( "Scene cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }

    bool SceneCooker::splitStreamingCells( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view sceneName,
                                           vector<WorldCellReference>* pOutListViolation )
    {
        if ( pOutListViolation != nullptr )
            pOutListViolation->clear();
        if ( inoutDoc._worldSettings._bStreaming == false )
            return true;
        if ( inoutDoc._listStreamingCell.empty() == false )
        {
            SW_LOG_ERROR( "Scene cook: '%#' is already split into cells", sceneName );
            return false;
        }
        return SceneCookerInternal::partitionStreamingCells( services, inoutDoc, sceneName, pOutListViolation );
    }

    bool SceneCooker::previewStreamingCells( const Scene& scene, StreamingCellPreview& outPreview )
    {
        outPreview = StreamingCellPreview{};
        if ( scene.getWorldSettings()._bStreaming == false )
        {
            SW_LOG_ERROR( "Streaming cell preview: '%#' does not stream", scene.getName().c_str() );
            return false;
        }
        SceneDocument doc{};
        if ( scene.serializeToDocument( doc ) == false )
        {
            SW_LOG_ERROR( "Streaming cell preview: '%#' could not be written as a document", scene.getName().c_str() );
            return false;
        }
        // 문서의 파일 id 는 씬이 들고 있는 표(런타임 id → 파일 id)에서 왔다 — 뒤집어 열린 씬의 오브젝트로 되돌린다.
        ObjectSavedIDMap mapSavedID;
        scene.collectSavedIDMap( mapSavedID );
        unordered_map<uint64, uint64> mapObjectIDByFileID;
        mapObjectIDByFileID.reserve( mapSavedID.size() );
        for ( const auto& [objectID, fileID] : mapSavedID )
        {
            mapObjectIDByFileID.emplace( fileID, objectID );
        }
        const auto toObjectID = [&mapObjectIDByFileID]( uint64 fileID )
        {
            const auto objectIt = mapObjectIDByFileID.find( fileID );
            return objectIt != mapObjectIDByFileID.end() ? objectIt->second : uint64{ 0 };
        };

        vector<WorldCellReference> listViolation;
        if ( splitStreamingCells( scene.getEngineServices(), doc, scene.getName(), &listViolation ) == false )
        {
            if ( listViolation.empty() )
                return false; // 나누지 못한 이유는 쿠커가 로그에 남겼다
            for ( const WorldCellReference& violation : listViolation )
            {
                outPreview._listViolation.push_back( WorldCellReference{ toObjectID( violation._fromObjectID ), toObjectID( violation._toObjectID ) } );
            }
            return true;
        }
        for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
        {
            outPreview._listAlwaysLoadedID.push_back( toObjectID( entity._fileID ) );
        }
        for ( const SceneDocument::StreamingCell& cell : doc._listStreamingCell )
        {
            StreamingCellPreviewCell& previewCell = outPreview._listCell.emplace_back();
            previewCell._cell                     = cell._cell;
            for ( const SceneDocument::SceneObjectNode& entity : cell._listSceneObjectNode )
            {
                previewCell._listObjectID.push_back( toObjectID( entity._fileID ) );
            }
        }
        return true;
    }

    CookJobDesc SceneCooker::makePreloadCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName    = "ScenePreload";
        desc._cookerVersion = ScenePreloadList::kVersion;
        desc._target        = "Any";
        desc._settingsHash  = 0; // 목록은 컴포넌트의 경로 프로퍼티 값이 정한다 — 타입 표가 바뀌어 프로퍼티가 달라지면 입력(씬 · 프리팹)을 다시 읽어 알게 된다
        return desc;
    }

    CookJobDesc SceneCooker::makeCookJobDesc( const TypeRegistry& registry )
    {
        CookJobDesc desc;
        desc._cookerName    = "Scene";
        desc._cookerVersion = kCookVersion;
        desc._target        = "Any";
        // 쿠킹된 상태는 리플렉션 표로 쓴다 — 타입 · 프로퍼티가 바뀌면 같은 XML 이라도 다시 베이크한다.
        desc._settingsHash = SceneCookerInternal::computeReflectionFingerprint( registry );
        return desc;
    }

} // namespace sw
