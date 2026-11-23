#include "pch.h"

#include "Engine/Scene/SceneCellSplitter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct SceneCellSplitterInternal
        {
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
                        SW_LOG_ERROR_ID( "SceneCellSplitter.SceneCookEntityNot", "Scene cook: entity '%#' of '%#' was not built - it cannot be placed in a cell", entity._name, scenePath );
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
                    SW_LOG_ERROR_ID( "SceneCellSplitter.SceneCookEntityId", "Scene cook: '%#' - entity '%#' (id %#) refers to '%#' (id %#) in another cell; mark the target always loaded or move it", scenePath,
                                     fromIt != mapObjectByFileID.end() ? fromIt->second->getName().c_str() : "?", violation._fromObjectID,
                                     toIt != mapObjectByFileID.end() ? toIt->second->getName().c_str() : "?", violation._toObjectID );
                }
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
                    StringBuilder<constant::buffer::kMax256> cellKeyBuilder;
                    cellKeyBuilder.appendFormat( "%#,%#", static_cast<int64>( record._cell._z ), static_cast<int64>( record._cell._x ) );
                    for ( const string& layer : entity._listDataLayer )
                    {
                        cellKeyBuilder.append( '|' ).append( layer );
                    }
                    const string cellKey( cellKeyBuilder.view() );
                    const auto   found     = mapCellIndex.find( cellKey );
                    size_t       cellIndex = 0;
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
                    SW_LOG_ERROR_ID( "SceneCellSplitter.HLODTypesMissing", "Scene cook: '%#' streams but '%#' or '%#' is not in the type table - its HLOD proxies cannot be built", scenePath, kMeshTypeName,
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
                    SW_LOG_ERROR_ID( "SceneCellSplitter.HLODPropertiesMissing", "Scene cook: '%#' streams but '%#' lacks a property the HLOD builder reads", scenePath, kMeshTypeName );
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

                    SceneDocument::SceneObjectNode           proxy{};
                    StringBuilder<constant::buffer::kMax256> proxyName;
                    proxyName.appendFormat( "HLOD_%#_%#", static_cast<int64>( cell._cell._x ), static_cast<int64>( cell._cell._z ) );
                    for ( const string& layer : cell._listDataLayer )
                    {
                        proxyName.append( '_' ).append( layer );
                    }
                    proxy._name          = string( proxyName.view() );
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
                    SW_LOG_INFO_ID( "SceneCellSplitter.SceneCookHLODProxies", "Scene cook: '%#' built %# HLOD proxies for %# cells", scenePath, proxyCount,
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
                    SW_LOG_ERROR_ID( "SceneCellSplitter.SceneCookStreamsBut", "Scene cook: '%#' streams but its cell grid is not usable", scenePath );
                    return false;
                }

                Scene placement{ "SceneCellSplitter.Placement", services };
                placement.setBuildAllDataLayers();
                const bool bBuilt = placement.instantiate( inoutDoc );
                if ( bBuilt == false || placement.getUnresolvedEntityCount() > 0 )
                {
                    SW_LOG_ERROR_ID( "SceneCellSplitter.SceneCookCouldNot", "Scene cook: '%#' could not be built to place its entities in cells (%# entities without a prefab)", scenePath,
                                     static_cast<uint32>( placement.getUnresolvedEntityCount() ) );
                    return false;
                }
                unordered_map<uint64, GameObject*> mapObjectByFileID;
                placement.collectObjectsByFileID( mapObjectByFileID );
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
                SW_LOG_INFO_ID( "SceneCellSplitter.SceneCookSplitCells", "Scene cook: '%#' split into %# cells (%# entities always loaded)", scenePath, static_cast<uint32>( inoutDoc._listStreamingCell.size() ),
                                static_cast<uint32>( inoutDoc._listSceneObjectNode.size() ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SceneCellSplitter::split( const SceneEngineServices& services, SceneDocument& inoutDoc, string_view sceneName,
                                   vector<StreamingCellReference>* pOutListViolation )
    {
        if ( pOutListViolation != nullptr )
            pOutListViolation->clear();
        if ( inoutDoc._sceneSettings._bStreaming == false )
            return true;
        if ( inoutDoc._listStreamingCell.empty() == false )
        {
            SW_LOG_ERROR_ID( "SceneCellSplitter.SceneCookAlreadySplit", "Scene cook: '%#' is already split into cells", sceneName );
            return false;
        }
        return SceneCellSplitterInternal::partitionStreamingCells( services, inoutDoc, sceneName, pOutListViolation );
    }

    bool StreamingCellPreview::build( const Scene& scene, StreamingCellPreview& outPreview )
    {
        outPreview = StreamingCellPreview{};
        if ( scene.getSceneSettings()._bStreaming == false )
        {
            SW_LOG_ERROR_ID( "SceneCellSplitter.StreamingCellPreviewDoes", "Streaming cell preview: '%#' does not stream", scene.getName().c_str() );
            return false;
        }
        SceneDocument doc{};
        if ( scene.serializeToDocument( doc ) == false )
        {
            SW_LOG_ERROR_ID( "SceneCellSplitter.StreamingCellPreviewCould", "Streaming cell preview: '%#' could not be written as a document", scene.getName().c_str() );
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
        if ( SceneCellSplitter::split( scene.getEngineServices(), doc, scene.getName(), &listViolation ) == false )
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
} // namespace sw
