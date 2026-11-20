#include "pch.h"

#include "Engine/Scene/SceneCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/ScenePreloadList.h"
#include "Engine/Scene/StreamingCellGrid.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
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
                GameObjectManager* pManager = scene.getObjectManager();
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
                        listEntry.push_back( string( name.toString() ) + "=" + sw::to_string( value ) );
                    }
                    VectorUtil::sort( listEntry );
                    string text = "enum " + string( info._fullyQualifiedName.view() );
                    for ( const string& entry : listEntry )
                    {
                        text.append( "|" ).append( entry );
                    }
                    listTypeText.push_back( std::move( text ) );
                } );
                VectorUtil::sort( listTypeText );
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
            [[nodiscard]] static bool collectCellRecords( const SceneDocument& doc, const StreamingCellGrid& grid, const unordered_map<uint64, GameObject*>& mapObjectByFileID, const ObjectSavedIDMap& mapSavedID,
                                                          string_view scenePath, vector<StreamingCellObjectRecord>& outListRecord, vector<StreamingCellReference>& outListReference )
            {
                outListRecord.reserve( doc._listSceneObjectNode.size() );
                vector<uint64> listReferencedID;
                for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
                {
                    const auto objectIt = mapObjectByFileID.find( entity._fileID );
                    if ( objectIt == mapObjectByFileID.end() )
                    {
                        SW_LOG_ERROR_ID( "SceneCooker.SceneCookEntityNot", "Scene cook: entity '%#' of '%#' was not built - it cannot be placed in a cell", entity._name, scenePath );
                        return false;
                    }
                    const GameObject*         pObject    = objectIt->second;
                    const GameObject*         pRoot      = findPlacementRoot( pObject );
                    const SceneComponent*     pRootScene = pRoot->getPrimarySceneComponent();
                    StreamingCellObjectRecord record{};
                    record._objectID      = entity._fileID;
                    record._bAlwaysLoaded = pObject->isAlwaysLoaded() || pRoot->isAlwaysLoaded() || pRootScene == nullptr;
                    record._cell          = pRootScene != nullptr ? grid.computeCell( pRootScene->getWorldPosition() ) : StreamingCellCoord{};
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
                        outListReference.push_back( StreamingCellReference{ entity._fileID, targetIt->second } );
                    }
                }
                return true;
            }

            /** @brief 셀을 넘는 참조마다 오류를 남깁니다. */
            static void reportCrossCellViolations( const vector<StreamingCellReference>& listViolation, const unordered_map<uint64, GameObject*>& mapObjectByFileID, string_view scenePath )
            {
                for ( const StreamingCellReference& violation : listViolation )
                {
                    const auto fromIt = mapObjectByFileID.find( violation._fromObjectID );
                    const auto toIt   = mapObjectByFileID.find( violation._toObjectID );
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookEntityId", "Scene cook: '%#' - entity '%#' (id %#) refers to '%#' (id %#) in another cell; mark the target always loaded or move it", scenePath,
                                     fromIt != mapObjectByFileID.end() ? fromIt->second->getName().c_str() : "?", violation._fromObjectID,
                                     toIt != mapObjectByFileID.end() ? toIt->second->getName().c_str() : "?", violation._toObjectID );
                }
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
            [[nodiscard]] static bool validateDataLayerReferences( const SceneEngineServices& services, const SceneDocument& doc, string_view scenePath )
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
                makeObjectByFileID( placement, mapObjectByFileID );
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

            /** @brief 기본 목록에 늘 올라 있는 엔티티를 남기고 나머지를 셀로 옮깁니다. 셀은 z, x, 레이어 수, 레이어 이름 오름차순, 셀 안의 엔티티는 문서 순서다(결정적). 좌표 + 레이어 집합이 셀 하나다. */
            static void splitIntoCells( SceneDocument& inoutDoc, const vector<StreamingCellObjectRecord>& listRecord )
            {
                vector<SceneDocument::SceneObjectNode> listBase;
                vector<SceneDocument::StreamingCell>   listCell;
                unordered_map<string, size_t>          mapCellIndex;
                for ( size_t entityIndex = 0; entityIndex < inoutDoc._listSceneObjectNode.size(); ++entityIndex )
                {
                    SceneDocument::SceneObjectNode&  entity = inoutDoc._listSceneObjectNode[entityIndex];
                    const StreamingCellObjectRecord& record = listRecord[entityIndex];
                    if ( record._bAlwaysLoaded )
                    {
                        listBase.push_back( std::move( entity ) );
                        continue;
                    }
                    // 셀 키 → 목록 인덱스 — 엔티티마다 셀 목록을 훑지 않는다(엔티티 수 × 셀 수).
                    string cellKey = sw::to_string( static_cast<int64>( record._cell._z ) ) + "," + sw::to_string( static_cast<int64>( record._cell._x ) );
                    for ( const string& layer : entity._listDataLayer )
                    {
                        cellKey.append( "|" ).append( layer );
                    }
                    const auto found     = mapCellIndex.find( cellKey );
                    size_t     cellIndex = 0;
                    if ( found == mapCellIndex.end() )
                    {
                        cellIndex = listCell.size();
                        mapCellIndex.emplace( cellKey, cellIndex );
                        listCell.push_back( SceneDocument::StreamingCell{ record._cell, {}, entity._listDataLayer } );
                    }
                    else
                    {
                        cellIndex = found->second;
                    }
                    listCell[cellIndex]._listSceneObjectNode.push_back( std::move( entity ) );
                }
                VectorUtil::sort( listCell, []( const SceneDocument::StreamingCell& lhs, const SceneDocument::StreamingCell& rhs )
                {
                    if ( lhs._cell._z != rhs._cell._z )
                        return lhs._cell._z < rhs._cell._z;
                    if ( lhs._cell._x != rhs._cell._x )
                        return lhs._cell._x < rhs._cell._x;
                    return lhs._listDataLayer.size() != rhs._listDataLayer.size() ? lhs._listDataLayer.size() < rhs._listDataLayer.size() : lhs._listDataLayer < rhs._listDataLayer;
                } );
                inoutDoc._listSceneObjectNode = std::move( listBase );
                inoutDoc._listStreamingCell   = std::move( listCell );
            }

            /** @brief HLOD 대리가 읽고 쓰는 형식입니다 — 씬(5)은 메시(6)를 몰라 타입 표에서 이름으로 찾는다(`packStaticMeshes` 와 같은 규칙). */
            struct HLODMeshTypes
            {
                static constexpr uint32 kMesh     = 0;
                static constexpr uint32 kMaterial = 1;
                static constexpr uint32 kSection  = 2;
                static constexpr uint32 kStatic   = 3;
                static constexpr uint32 kVisible  = 4;
                static constexpr uint32 kKeyCount = 5;

                const TypeInfo*     _pMeshType{ nullptr };
                const PropertyInfo* _arrMeshProp[kKeyCount]{}; ///< 메시 · 머티리얼 · 구간 · 정적 · 보임
                hashed_string       _vectorTypeName;           ///< 인스턴스 자리 글의 형식(메시 `_localPosition` 의 float3)
            };

            /**
             * @brief 대리 형식을 타입 표에서 찾습니다. 나누기 전에 불러 실패하면 문서를 건드리지 않는다.
             * @return 형식 · 칸이 표에 없으면 오류를 남기고 false 입니다(조용히 대리 없이 넘어가지 않는다).
             */
            [[nodiscard]] static bool resolveHLODMeshTypes( const SceneEngineServices& services, string_view scenePath, HLODMeshTypes& outTypes )
            {
                static constexpr const utf8* kMeshTypeName      = "MeshComponent";
                static constexpr const utf8* kInstancedTypeName = "InstancedMeshComponent";
                const TypeInfo* const        pMeshType          = services._pTypeRegistry != nullptr ? services._pTypeRegistry->findType( hashed_string( kMeshTypeName ) ) : nullptr;
                const TypeInfo* const        pInstancedType     = services._pTypeRegistry != nullptr ? services._pTypeRegistry->findType( hashed_string( kInstancedTypeName ) ) : nullptr;
                if ( pMeshType == nullptr || pInstancedType == nullptr )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.HLODTypesMissing", "Scene cook: '%#' streams but '%#' or '%#' is not in the type table - its HLOD proxies cannot be built", scenePath, kMeshTypeName,
                                     kInstancedTypeName );
                    return false;
                }
                static constexpr const utf8* kArrMeshKey[HLODMeshTypes::kKeyCount] = { "_meshID", "_materialPath", "_sectionName", "_bStatic", "_bVisible" };
                bool                         bPropertiesKnown                      = true;
                outTypes._pMeshType                                                = pMeshType;
                for ( uint32 keyIndex = 0; keyIndex < HLODMeshTypes::kKeyCount; ++keyIndex )
                {
                    outTypes._arrMeshProp[keyIndex] = pMeshType->findPropertyInHierarchy( hashed_string( kArrMeshKey[keyIndex] ) );
                    bPropertiesKnown                = bPropertiesKnown && outTypes._arrMeshProp[keyIndex] != nullptr;
                }
                const PropertyInfo* pPositionProp = pMeshType->findPropertyInHierarchy( hashed_string( "_localPosition" ) );
                if ( bPropertiesKnown == false || pPositionProp == nullptr )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.HLODPropertiesMissing", "Scene cook: '%#' streams but '%#' lacks a property the HLOD builder reads", scenePath, kMeshTypeName );
                    return false;
                }
                outTypes._vectorTypeName = pPositionProp->_typeName;
                return true;
            }

            /** @brief HLOD 대리 하나에 모을 (메시, 머티리얼, 구간) 묶음입니다 — 인스턴스마다 월드 위치 · 회전 · 스케일 글 셋. */
            struct HLODGroup
            {
                string         _meshText;
                string         _materialText;
                string         _sectionText;
                vector<string> _listTransformText;
            };

            /**
             * @brief 오브젝트 @p pObject 와 그 자식(다른 엔티티가 아닌 것 — 중첩 프리팹 부품)의 정적 메시를 @p inoutListGroup 에 모읍니다.
             * @details 자리는 월드 행렬을 나눠 쓴다 — 대리는 원점에 있다.
             */
            static void collectHLODMeshes( const GameObject* pObject, const unordered_set<const GameObject*>& uniqueEntityObject, const HLODMeshTypes& types,
                                           vector<HLODGroup>& inoutListGroup )
            {
                const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
                for ( const Component* pComponent : pObject->getComponents() )
                {
                    if ( pComponent == nullptr || pComponent->getTypeInfo() != types._pMeshType || pComponent->isSceneComponent() == false )
                        continue;
                    // 움직이는 메시(정적이 아님)와 숨긴 메시는 대리에 넣지 않는다.
                    string arrText[HLODMeshTypes::kKeyCount];
                    for ( uint32 keyIndex = 0; keyIndex < HLODMeshTypes::kKeyCount; ++keyIndex )
                    {
                        arrText[keyIndex] = SerializerUtil::formatPropertyText( *types._arrMeshProp[keyIndex], ObjectAddress{ pComponent }, context );
                    }
                    if ( arrText[HLODMeshTypes::kStatic] != "true" || arrText[HLODMeshTypes::kVisible] != "true" )
                        continue;
                    float3     scale{};
                    quaternion rotation{};
                    float3     position{};
                    if ( static_cast<const SceneComponent*>( pComponent )->getWorldMatrix().decompose( scale, rotation, position ) == false )
                        continue; // 스케일 0 — 그릴 것이 없다
                    const float3 euler  = rotation.getEulerAngles();
                    HLODGroup*   pGroup = nullptr;
                    for ( HLODGroup& group : inoutListGroup )
                    {
                        const bool bSame = group._meshText == arrText[HLODMeshTypes::kMesh] && group._materialText == arrText[HLODMeshTypes::kMaterial] &&
                                           group._sectionText == arrText[HLODMeshTypes::kSection];
                        pGroup = pGroup == nullptr && bSame ? &group : pGroup;
                    }
                    if ( pGroup == nullptr )
                        pGroup = &inoutListGroup.emplace_back( HLODGroup{ arrText[HLODMeshTypes::kMesh], arrText[HLODMeshTypes::kMaterial], arrText[HLODMeshTypes::kSection], {} } );
                    pGroup->_listTransformText.push_back( SerializerUtil::formatValueText( ValueAddress{ &position }, types._vectorTypeName, context ) );
                    pGroup->_listTransformText.push_back( SerializerUtil::formatValueText( ValueAddress{ &euler }, types._vectorTypeName, context ) );
                    pGroup->_listTransformText.push_back( SerializerUtil::formatValueText( ValueAddress{ &scale }, types._vectorTypeName, context ) );
                }
                vector<GameObject*> listChild;
                pObject->getChildren( listChild );
                for ( const GameObject* pChild : listChild )
                {
                    if ( uniqueEntityObject.count( pChild ) == 0 ) // 붙은 엔티티는 제 엔티티 차례에 모은다(같은 셀이다)
                        collectHLODMeshes( pChild, uniqueEntityObject, types, inoutListGroup );
                }
            }

            /**
             * @brief 셀 조각마다 정적 메시를 인스턴스 메시로 모은 HLOD 대리 엔티티를 기본 목록(늘 올라 있음)에 더하고 조각에 그 파일 id 를 적습니다(설계 "씬 스트리밍 (라)").
             * @details 언리얼 World Partition HLOD 의 Instancing 레이어(셀마다 대리 액터 하나 — 원본 메시를 인스턴스로)와 같은 꼴이다. 대리는 원점의 씬 컴포넌트 아래
             *          (메시, 머티리얼, 구간)마다 `InstancedMeshComponent` 하나이고 인스턴스 자리는 월드 자리다. 조각의 레이어를 물려받아 레이어가 꺼지면 대리도 없다.
             *          정적 메시가 없는 조각은 대리가 없다. @p mapObjectByFileID 는 나누기 전에 지은 배치 씬의 오브젝트다.
             */
            static void buildHLODProxies( const HLODMeshTypes& types, const unordered_map<uint64, GameObject*>& mapObjectByFileID, SceneDocument& inoutDoc, string_view scenePath )
            {
                static constexpr const utf8*     kInstancedTypeName = "InstancedMeshComponent";
                static constexpr const utf8*     kTransformTypeName = "InstancedMeshTransform";
                unordered_set<const GameObject*> uniqueEntityObject;
                for ( const auto& [fileID, pObject] : mapObjectByFileID )
                {
                    uniqueEntityObject.insert( pObject );
                }
                uint64 nextFileID = SceneDocument::computeMaxFileID( inoutDoc._listSceneObjectNode );
                for ( const SceneDocument::StreamingCell& cell : inoutDoc._listStreamingCell )
                {
                    nextFileID = MathUtil::max( nextFileID, SceneDocument::computeMaxFileID( cell._listSceneObjectNode ) );
                }
                nextFileID += 1;

                uint32            proxyCount = 0;
                vector<HLODGroup> listGroup;
                for ( SceneDocument::StreamingCell& cell : inoutDoc._listStreamingCell )
                {
                    listGroup.clear();
                    for ( const SceneDocument::SceneObjectNode& node : cell._listSceneObjectNode )
                    {
                        const auto objectIt = mapObjectByFileID.find( node._fileID );
                        if ( objectIt != mapObjectByFileID.end() )
                            collectHLODMeshes( objectIt->second, uniqueEntityObject, types, listGroup );
                    }
                    if ( listGroup.empty() )
                        continue;

                    SceneDocument::SceneObjectNode proxy{};
                    proxy._name = "HLOD_" + sw::to_string( static_cast<int64>( cell._cell._x ) ) + "_" + sw::to_string( static_cast<int64>( cell._cell._z ) );
                    for ( const string& layer : cell._listDataLayer )
                    {
                        proxy._name.append( "_" ).append( layer );
                    }
                    proxy._fileID        = nextFileID++;
                    proxy._listDataLayer = cell._listDataLayer;

                    XMLDocument state;
                    XMLNode     root = state.appendRoot( "GameObject" );
                    root.setAttribute( "_schemaVersion", "0" );
                    root.setAttribute( "_name", string_view( proxy._name ) );
                    root.setAttribute( "_bActive", "true" );
                    root.setAttribute( "_bAlwaysLoaded", "true" ); // 셀에 들지 않는다 — 원점 자리라 런타임 스폰으로 셀에 넣지 않게(`SceneStreamingSubsystem::regroupObjects`)
                    // 지역 id 를 적는다 — 루트 씬 컴포넌트는 1, 묶음 컴포넌트는 2 부터. 부착은 루트의 id 키다(`ComponentStableKey`).
                    constexpr uint32 kRootLocalID  = 1;
                    uint32           nextLocalID   = kRootLocalID + 1;
                    const string     rootKey       = ComponentStableKey::formatKey( kRootLocalID );
                    XMLNode          listComponent = root.appendChild( "_listComponent" );
                    XMLNode          rootComponent = listComponent.appendChild( "SceneComponent" );
                    rootComponent.setAttribute( "_localID", kRootLocalID );
                    for ( const HLODGroup& group : listGroup )
                    {
                        XMLNode component = listComponent.appendChild( kInstancedTypeName );
                        component.setAttribute( "_localID", nextLocalID++ );
                        component.setAttribute( "_attachOwner", "None" );
                        component.setAttribute( "_attachComponent", string_view( rootKey ) );
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
                    }
                    proxy._embeddedXML = root.toString();
                    cell._hlodFileID   = proxy._fileID;
                    inoutDoc._listSceneObjectNode.push_back( std::move( proxy ) );
                    ++proxyCount;
                }
                if ( proxyCount > 0 )
                    SW_LOG_INFO_ID( "SceneCooker.SceneCookHLODProxies", "Scene cook: '%#' built %# HLOD proxies for %# cells", scenePath, proxyCount,
                                    static_cast<uint32>( inoutDoc._listStreamingCell.size() ) );
            }

            /**
             * @brief 스트리밍하는 씬의 엔티티를 셀로 나눕니다. 늘 올라 있는 엔티티는 `_listSceneObjectNode` 에 남고 나머지는 `_listStreamingCell` 로 갑니다.
             * @details 쿠킹된 문서를 한 번 더 지어 오브젝트마다 자리 · 플래그 · 참조를 읽는다. 셀을 넘는 참조(`StreamingCellGrid::findCrossCellReferences`)나
             *          프리팹을 찾지 못한 엔티티가 있으면 오류를 남기고 false 다 — 런타임에 조용히 끊기는 참조 · 빠지는 엔티티를 쿠킹본에 싣지 않는다.
             */
            static bool partitionStreamingCells( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view scenePath,
                                                 vector<StreamingCellReference>* pOutListViolation )
            {
                StreamingCellGrid grid;
                if ( StreamingCellGrid::tryCreate( inoutDoc._sceneSettings._cellGrid, grid ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookStreamsBut", "Scene cook: '%#' streams but its cell grid is not usable", scenePath );
                    return false;
                }

                Scene placement{ "SceneCooker.Placement", services };
                placement.setBuildAllDataLayers();
                const bool bBuilt = placement.instantiate( inoutDoc );
                if ( bBuilt == false || placement.getUnresolvedEntityCount() > 0 )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookCouldNot", "Scene cook: '%#' could not be built to place its entities in cells (%# entities without a prefab)", scenePath,
                                     static_cast<uint32>( placement.getUnresolvedEntityCount() ) );
                    return false;
                }
                unordered_map<uint64, GameObject*> mapObjectByFileID;
                makeObjectByFileID( placement, mapObjectByFileID );
                ObjectSavedIDMap mapSavedID;
                placement.collectSavedIDMap( mapSavedID );

                vector<StreamingCellObjectRecord> listRecord;
                vector<StreamingCellReference>    listReference;
                if ( collectCellRecords( inoutDoc, grid, mapObjectByFileID, mapSavedID, scenePath, listRecord, listReference ) == false )
                    return false;

                vector<StreamingCellReference> listViolation;
                StreamingCellGrid::findCrossCellReferences( listRecord, listReference, listViolation );
                if ( listViolation.empty() == false )
                {
                    reportCrossCellViolations( listViolation, mapObjectByFileID, scenePath );
                    if ( pOutListViolation != nullptr )
                        *pOutListViolation = std::move( listViolation );
                    return false;
                }

                HLODMeshTypes hlodTypes{};
                if ( resolveHLODMeshTypes( services, scenePath, hlodTypes ) == false )
                    return false;
                splitIntoCells( inoutDoc, listRecord );
                // 나눈 조각마다 HLOD 대리를 굽는다 — 배치 씬(`placement`)이 아직 살아 있어 엔티티의 월드 자리를 그대로 읽는다.
                if ( placement.getObjectManager()->hasDirtySceneTransforms() )
                    placement.getObjectManager()->flushSceneTransforms();
                buildHLODProxies( hlodTypes, mapObjectByFileID, inoutDoc, scenePath );
                SW_LOG_INFO_ID( "SceneCooker.SceneCookSplitCells", "Scene cook: '%#' split into %# cells (%# entities always loaded)", scenePath, static_cast<uint32>( inoutDoc._listStreamingCell.size() ),
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
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookFailedRead", "Scene cook failed to read '%#'.", scenePath );
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
                if ( SceneCooker::packStaticMeshes( services, doc, scenePath ) == false )
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
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookComponentsUnknown", "Scene cook: '%#' has %# components of unknown type - not cooked", scenePath, missingComponentCount );
                    return CookStep::Failed;
                }
                // 이 비교는 **모든 빌드에서** 돌아야 한다. 아래 요약은 `SW_LOG_INFO` 라 Shipping 에서
                // 통째로 사라지는데, "쿠킹했다고 했지만 실은 XML 그대로" 는 그때도 알아야 할 일이다.
                if ( cookedCount < statefulCount )
                {
                    SW_LOG_WARNING_ID( "SceneCooker.SceneEntitiesCouldNot", "Scene '%#': %# of %# entities could not be cooked to binary state - they keep XML.", scenePath, statefulCount - cookedCount,
                                       statefulCount );
                }
                // 레이어를 넘는 참조도 쿠킹 실패다 — 레이어를 끈 채 오른 쪽의 참조가 조용히 끊긴다.
                if ( validateDataLayerReferences( services, doc, scenePath ) == false )
                    return CookStep::Failed;
                // 스트리밍하는 씬은 엔티티를 셀로 나눈다. 셀을 넘는 참조가 있으면 쿠킹 실패다 — 런타임에 조용히 끊기는 참조를 싣지 않는다.
                if ( SceneCooker::splitStreamingCells( services, doc, scenePath ) == false )
                    return CookStep::Failed;
                // 쿠킹본은 펼친 결과만 싣는다 — 런타임의 짓는 길이 하나다.
                doc.stripAuthoringInstanceData();
                const optional<AbsolutePath> outputFile = FileUtil::requireAbsolutePath( outputPath );
                if ( outputFile.has_value() == false || doc.saveBinary( *outputFile ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.SceneCookFailedWrite", "Scene cook failed to write '%#'.", outputPath );
                    return CookStep::Failed;
                }
                SW_LOG_INFO_ID( "SceneCooker.CookedEntitiesBinaryState", "Cooked '%#' -> '%#' (%# entities, %# as binary state)", scenePath, outputPath, static_cast<uint32>( doc.computeTotalEntityCount() ),
                                cookedCount );
                return CookStep::Written;
            }

            /**
             * @brief 씬 하나의 프리로드 목록(`ScenePreloadList`)을 만들어 @p outputPath 에 씁니다. 씬을 런타임과 같은 길로 지어 컴포넌트의 에셋 경로 프로퍼티를 모은다.
             * @details 씬 쿠킹과 따로 도는 작업이다(쿠킹 기록은 작업 하나에 출력 하나). 입력은 씬이 지어지며 읽은 모든 것이라, 프리팹을 고치면 목록도 다시 구워진다.
             */
            static CookStep cookPreloadList( SceneIterationKey key, const SceneEngineServices& services, const string& scenePath, const string& outputPath )
            {
                SceneDocument doc{};
                if ( doc.loadXMLFromDiskFile( scenePath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookFailed", "Scene preload cook failed to read '%#'.", scenePath );
                    return CookStep::Failed;
                }
                Scene source{ "ScenePreload.Source", services };
                source.setBuildAllDataLayers(); // 끈 레이어의 에셋도 쿠킹본이 켜면 쓴다
                if ( source.instantiate( doc ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookCould", "Scene preload cook could not build '%#'.", scenePath );
                    return CookStep::Failed;
                }
                vector<string> listPath;
                ScenePreloadList::collect( key, *source.getObjectManager(), listPath );
                if ( ScenePreloadList::write( outputPath, listPath ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneCooker.ScenePreloadCookFailed2", "Scene preload cook failed to write '%#'.", outputPath );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 SceneCooker::cookEntityState( const SceneEngineServices& services, SceneDocument& inoutDoc, uint32* pOutMissingComponentCount )
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
        verify.setBuildAllDataLayers();
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

    uint32 SceneCooker::cookAllScenes( const SceneEngineServices& services, string_view sourceRoot, string_view cookedDir, const CookStore& store,
                                       uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookNeedsOutput", "Scene cook needs an output directory (--cooked-dir)." );
            ++outFailedCount;
            return 0;
        }

        const string resourceRoot( sourceRoot );
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

        vector<string> listSceneFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listSceneFile ) == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookCouldNot3", "Scene cook could not list '%#' - nothing is cooked", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listSceneFile );

        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      normalizedOut  = FileUtil::normalizeSeparators( cookedDir );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ).str() );
        const CookJobDesc desc           = makeCookJobDesc( registry );
        const CookJobDesc preloadDesc    = makePreloadCookJobDesc();

        // 병렬 씬 쿠킹 판단(DesignBacklog "비동기 · 병렬"): 다시 구운 씬의 시간 합과 가장 느린 씬. 합이 쿠킹 전체를 지배할 때만 캐시 격리 · 결과 병합을 설계한다.
        CookStats stats;
        int64     cookedNanoseconds  = 0;
        int64     slowestNanoseconds = 0;
        string    slowestScene;
        for ( const string& scenePath : listSceneFile )
        {
            if ( StringUtil::endsWith( scenePath, ".scene.xml", true ) == false )
                continue;
            const string relativeScene = FileUtil::normalizeSeparators( scenePath ).substr( MathUtil::min( normalizedRoot.size() + 1, scenePath.size() ) );
            if ( SceneCookerInternal::isEditorOnlyScene( relativeScene ) )
                continue;

            // 출력은 `<cookedDir>/<Resource 기준 상대 경로>` 에 같은 이름으로, 확장자만 .bin 이다.
            const string outputID   = AssetCookPath::toCookedPath( relativeScene );
            string       outputPath = normalizedOut;
            if ( outputPath.empty() == false && outputPath.back() != '/' )
                outputPath += '/';
            outputPath += outputID;

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
            const string     preloadID     = ScenePreloadList::makeListPath( outputID );
            const string     preloadPath   = ScenePreloadList::makeListPath( outputPath );
            const CookResult preloadResult = store.cookFile( preloadDesc, preloadID, preloadPath, [&]()
            { return SceneCookerInternal::cookPreloadList( SceneIterationKey{}, services, scenePath, preloadPath ); } );
            outFailedCount += preloadResult == CookResult::Failed ? 1u : 0u;
        }

        SW_LOG_INFO_ID( "SceneCooker.SceneCookReusedDDC", "Scene cook: %# reused from the DDC, %# cooked", stats._reusedCount, stats._cookedCount );
        SW_LOG_INFO_ID( "SceneCooker.SceneCookTime", "Scene cook time: %# ms over %# cooked scenes, slowest %# ms ('%#')", cookedNanoseconds / constant::time::kNanosecondsPerMillisecond,
                        stats._cookedCount, slowestNanoseconds / constant::time::kNanosecondsPerMillisecond, slowestScene );
        return stats.getWrittenCount();
    }

    bool SceneCooker::packStaticMeshes( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view scenePath )
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
        SceneCookerInternal::makeObjectByFileID( packing, mapObjectByFileID );
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
            string           anchorPrefix;
            for ( const SceneDocument::SceneObjectNode& anchor : inoutDoc._listSceneObjectNode )
            {
                if ( group._ownerFileID != 0 && anchor._fileID == group._ownerFileID )
                    anchorPrefix = anchor._name + "/";
            }
            SceneDocument::SceneObjectNode node{};
            node._name                 = anchorPrefix + "Packed" + sw::to_string( static_cast<uint64>( groupIndex ) );
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

    bool SceneCooker::splitStreamingCells( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view sceneName,
                                           vector<StreamingCellReference>* pOutListViolation )
    {
        if ( pOutListViolation != nullptr )
            pOutListViolation->clear();
        if ( inoutDoc._sceneSettings._bStreaming == false )
            return true;
        if ( inoutDoc._listStreamingCell.empty() == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.SceneCookAlreadySplit", "Scene cook: '%#' is already split into cells", sceneName );
            return false;
        }
        return SceneCookerInternal::partitionStreamingCells( services, inoutDoc, sceneName, pOutListViolation );
    }

    bool SceneCooker::previewStreamingCells( const Scene& scene, StreamingCellPreview& outPreview )
    {
        outPreview = StreamingCellPreview{};
        if ( scene.getSceneSettings()._bStreaming == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.StreamingCellPreviewDoes", "Streaming cell preview: '%#' does not stream", scene.getName().c_str() );
            return false;
        }
        SceneDocument doc{};
        if ( scene.serializeToDocument( doc ) == false )
        {
            SW_LOG_ERROR_ID( "SceneCooker.StreamingCellPreviewCould", "Streaming cell preview: '%#' could not be written as a document", scene.getName().c_str() );
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

        vector<StreamingCellReference> listViolation;
        if ( splitStreamingCells( scene.getEngineServices(), doc, scene.getName(), &listViolation ) == false )
        {
            if ( listViolation.empty() )
                return false; // 나누지 못한 이유는 쿠커가 로그에 남겼다
            for ( const StreamingCellReference& violation : listViolation )
            {
                outPreview._listViolation.push_back( StreamingCellReference{ toObjectID( violation._fromObjectID ), toObjectID( violation._toObjectID ) } );
            }
            return true;
        }
        for ( const SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
        {
            // 쿠커가 만든 엔티티(HLOD 대리 — 조각의 `_hlodFileID`)는 열린 씬에 없다 — 뺀다.
            const uint64 objectID = toObjectID( entity._fileID );
            if ( objectID != 0 )
                outPreview._listAlwaysLoadedID.push_back( objectID );
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
        desc._cookerName      = "ScenePreload";
        desc._codeFingerprint = CookCodeFingerprint::find( "ScenePreload" );
        desc._target          = "Any";
        desc._settingsHash    = 0; // 목록은 컴포넌트의 경로 프로퍼티 값이 정한다 — 타입 표가 바뀌어 프로퍼티가 달라지면 입력(씬 · 프리팹)을 다시 읽어 알게 된다
        return desc;
    }

    CookJobDesc SceneCooker::makeCookJobDesc( const TypeRegistry& registry )
    {
        CookJobDesc desc;
        desc._cookerName      = "Scene";
        desc._codeFingerprint = CookCodeFingerprint::find( "Scene" );
        desc._target          = "Any";
        // 쿠킹된 상태는 리플렉션 표로 쓴다 — 타입 · 프로퍼티가 바뀌면 같은 XML 이라도 다시 베이크한다.
        desc._settingsHash = SceneCookerInternal::computeReflectionFingerprint( registry );
        return desc;
    }

} // namespace sw
