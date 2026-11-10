#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Object/Prefab/PrefabPathStack.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

/**
 * @file PrefabAsset.cpp
 * @brief 프리팹 에셋의 형식 셋(XML · JSON · PFB2 바이너리) 읽기 · 쓰기와 변형 · 중첩 노드 표현을 구현합니다.
 * @details 로드 캐시 · 스폰 · 펼치기 · 쿠킹은 `PrefabCache.cpp` 다(언리얼이 에셋 직렬화와 스폰을 `UObject` 로드 · `SpawnActor` 로 가르는 나눔).
 */

namespace sw
{
    namespace
    {
        struct PrefabAssetInternal
        {
            static constexpr const utf8* kRoot           = "Prefab";
            static constexpr const utf8* kName           = "name";
            static constexpr const utf8* kGameObject     = "GameObject";
            static constexpr const utf8* kNestedInstance = "PrefabInstance";
            static constexpr const utf8* kNestedPrefab   = "prefab";
            /** @brief 지운 칸입니다. 중첩 원본은 경로(`prefab`)로만 가리킨다 — 이 칸이 남은 프리팹은 오류로 읽지 않는다. */
            static constexpr const utf8* kRemovedNestedGUID = "prefabGUID";
            static constexpr const utf8* kNestedAttach      = "attach";
            static constexpr const utf8* kNestedOverrides   = "overrides";
            static constexpr const utf8* kNestedList        = "_listPrefabInstance";
            static constexpr const utf8* kVariantOverrides  = "overrides"; ///< JSON 변형의 다른 점(글 하나 — 중첩 노드의 `overrides` 와 같다)
            static constexpr uint32      kBinMagic          = FourCCUtil::make( "PFB2" );
            /**
             * @brief 버전 1 부터 상태 뒤에 중첩 인스턴스 노드가 붙고, 2 부터 노드에 부품 경로(`_partPath`)가 붙고, 3 에서 노드의 GUID 칸을 지웠다(경로 하나로 가리킨다).
             *        다른 버전은 읽지 않는다(쿠킹본은 다시 쿠킹한다).
             */
            static constexpr uint32 kBinVersion = 3;

            static void collectPrefabRefsFromText( const utf8* pText, vector<string>& outListPath )
            {
                if ( StringUtil::isNullOrEmpty( pText ) )
                    return;
                if ( StringUtil::stristr( pText, ".prefab" ) == nullptr )
                    return;
                outListPath.push_back( pText );
            }

            static void collectPrefabRefsFromXML( XMLNode node, vector<string>& outListPath )
            {
                if ( node.isValid() == false )
                    return;
                collectPrefabRefsFromText( node.getText(), outListPath );
                for ( XMLAttribute attr = node.getFirstAttribute(); attr; attr = attr.getNext() )
                {
                    collectPrefabRefsFromText( attr.getValue(), outListPath );
                }
                for ( XMLNode childNode = node.findChild(); childNode; childNode = childNode.findNextSibling() )
                {
                    collectPrefabRefsFromXML( childNode, outListPath );
                }
            }

            /** @brief XML 글 하나(상태 · 오버라이드 · 펼친 상태) 안의 `.prefab` 경로를 모읍니다. */
            static void collectPrefabRefsFromXMLText( string_view xmlText, vector<string>& outListPath )
            {
                const string_view trimmed = StringUtil::trim( xmlText );
                if ( trimmed.empty() )
                    return;
                XMLDocument doc;
                if ( doc.parse( trimmed ) == false )
                    return;
                collectPrefabRefsFromXML( doc.getRoot(), outListPath );
            }

            static void collectPrefabRefsFromJSON( JSONValue value, vector<string>& outListPath )
            {
                if ( value.isValid() == false )
                    return;
                if ( value.isString() )
                {
                    const string text = value.asString();
                    collectPrefabRefsFromText( text.c_str(), outListPath );
                    return;
                }
                if ( value.isObject() )
                {
                    const vector<string> listKey = value.getMemberNames();
                    for ( const string& key : listKey )
                    {
                        collectPrefabRefsFromJSON( value.get( key ), outListPath );
                    }
                    return;
                }
                if ( value.isArray() )
                {
                    const size_t count = value.size();
                    for ( size_t index = 0; index < count; ++index )
                    {
                        collectPrefabRefsFromJSON( value.at( index ), outListPath );
                    }
                }
            }

            /** @brief @p path 가 목록에 없으면 끝에 더합니다(대소문자 · 구분자를 무시한 캐시 키로 비교한다). */
            static void appendUniquePath( const string& path, vector<string>& inoutListPath )
            {
                if ( path.empty() )
                    return;
                const string key = PrefabCache::makeCacheKey( path );
                for ( const string& existing : inoutListPath )
                {
                    if ( StringUtil::equals( PrefabCache::makeCacheKey( existing ), key, true ) )
                        return;
                }
                inoutListPath.push_back( path );
            }

            /** @brief 형식을 모르는 본문(쿠킹한 바이너리 · 직접 준 텍스트)의 형식입니다. 읽을 때 **한 번만** 부릅니다. */
            static PrefabStateFormat detectStateFormat( string_view stateData )
            {
                const string_view trimmed = StringUtil::trim( stateData );
                return ( trimmed.empty() == false && trimmed.front() == '{' ) ? PrefabStateFormat::JSON : PrefabStateFormat::XML;
            }

            static bool upgradePrefabXMLBody( string& xmlBody, PrefabStateFormat format )
            {
                if ( xmlBody.empty() )
                    return false;
                if ( format == PrefabStateFormat::JSON )
                    return true; // JSON 본문은 XML 업그레이드 대상이 아니다

                string wrapped = "<Prefab>";
                wrapped += xmlBody;
                wrapped += "</Prefab>";
                XMLDocument wrapDoc;
                if ( wrapDoc.parse( wrapped ) == false )
                    return false;
                XMLNode wrapRoot = wrapDoc.getRoot( kRoot );
                if ( wrapRoot.isValid() == false )
                    return false;
                if ( AssetXMLFormat::isCurrentXMLVersion( PrefabAsset::kAssetType, wrapRoot ) == false )
                    return false;
                XMLNode bodyNode = wrapRoot.findChild( kGameObject );
                if ( bodyNode.isValid() == false )
                    return false;
                xmlBody = bodyNode.toString();
                return true;
            }

            /**
             * @brief 노드 목록이 규칙(부모가 자기보다 앞, 원본 경로나 펼친 상태가 있음, 펼친 노드는 부품 경로가 있고 저작본 노드는 없음)을 지키면 true 입니다.
             *        어긴 노드를 오류로 알립니다.
             */
            static bool areNodesWellFormed( const vector<PrefabInstanceNode>& listNode, string_view sourceName )
            {
                for ( size_t index = 0; index < listNode.size(); ++index )
                {
                    const PrefabInstanceNode& node        = listNode[index];
                    const bool                bParentOk   = -1 <= node._parentIndex && node._parentIndex < static_cast<int32>( index );
                    const bool                bHasSource  = node.isResolved() || node._prefabPath.empty() == false;
                    const bool                bPartPathOk = node.isResolved() != node._partPath.empty();
                    const bool                bWellFormed = bParentOk && bHasSource && bPartPathOk;
                    if ( bWellFormed )
                        continue;
                    const utf8* pProblem = bParentOk == false  ? "a parent that does not come before it"
                                         : bHasSource == false ? "no prefab path"
                                                               : "a part path only a flattened (cooked) node may have, or a flattened node without one";
                    SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabInstancePrefab", "Nested prefab instance #%# ('%#') in '%#' has %# - the prefab is not loaded", static_cast<uint32>( index ), node._name, sourceName, pProblem );
                    return false;
                }
                return true;
            }

            /** @brief `<PrefabInstance>` 원소들(@p parent 의 자식)을 읽어 끝에 더합니다. 안에 둔 `<PrefabInstance>` 는 그 노드에 붙는 노드입니다. 지운 칸(`prefabGUID`)이 있으면 오류로 false 입니다. */
            [[nodiscard]] static bool readNodesFromXML( const XMLNode& parent, int32 parentIndex, vector<PrefabInstanceNode>& inoutListNode, string_view sourceName )
            {
                for ( XMLNode element = parent.findChild( kNestedInstance ); element.isValid(); element = element.findNextSibling( kNestedInstance ) )
                {
                    // 지운 칸은 옮겨 갈 곳을 알려 준다(모르는 칸 오류보다 먼저).
                    if ( element.findAttribute( kRemovedNestedGUID ) != nullptr )
                    {
                        SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabGUIDRemoved", "Nested prefab instance '%#' in '%#' still has '%#' - prefabs are referenced by path only; the prefab is not loaded",
                                         element.getAttributeText( kName ), sourceName, kRemovedNestedGUID );
                        return false;
                    }
                    PrefabInstanceParams params;
                    if ( XMLFields::readFields( element, params, sourceName ) == false )
                        return false;
                    PrefabInstanceNode node{};
                    node._name              = std::move( params._name );
                    node._prefabPath        = std::move( params._prefab );
                    node._attachKey         = std::move( params._attach );
                    node._parentIndex       = parentIndex;
                    const XMLNode overrides = element.findChild( PrefabOverrides::kRootName );
                    if ( overrides.isValid() )
                        node._overrideXML = overrides.toString();
                    inoutListNode.push_back( std::move( node ) );
                    if ( readNodesFromXML( element, static_cast<int32>( inoutListNode.size() - 1 ), inoutListNode, sourceName ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 부모가 @p parentIndex 인 노드를 `<PrefabInstance>` 로 씁니다(그 노드에 붙는 노드는 안에). 오버라이드가 XML 이 아니면 false 입니다. */
            [[nodiscard]] static bool writeNodesToXML( XMLNode parent, const vector<PrefabInstanceNode>& listNode, int32 parentIndex )
            {
                for ( size_t index = 0; index < listNode.size(); ++index )
                {
                    const PrefabInstanceNode& node = listNode[index];
                    if ( node._parentIndex != parentIndex )
                        continue;
                    XMLNode element = parent.appendChild( kNestedInstance );
                    XMLFields::writeFields( element, PrefabInstanceParams{ node._name, node._prefabPath, node._attachKey } );
                    if ( node._overrideXML.empty() == false )
                    {
                        XMLDocument overrideDoc;
                        if ( overrideDoc.parse( node._overrideXML ) == false || overrideDoc.getRoot().isValid() == false )
                        {
                            SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabInstanceOverrides", "Nested prefab instance '%#' has overrides that are not XML", node._name );
                            return false;
                        }
                        element.appendClone( overrideDoc.getRoot() );
                    }
                    if ( writeNodesToXML( element, listNode, static_cast<int32>( index ) ) == false )
                        return false;
                }
                return true;
            }

            /** @brief JSON 배열 `_listPrefabInstance`(@p parent 의 멤버)의 노드를 읽어 끝에 더합니다. 객체마다 같은 이름의 배열이 그 노드에 붙는 노드입니다. 지운 칸(`prefabGUID`)이 있으면 오류로 false 입니다. */
            [[nodiscard]] static bool readNodesFromJSON( const JSONValue& parent, int32 parentIndex, vector<PrefabInstanceNode>& inoutListNode, string_view sourceName )
            {
                const JSONValue arrayValue = parent.get( kNestedList );
                if ( arrayValue.isArray() == false )
                    return true;
                const size_t count = arrayValue.size();
                for ( size_t index = 0; index < count; ++index )
                {
                    const JSONValue element = arrayValue.at( index );
                    if ( element.isObject() == false )
                        continue;
                    PrefabInstanceNode node{};
                    node._name        = element.get( kName ).asString();
                    node._prefabPath  = element.get( kNestedPrefab ).asString();
                    node._attachKey   = element.get( kNestedAttach ).asString();
                    node._overrideXML = element.get( kNestedOverrides ).asString();
                    node._parentIndex = parentIndex;
                    if ( element.has( kRemovedNestedGUID ) )
                    {
                        SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabGUIDRemovedJSON", "Nested prefab instance '%#' in '%#' still has '%#' - prefabs are referenced by path only; the prefab is not loaded",
                                         node._name, sourceName, kRemovedNestedGUID );
                        return false;
                    }
                    inoutListNode.push_back( std::move( node ) );
                    if ( readNodesFromJSON( element, static_cast<int32>( inoutListNode.size() - 1 ), inoutListNode, sourceName ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 부모가 @p parentIndex 인 노드를 @p parent 의 배열 `_listPrefabInstance` 로 씁니다. 그런 노드가 없으면 배열을 만들지 않습니다. */
            static void writeNodesToJSON( const JSONValue& parent, const vector<PrefabInstanceNode>& listNode, int32 parentIndex )
            {
                JSONValue arrayValue{};
                for ( size_t index = 0; index < listNode.size(); ++index )
                {
                    const PrefabInstanceNode& node = listNode[index];
                    if ( node._parentIndex != parentIndex )
                        continue;
                    if ( arrayValue.isValid() == false )
                    {
                        arrayValue = parent.set( kNestedList );
                        arrayValue.setArray();
                    }
                    const JSONValue element = arrayValue.pushBack();
                    element.setObject();
                    if ( node._name.empty() == false )
                        element.set( kName ).setString( node._name );
                    element.set( kNestedPrefab ).setString( node._prefabPath );
                    if ( node._attachKey.empty() == false )
                        element.set( kNestedAttach ).setString( node._attachKey );
                    if ( node._overrideXML.empty() == false )
                        element.set( kNestedOverrides ).setString( node._overrideXML );
                    writeNodesToJSON( element, listNode, static_cast<int32>( index ) );
                }
            }

            /**
             * @brief 저장할 XML 본문입니다 — 형식 기본값과 같은 칸을 뺀 글이고, 빼고 읽은 값이 뺀 전과 같을 때만 그것을 씁니다.
             * @details 대조는 데이터를 지키는 자리다. 칸의 초기값이 다른 칸에 달려 있으면(접근자 칸) 뺀 글이 다르게 읽힐 수 있다 — 그때는 모든 칸을 쓰고 경고한다.
             *          shortcut: JSON 저장은 칸을 빼지 않는다, JSON 프리팹이 생기면 같은 비교를 JSON 값에 더한다.
             */
            static string makeBodyWithoutDefaults( const PrefabAsset& asset, const string& fullBody )
            {
                string trimmedBody;
                if ( fullBody.empty() || PrefabOverrides::omitDefaultValues( fullBody, trimmedBody ) == false )
                    return fullBody;
                PrefabAsset trimmedAsset;
                string      fullState;
                string      trimmedState;
                if ( trimmedAsset.setStateForCompare( asset.getName(), trimmedBody ) && PrefabOverrides::makeBaseState( asset, fullState ) && PrefabOverrides::makeBaseState( trimmedAsset, trimmedState ) && fullState == trimmedState )
                    return trimmedBody;
                SW_LOG_WARNING_ID( "PrefabAsset.DefaultOmitMismatch", "Prefab '%#' reads differently without its default values - all fields are written", asset.getName() );
                return fullBody;
            }

            /**
             * @brief 변형의 노드 목록을 물려받은 것과 자기 것으로 나눕니다. 물려받은 노드를 고친 것은 그 노드의 `<Part>` 가 됩니다.
             * @details 지우거나 옮긴 물려받은 노드는 파일로 적을 수 없어 오류다(유니티 Prefab Variant 도 물려받은 자식을 지우지 못한다). 자기 노드는 물려받은 노드 아래에 둘 수 없다 — 파일에서 노드는 자기 노드 안에만 중첩한다.
             */
            [[nodiscard]] static bool splitVariantNodes( const PrefabAsset& base, const vector<PrefabInstanceNode>& listNode, string_view prefabName, vector<PrefabInstanceNode>& outListOwnNode, vector<PrefabPartOverride>& inoutListPart )
            {
                const vector<PrefabInstanceNode>& listBaseNode = base.getNestedInstances();
                const size_t                      inherited    = listBaseNode.size();
                if ( listNode.size() < inherited )
                {
                    SW_LOG_ERROR_ID( "PrefabAsset.VariantRemovedInheritedNode", "Variant '%#' removed nested instances its base has - a variant cannot remove them", prefabName );
                    return false;
                }
                PrefabCache sources;
                for ( size_t index = 0; index < inherited; ++index )
                {
                    const PrefabInstanceNode& node     = listNode[index];
                    const PrefabInstanceNode& baseNode = listBaseNode[index];
                    if ( node._prefabPath != baseNode._prefabPath || node._parentIndex != baseNode._parentIndex || node._attachKey != baseNode._attachKey )
                    {
                        SW_LOG_ERROR_ID( "PrefabAsset.VariantMovedInheritedNode", "Variant '%#' moved or replaced nested instance '%#' its base has - a variant cannot", prefabName, baseNode._name );
                        return false;
                    }
                    if ( node._overrideXML == baseNode._overrideXML )
                        continue;
                    const shared_ptr<const PrefabAsset> pSource = sources.loadPrefab( node._prefabPath );
                    string                              sourceState;
                    string                              baseNodeState;
                    string                              nodeState;
                    expected<void>                      overrides{};
                    PrefabPartOverride                  part{ to_string( static_cast<uint64>( index ) ), {} };
                    vector<PrefabPartOverride>          listDeepPart;
                    const bool                          bMade = pSource != nullptr && PrefabOverrides::makeBaseState( *pSource, sourceState ) &&
                                       PrefabOverrides::makeInstanceState( sourceState, baseNode._overrideXML, node._name, baseNodeState ) &&
                                       PrefabOverrides::makeInstanceState( sourceState, node._overrideXML, node._name, nodeState ) &&
                                       ( overrides = PrefabOverrides::computeOverrides( nodeState, baseNodeState, part._overrideXML ) ).has_value() &&
                                       PrefabOverrides::collectPartOverrides( node._overrideXML, listDeepPart );
                    if ( bMade == false )
                    {
                        SW_LOG_ERROR_ID( "PrefabAsset.VariantNodeDeltaFailed", "Variant '%#': the difference of nested instance '%#' could not be made (%#)", prefabName, node._name,
                                         overrides.has_value() ? string_view{} : overrides.error().detail() );
                        return false;
                    }
                    if ( part._overrideXML.empty() == false )
                        inoutListPart.push_back( std::move( part ) );
                    for ( PrefabPartOverride& deepPart : listDeepPart )
                    {
                        deepPart._partPath = PrefabPartOverride::joinPath( to_string( static_cast<uint64>( index ) ), deepPart._partPath );
                        inoutListPart.push_back( std::move( deepPart ) );
                    }
                }
                outListOwnNode.clear();
                for ( size_t index = inherited; index < listNode.size(); ++index )
                {
                    PrefabInstanceNode node = listNode[index];
                    if ( node._parentIndex >= 0 && static_cast<size_t>( node._parentIndex ) < inherited )
                    {
                        SW_LOG_ERROR_ID( "PrefabAsset.VariantNodeUnderInherited", "Variant '%#': nested instance '%#' hangs under an inherited one - a variant adds nodes only at its root or under its own", prefabName, node._name );
                        return false;
                    }
                    if ( node._parentIndex >= 0 )
                        node._parentIndex -= static_cast<int32>( inherited );
                    outListOwnNode.push_back( std::move( node ) );
                }
                return true;
            }

            /** @brief 노드로 담은 부품 오브젝트와 그 노드 인덱스입니다(`captureNestedParts`). */
            struct NestedPartEntry
            {
                const GameObject* _pObject;
                int32             _nodeIndex;
            };

            /**
             * @brief @p root 가 스폰한 부품(`PrefabInstanceRegistry`)을 중첩 인스턴스 노드로 만듭니다 — 원본 프리팹과 다른 점이 오버라이드가 됩니다.
             * @details 부모 노드가 자식 노드보다 앞에 오도록 레지스트리의 기록 순서(스폰 순서)로 돈다. 사용자가 옮긴 부품 · 원본을 읽지 못한 부품은 싣지 않는다.
             */
            static void captureNestedParts( const GameObject& root, PrefabCache* pPrefabs, vector<PrefabInstanceNode>& outListNode )
            {
                outListNode.clear();
                GameObjectManager* pManager = root.getManager();
                if ( pManager == nullptr )
                    return;
                const PrefabInstanceRegistry& registry = pManager->getSubsystem<PrefabInstanceRegistry>();
                vector<uint64>                listPartID;
                registry.collectNestedPartsOf( root.getObjectID(), listPartID );
                if ( listPartID.empty() )
                    return;
                if ( pPrefabs == nullptr )
                {
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstancesBut", "'%#' has nested prefab instances but no prefab cache is given - they are not written to the prefab", root.getName().c_str() );
                    return;
                }

                vector<NestedPartEntry> listCaptured;
                for ( const uint64 partID : listPartID )
                {
                    const GameObject* pPart        = pManager->findGameObjectByID( partID );
                    const bool        bForeignPart = pPart == nullptr || pPart->isPendingDestroy() || registry.isAttachedNestedPart( pPart ) == false;
                    if ( bForeignPart )
                        continue; // 지운 부품 · 옮긴 부품은 이 프리팹의 것이 아니다
                    const PrefabNestedPart* pRecord = registry.findNestedPart( partID );

                    PrefabInstanceNode    node{};
                    const SceneComponent* pPartRoot = pPart->getPrimarySceneComponent();
                    const GameObject*     pParent   = pPartRoot != nullptr ? pPart->getParent() : &root;
                    if ( pParent != &root )
                    {
                        bool bParentCaptured = false;
                        for ( const NestedPartEntry& entry : listCaptured )
                        {
                            if ( entry._pObject != pParent )
                                continue;
                            node._parentIndex = entry._nodeIndex;
                            bParentCaptured   = true;
                        }
                        if ( bParentCaptured == false )
                        {
                            SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceHangs", "Nested prefab instance '%#' hangs under '%#', which is not part of '%#' - it is not written to the prefab", pPart->getName().c_str(),
                                               pParent != nullptr ? pParent->getName().c_str() : "?", root.getName().c_str() );
                            continue;
                        }
                    }
                    const SceneComponent* pAttachedTo               = pPartRoot != nullptr ? pPartRoot->getParent() : nullptr;
                    const bool            bAttachedToOtherComponent = pAttachedTo != nullptr && pParent != nullptr && pAttachedTo != pParent->getPrimarySceneComponent();
                    if ( bAttachedToOtherComponent )
                        node._attachKey = ComponentStableKey::makeKey( pAttachedTo );
                    node._name       = pPart->getName().c_str();
                    node._prefabPath = pRecord->_prefabPath;

                    const shared_ptr<const PrefabAsset> pSource = pPrefabs->loadPrefab( node._prefabPath );
                    string                              baseState;
                    if ( pSource == nullptr || PrefabOverrides::makeBaseState( *pSource, baseState ) == false )
                    {
                        SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot", "Nested prefab '%#' of '%#' could not be read - instance '%#' is not written to the prefab", node._prefabPath, root.getName().c_str(),
                                           node._name );
                        continue;
                    }
                    ObjectSaveOptions options{ SerializePurpose::Authoring };
                    options._bOmitExternalParent = SW_TRUE; // 부모는 노드의 자리(부모 노드 · attach)가 적는다
                    // 부품 아래 부품(원본이 스폰하는 것)을 고친 것은 노드의 `<Part>` 로 싣는다 — 노드로 담지 않으므로 싣지 않으면 Apply 가 버린다.
                    vector<PrefabPartOverride> listDeepPart;
                    const expected<void>       overrides = PrefabOverrides::computeOverrides( ObjectStateSerializer::saveToXMLString( pPart, options ), baseState, node._overrideXML );
                    const bool                 bOverridesMade =
                        overrides.has_value() && pPrefabs->collectPartOverrides( *pPart, *pSource, listDeepPart ) && PrefabOverrides::appendPartOverrides( node._overrideXML, listDeepPart );
                    if ( bOverridesMade == false )
                    {
                        SW_LOG_WARNING_ID( "PrefabAsset.OverridesNestedPrefabInstance", "Overrides of nested prefab instance '%#' could not be computed (%#) - it is not written to the prefab", node._name,
                                           overrides.has_value() ? string_view{ "its nested part overrides could not be collected" } : overrides.error().message() );
                        continue;
                    }
                    listCaptured.push_back( NestedPartEntry{ pPart, static_cast<int32>( outListNode.size() ) } );
                    outListNode.push_back( std::move( node ) );
                }
            }
        };

        /**
         * @brief 이 스레드가 변형을 읽으며 따라가는 부모 경로입니다(순환 · 깊이 검사, `PrefabAsset::resolveVariant`).
         * @details 재진입하는 호출 사슬(변형의 부모를 읽다 그 부모의 부모를 읽는다)의 기록이라 공유 상태가 아니라 사슬을 도는 스레드의 상태다.
         */
        thread_local PrefabPathStack::List t_variantStack;
    } // namespace
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( Prefab, PrefabAsset::kAssetType );

    string PrefabInstanceNode::makePartKey( size_t index ) const
    {
        return isResolved() ? _partPath : to_string( static_cast<uint64>( index ) );
    }

    PrefabAsset::PrefabAsset()
        : _name{}
        , _stateData{}
        , _basePath{}
        , _variantOverrideXML{}
        , _listNestedInstance{}
        , _listVariantPart{}
        , _stateFormat{ PrefabStateFormat::XML }
        , _bValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool PrefabAsset::loadFromXMLFile( const AssetID& assetID )
    {
        AssetLoadScope loadScope( "Prefab", assetID.view() );
        XMLDocument    doc;
        string         absPath;
        const bool     bRead = doc.loadResource( assetID, &absPath );
        return readXMLSource( doc, bRead, absPath, loadScope );
    }

    bool PrefabAsset::loadFromJSONFile( const AssetID& assetID )
    {
        JSONDocument doc;
        string       absPath;
        const bool   bRead = doc.loadResource( assetID, &absPath );
        return readJSONSource( doc, bRead, absPath );
    }

    bool PrefabAsset::loadFromDiskFile( string_view diskPath )
    {
        // 형식은 확장자 하나로 정한다 — `saveToFile` · 쿠커와 같은 규칙.
        // 디스크 파일은 부르는 쪽이 이름(`loadFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        if ( StringUtil::endsWith( diskPath, ".prefab.json", true ) )
        {
            JSONDocument doc;
            const bool   bRead = doc.loadFromFile( diskPath );
            return readJSONSource( doc, bRead, string( diskPath ) );
        }
        if ( StringUtil::endsWith( diskPath, ".prefab.xml", true ) )
        {
            AssetLoadScope loadScope( "Prefab", diskPath );
            XMLDocument    doc;
            const bool     bRead = doc.loadDataFile( diskPath ); // 머리의 조건 · 층 단계는 리소스 읽기와 같은 길을 지난다. 부모(`_base`)는 `resolveVariant` 가 합친다
            return readXMLSource( doc, bRead, string( diskPath ), loadScope );
        }
        SW_LOG_ERROR_ID( "PrefabAsset.NotPrefabSourceFile", "Not a prefab source file (.prefab.xml / .prefab.json): %#", diskPath );
        return false;
    }

    bool PrefabAsset::readXMLSource( XMLDocument& doc, const bool bRead, const string& absPath, AssetLoadScope& loadScope )
    {
        _bValid = SW_FALSE;
        if ( bRead == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabNotLoaded", "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Decode );

        XMLNode root = doc.getRoot( PrefabAssetInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.MissingPrefabRoot", "Missing <Prefab> root: %#", absPath );
            return false;
        }
        if ( AssetXMLFormat::isCurrentXMLVersion( PrefabAsset::kAssetType, root ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.FormatVersionMismatch", "formatVersion mismatch: %#", absPath );
            return false;
        }

        PrefabRootParams rootParams;
        if ( XMLFields::readFields( root, rootParams, absPath, { "formatVersion", DataInheritance::kBaseAttribute } ) == false )
            return false;
        if ( rootParams._name.empty() == false )
            _name = rootParams._name;
        if ( _name.empty() )
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        vector<PrefabInstanceNode> listNode;
        if ( PrefabAssetInternal::readNodesFromXML( root, -1, listNode, absPath ) == false || PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;

        const XMLNode     bodyNode = root.findChild( PrefabAssetInternal::kGameObject );
        const string_view basePath = root.getAttributeText( DataInheritance::kBaseAttribute );
        if ( basePath.empty() )
        {
            if ( bodyNode.isValid() == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.MissingGameObjectPrefab", "Missing <GameObject> in <Prefab>: %#", absPath );
                return false;
            }
            _stateData          = bodyNode.toString();
            _listNestedInstance = std::move( listNode );
        }
        else
        {
            if ( bodyNode.isValid() )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.VariantHasBody", "Prefab variant has both %# and <GameObject> - it writes <%#> only: %#", DataInheritance::kBaseAttribute, PrefabOverrides::kRootName, absPath );
                return false;
            }
            const XMLNode overrides = root.findChild( PrefabOverrides::kRootName );
            if ( resolveVariant( basePath, overrides.isValid() ? overrides.toString() : string{}, std::move( listNode ), absPath ) == false )
                return false;
        }

        _stateFormat = PrefabStateFormat::XML;
        _bValid      = SW_TRUE;
        loadScope.setBytes( _stateData.size() );
        loadScope.setSucceeded();
        SW_LOG_INFO_ID( "PrefabAsset.Loaded", "Loaded '%#' from %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::readJSONSource( JSONDocument& doc, const bool bRead, const string& absPath )
    {
        _bValid = SW_FALSE;
        if ( bRead == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabNotLoaded", "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }

        // 저장기가 쓰는 모양 하나 — GameObject 상태 JSON 그대로다(이름은 그 `_name`). 중첩 인스턴스(`_listPrefabInstance`)는 상태에서 떼어 둔다.
        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabJSONNotGameObject", "Prefab JSON is not a GameObject state object: %#", absPath );
            return false;
        }
        _name = root.get( "_name" ).asString();

        vector<PrefabInstanceNode> listNode;
        if ( PrefabAssetInternal::readNodesFromJSON( root, -1, listNode, absPath ) == false || PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;

        // 변형은 XML 과 같다 — 부모 경로와 다른 점(글 하나)만 적고, 읽으면 상태는 XML 이다(`makeInstanceState`).
        if ( root.has( DataInheritance::kBaseAttribute ) )
        {
            if ( _name.empty() )
                _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );
            const string basePath  = root.get( DataInheritance::kBaseAttribute ).asString();
            const string overrides = root.get( PrefabAssetInternal::kVariantOverrides ).asString();
            if ( resolveVariant( basePath, overrides, std::move( listNode ), absPath ) == false )
                return false;
            _stateFormat = PrefabStateFormat::XML;
            _bValid      = SW_TRUE;
            return true;
        }
        _listNestedInstance = std::move( listNode );

        if ( root.has( PrefabAssetInternal::kNestedList ) )
        {
            JSONDocument    stateDoc;
            const JSONValue stateRoot = stateDoc.makeObject();
            for ( const string& key : root.getMemberNames() )
            {
                if ( key != PrefabAssetInternal::kNestedList )
                    stateRoot.set( key ).assignFrom( root.get( key ) );
            }
            _stateData = stateDoc.dump( 0 );
        }
        else
        {
            _stateData = doc.dump( 0 );
        }

        if ( _name.empty() )
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        _stateFormat = PrefabStateFormat::JSON;
        _bValid      = SW_TRUE;
        return true;
    }

    bool PrefabAsset::resolveVariant( const string_view basePath, const string_view overrideXML, vector<PrefabInstanceNode> listOwnNode, const string_view sourceName )
    {
        PrefabPathStack::List& stack = t_variantStack;
        const string           baseID{ basePath };
        const hashed_string    baseKey = PrefabPathStack::makeKey( PrefabCache::makeCacheKey( baseID ) );
        if ( VectorUtil::contains( stack, baseKey ) )
        {
            DataInheritance::reportCycle( basePath, sourceName );
            return false;
        }
        if ( stack.size() >= DataInheritance::getMaxDepth() )
        {
            DataInheritance::reportTooDeep( basePath, sourceName );
            return false;
        }
        const PrefabPathStack::Scope scope{ stack, baseKey };

        // 부모는 캐시를 거치지 않고 바로 읽는다 — 자산이 합친 값을 들고 있으므로 부모를 들고 있을 일이 없고, 쿠킹 작업의 입력도 이 읽기로 남는다.
        PrefabAsset base;
        const bool  bLoaded = FileUtil::hasExtension( baseID, ".json" ) ? base.loadFromJSONFile( AssetID{ baseID } ) : base.loadFromXMLFile( AssetID{ baseID } );
        if ( bLoaded == false )
        {
            DataInheritance::reportBaseNotFound( basePath, sourceName );
            return false;
        }
        string convertedBase;
        if ( base.getStateFormat() != PrefabStateFormat::XML && PrefabOverrides::makeBaseState( base, convertedBase ) == false )
            return false;
        const string_view baseState = base.getStateFormat() == PrefabStateFormat::XML ? string_view( base.getStateData() ) : string_view( convertedBase );

        vector<PrefabPartOverride> listOwnPart;
        if ( PrefabOverrides::checkOverrideTargets( baseState, overrideXML, sourceName ) == false || PrefabOverrides::collectPartOverrides( overrideXML, listOwnPart ) == false )
            return false;
        if ( PrefabOverrides::makeInstanceState( baseState, overrideXML, _name, _stateData ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.VariantStateFailed", "Prefab variant '%#': its differences could not be applied to '%#'", sourceName, basePath );
            return false;
        }
        _basePath           = baseID;
        _variantOverrideXML = string{ overrideXML };
        _listVariantPart    = base.getVariantPartOverrides();
        _listVariantPart.insert( _listVariantPart.end(), listOwnPart.begin(), listOwnPart.end() );

        // 부모 노드가 앞이고 자기 노드의 부모 인덱스는 그만큼 밀린다 — 부품 경로(`<Part>` 의 `2/0`)가 부모와 같은 번호를 유지한다.
        _listNestedInstance            = base.getNestedInstances();
        const int32 inheritedNodeCount = static_cast<int32>( _listNestedInstance.size() );
        for ( PrefabInstanceNode& node : listOwnNode )
        {
            if ( node._parentIndex >= 0 )
                node._parentIndex += inheritedNodeCount;
            _listNestedInstance.push_back( std::move( node ) );
        }
        return true;
    }

    bool PrefabAsset::makeVariantFileFields( string& outOverrideXML, vector<PrefabInstanceNode>& outListOwnNode ) const
    {
        outOverrideXML.clear();
        PrefabAsset base;
        const bool  bLoaded = FileUtil::hasExtension( _basePath, ".json" ) ? base.loadFromJSONFile( AssetID{ _basePath } ) : base.loadFromXMLFile( AssetID{ _basePath } );
        if ( bLoaded == false )
        {
            DataInheritance::reportBaseNotFound( _basePath, _name );
            return false;
        }
        // 두 쪽 모두 같은 직렬화기로 쓴 글을 맞댄다 — 손으로 쓴 원문과 맞대면 같은 값도 글자가 달라 다른 칸으로 남는다.
        string         selfState;
        string         baseState;
        expected<void> overrides{};
        if ( PrefabOverrides::makeBaseState( *this, selfState ) == false || PrefabOverrides::makeBaseState( base, baseState ) == false ||
             ( overrides = PrefabOverrides::computeOverrides( selfState, baseState, outOverrideXML ) ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.VariantDeltaFailed", "Prefab variant '%#': its differences from '%#' could not be made (%#)", _name, _basePath,
                             overrides.has_value() ? string_view{} : overrides.error().message() );
            return false;
        }
        vector<PrefabPartOverride> listPart;
        for ( const PrefabPartOverride& part : _listVariantPart )
        {
            // 부모에게서 물려받은 부품 덮어쓰기는 부모가 적는다.
            const bool bInherited = VectorUtil::containsIf( base.getVariantPartOverrides(), [&part]( const PrefabPartOverride& basePart )
            { return basePart._partPath == part._partPath && basePart._overrideXML == part._overrideXML; } );
            if ( bInherited == false )
                listPart.push_back( part );
        }
        if ( PrefabAssetInternal::splitVariantNodes( base, _listNestedInstance, _name, outListOwnNode, listPart ) == false )
            return false;
        return PrefabOverrides::appendPartOverrides( outOverrideXML, listPart );
    }

    bool PrefabAsset::setStateForCompare( const string_view name, const string_view stateXML )
    {
        _name        = string{ name };
        _stateData   = string{ stateXML };
        _stateFormat = PrefabStateFormat::XML;
        _bValid      = _stateData.empty() ? SW_FALSE : SW_TRUE;
        return _bValid == SW_TRUE;
    }

    string PrefabAsset::readBasePathFromSource( string_view sourceText )
    {
        const string_view trimmed = StringUtil::trim( sourceText );
        if ( trimmed.empty() || trimmed.front() != '{' )
            return DataInheritance::findBasePath( sourceText );
        JSONDocument doc;
        if ( doc.parse( trimmed ) == false || doc.getRoot().isObject() == false || doc.getRoot().has( DataInheritance::kBaseAttribute ) == false )
            return {};
        return doc.getRoot().get( DataInheritance::kBaseAttribute ).asString();
    }

    bool PrefabAsset::loadFromBinaryFile( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        _bValid                             = SW_FALSE;
        // 쿠킹한 프리팹은 팩에 든다 — 디스크 경로로 열지 않고 `AssetIO` 로 읽는다(낱개 파일 허용 정책도 거기서 본다).
        const string  absPath{ assetRelativePath };
        vector<uint8> bytes;
        Archive       arch;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) )
            arch = Archive( bytes.data(), bytes.size() );
        if ( arch.getSize() < 12 )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.BinaryReadFailedToo", "Binary read failed or too small: %#", absPath );
            return false;
        }

        uint32 magic{ 0 };
        arch >> magic;
        if ( magic != PrefabAssetInternal::kBinMagic )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.BadBinaryMagic", "Bad binary magic: %#", absPath );
            return false;
        }

        uint32 version{ 0 };
        arch >> version;
        if ( version != PrefabAssetInternal::kBinVersion )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.CookedPrefabVersionNot", "Cooked prefab version %# is not %# in %# - cook the prefabs again", version, PrefabAssetInternal::kBinVersion, absPath );
            return false;
        }

        // 노드 하나는 글 여섯의 길이와 부모 번호다 — 파일 크기만큼의 노드(한 개가 수백 바이트)를 잡지 않는다.
        constexpr uint32 kMinBytesPerNode = sizeof( uint32 ) * 6 + sizeof( int32 );
        uint32           nodeCount{ 0 };
        arch >> _name >> _stateData;
        if ( arch.isError() || arch.readCount( kMinBytesPerNode, nodeCount ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.BinaryPrefabStreamCorrupted", "Binary prefab stream corrupted in %#", absPath );
            return false;
        }
        vector<PrefabInstanceNode> listNode( nodeCount );
        for ( PrefabInstanceNode& node : listNode )
        {
            arch >> node._name >> node._prefabPath >> node._attachKey >> node._overrideXML >> node._resolvedStateXML >> node._partPath >> node._parentIndex;
        }
        if ( arch.isError() )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.BinaryPrefabStreamCorrupted", "Binary prefab stream corrupted in %#", absPath );
            return false;
        }
        if ( PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;
        _listNestedInstance = std::move( listNode );

        _stateFormat = PrefabAssetInternal::detectStateFormat( _stateData );
        if ( PrefabAssetInternal::upgradePrefabXMLBody( _stateData, _stateFormat ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.FormatVersionMismatch", "formatVersion mismatch: %#", absPath );
            return false;
        }

        _bValid = SW_TRUE;
        SW_LOG_INFO_ID( "PrefabAsset.LoadedBinary", "Loaded '%#' from binary %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToXMLFile( const AbsolutePath& absPath ) const
    {
        if ( hasResolvedNestedInstances() )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabHoldsCookedResolved", "Prefab '%#' holds cooked (resolved) nested instances and cannot be written as a source file - '%#' is left as it was", _name,
                             absPath.str() );
            return false;
        }

        XMLDocument xmlDoc;
        XMLNode     root = xmlDoc.appendRoot( PrefabAssetInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( PrefabAsset::kAssetType._formatVersion ) );
        if ( _basePath.empty() == false )
            root.appendAttribute( DataInheritance::kBaseAttribute, string_view( _basePath ) );
        XMLFields::writeFields( root, PrefabRootParams{ _name } );

        // 변형은 부모와 다른 점만 쓴다. 다른 점을 만들지 못하면 쓰지 않는다 — 모든 칸을 적어 부모를 끊지 않는다.
        if ( _basePath.empty() == false )
        {
            string                     overrideXML;
            vector<PrefabInstanceNode> listOwnNode;
            XMLDocument                overrideDoc;
            const bool                 bFields = makeVariantFileFields( overrideXML, listOwnNode );
            if ( bFields && overrideXML.empty() == false && overrideDoc.parse( overrideXML ) )
                (void)root.appendClone( overrideDoc.getRoot() );
            if ( bFields == false || PrefabAssetInternal::writeNodesToXML( root, listOwnNode, -1 ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabVariantNotWritten", "Prefab variant '%#' could not be written - '%#' is left as it was", _name, absPath.str() );
                return false;
            }
            if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false || xmlDoc.saveToFile( absPath.str() ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.FailedWritePrefabXML2", "Failed to write prefab XML: %#", absPath.str() );
                return false;
            }
            SW_LOG_INFO_ID( "PrefabAsset.SavedVariant", "Saved variant '%#' of '%#' -> %#", _name, _basePath, absPath.str() );
            return true;
        }

        const string fullBody = ( _stateFormat == PrefabStateFormat::XML ) ? _stateData : convertState( PrefabStateFormat::XML );
        const string xmlBody  = PrefabAssetInternal::makeBodyWithoutDefaults( *this, fullBody );
        if ( xmlBody.empty() == false )
        {
            XMLDocument bodyDoc;
            const bool  bBodyParsed = bodyDoc.parse( xmlBody );
            XMLNode     bodyRoot    = bBodyParsed ? bodyDoc.getRoot() : XMLNode{};
            if ( bodyRoot.isValid() == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabStateCannotWritten", "Prefab '%#' has a state that cannot be written as XML - '%#' is left as it was", _name, absPath.str() );
                return false;
            }
            root.appendClone( bodyRoot );
        }
        else if ( _stateData.empty() == false )
        {
            // 상태가 있는데 XML 로 옮기지 못했다(타입이 빠진 JSON 본문 등). 빈 `<Prefab>` 을 쓰면 파일의 내용이 사라지므로 실패로 돌려준다.
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabCouldNotConverted", "Prefab '%#' could not be converted to XML - '%#' is left as it was", _name, absPath.str() );
            return false;
        }
        if ( PrefabAssetInternal::writeNodesToXML( root, _listNestedInstance, -1 ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabNestedInstancesCannot", "Prefab '%#' has nested instances that cannot be written - '%#' is left as it was", _name, absPath.str() );
            return false;
        }

        // 상위 폴더를 만들지 못하면 쓰지 않는다(폴더 쪽 이유는 ensure 가 남긴다). 실패는 로그로 남긴다(SceneDocument::saveToFile 과 같은 형태).
        const bool writeOk = FileUtil::ensureParentDirectoryExists( absPath.str() ) && xmlDoc.saveToFile( absPath.str() );
        if ( writeOk == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.FailedWritePrefabXML", "Failed to write prefab XML: %#", absPath.str() );
            return false;
        }

        SW_LOG_INFO_ID( "PrefabAsset.Saved", "Saved '%#' -> %#", _name, absPath.str() );
        return true;
    }

    bool PrefabAsset::saveToJSONFile( const AbsolutePath& absPath ) const
    {
        if ( hasResolvedNestedInstances() )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabHoldsCookedResolved2", "Prefab '%#' holds cooked (resolved) nested instances and cannot be written as a source file; nothing written: %#", _name, absPath.str() );
            return false;
        }

        if ( _basePath.empty() == false )
        {
            string                     overrideXML;
            vector<PrefabInstanceNode> listOwnNode;
            if ( makeVariantFileFields( overrideXML, listOwnNode ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabVariantNotWrittenJSON", "Prefab variant '%#' could not be written; nothing written: %#", _name, absPath.str() );
                return false;
            }
            JSONDocument    variantDoc;
            const JSONValue variantRoot = variantDoc.makeObject();
            variantRoot.set( "_name" ).setString( _name );
            variantRoot.set( DataInheritance::kBaseAttribute ).setString( _basePath );
            if ( overrideXML.empty() == false )
                variantRoot.set( PrefabAssetInternal::kVariantOverrides ).setString( overrideXML );
            PrefabAssetInternal::writeNodesToJSON( variantRoot, listOwnNode, -1 );
            const string variantText = variantDoc.dump( 0 );
            if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false || FileUtil::writeFile( absPath.str(), reinterpret_cast<const uint8*>( variantText.data() ), variantText.size() ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.FailedWritePrefabJSON2", "Failed to write prefab JSON: %#", absPath.str() );
                return false;
            }
            return true;
        }

        string jsonStr = ( _stateFormat == PrefabStateFormat::JSON ) ? _stateData : convertState( PrefabStateFormat::JSON );
        if ( jsonStr.empty() )
        {
            // 상태를 읽지 못했다(타입이 빠진 본문 등). 로더가 읽을 수 없는 파일을 쓰지 않는다 — XML 로는 그대로 저장할 수 있다.
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabStateCouldNot", "Prefab '%#' state could not be converted to JSON; nothing written: %#", _name, absPath.str() );
            return false;
        }
        if ( _listNestedInstance.empty() == false )
        {
            JSONDocument doc;
            if ( doc.parse( jsonStr ) == false || doc.getRoot().isObject() == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabStateNotJSON", "Prefab '%#' state is not a JSON object; nothing written: %#", _name, absPath.str() );
                return false;
            }
            PrefabAssetInternal::writeNodesToJSON( doc.getRoot(), _listNestedInstance, -1 );
            jsonStr = doc.dump( 0 );
        }

        const bool writeOk = FileUtil::ensureParentDirectoryExists( absPath.str() ) &&
                             FileUtil::writeFile( absPath.str(), reinterpret_cast<const uint8*>( jsonStr.data() ), jsonStr.size() );
        if ( writeOk == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.FailedWritePrefabJSON", "Failed to write prefab JSON: %#", absPath.str() );
            return false;
        }

        SW_LOG_INFO_ID( "PrefabAsset.SavedJSON", "Saved '%#' JSON %#", _name, absPath.str() );
        return true;
    }

    bool PrefabAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        // 형식은 경로가 정한다 — 쿠커(`cookAllPrefabs`)와 로더(`loadPrefab`)가 읽는 규칙과 같다. 늘 XML 로 쓰면 `.prefab.json` 에 XML 이
        // 들어가 그 프리팹이 다시는 읽히지 않는다. 프리팹이 아닌 경로(씬 · 머티리얼)는 덮지 않는다.
        if ( StringUtil::endsWith( absPath.str(), ".prefab.json", true ) )
            return saveToJSONFile( absPath );
        if ( StringUtil::endsWith( absPath.str(), ".prefab.xml", true ) )
            return saveToXMLFile( absPath );
        SW_LOG_ERROR_ID( "PrefabAsset.NotPrefabSourcePath", "'%#' is not a prefab source path (.prefab.xml / .prefab.json) - nothing was written", absPath.str() );
        return false;
    }

    bool PrefabAsset::saveToBinaryFile( const AbsolutePath& absPath ) const
    {

        Archive arch;
        arch << PrefabAssetInternal::kBinMagic;
        arch << PrefabAssetInternal::kBinVersion;
        arch << _name;
        arch << _stateData;
        arch << static_cast<uint32>( _listNestedInstance.size() );
        for ( const PrefabInstanceNode& node : _listNestedInstance )
        {
            arch << node._name << node._prefabPath << node._attachKey << node._overrideXML << node._resolvedStateXML << node._partPath << node._parentIndex;
        }

        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false || arch.saveToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.FailedWritePrefabBinary", "Failed to write prefab binary: %#", absPath.str() );
            return false;
        }
        return true;
    }

    void PrefabAsset::setFromGameObject( const GameObject* pGameObject, PrefabCache* pNestedPrefabs )
    {
        _listNestedInstance.clear();
        _listVariantPart.clear(); // 살아 있는 부품에 이미 얹혀 있다 — 노드의 오버라이드로 다시 담긴다
        _variantOverrideXML.clear();
        if ( pGameObject == nullptr )
        {
            _bValid = SW_FALSE;
            return;
        }
        // 프리팹 루트에는 부모가 없다 — 자식 인스턴스로 프리팹을 만들어도 원래 부모를 싣지 않는다. 실으면 그 프리팹을 스폰할 때마다
        // 그 이름의 오브젝트에 붙는다(언리얼 · 유니티의 프리팹 루트도 부모를 들지 않는다).
        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = SW_TRUE;
        _name                        = pGameObject->getName().c_str();
        _stateData                   = ObjectStateSerializer::saveToXMLString( pGameObject, options );
        _stateFormat                 = PrefabStateFormat::XML;
        _bValid                      = _stateData.empty() == false ? SW_TRUE : SW_FALSE;

        PrefabCache* pPrefabs = pNestedPrefabs;
        if ( pPrefabs == nullptr && engine::areEngineServicesBound() )
            pPrefabs = &engine::getSubsystem<AssetManager>().getAssetCache<PrefabCache>();
        PrefabAssetInternal::captureNestedParts( *pGameObject, pPrefabs, _listNestedInstance );
    }

    bool PrefabAsset::applyStateTo( GameObject* pTarget, const ObjectIdentity* pIdentity ) const
    {
        if ( pTarget == nullptr || _stateData.empty() )
            return false;
        // 프리팹 본문에 남은 다른 오브젝트로의 부착은 읽지 않는다(위 `setFromGameObject`). 오브젝트 안의 부착은 그대로 잇는다.
        ObjectLoadContext context{ SerializePurpose::Authoring };
        context._pIdentity              = pIdentity;
        context._bExternalParentAllowed = false;
        return ( _stateFormat == PrefabStateFormat::JSON ) ? ObjectStateSerializer::loadFromJSONString( pTarget, _stateData, context )
                                                           : ObjectStateSerializer::loadFromXMLString( pTarget, _stateData, context );
    }

    string PrefabAsset::convertState( PrefabStateFormat targetFormat ) const
    {
        // 컴포넌트는 매니저가 이름으로 만든다(역직렬화 팩토리). 쓰고 버리는 매니저 안에서 읽고 다른 형식으로 쓴다 — 씬 쿠커와 같은 방법이다.
        GameObjectManager scratch;
        GameObject*       pTemp = scratch.createGameObject( hashed_string( _name.c_str() ) );
        if ( applyStateTo( pTemp ) == false )
            return {};
        return ( targetFormat == PrefabStateFormat::JSON ) ? ObjectStateSerializer::saveToJSONString( pTemp, ObjectSaveOptions{ SerializePurpose::Authoring } ) : ObjectStateSerializer::saveToXMLString( pTemp, ObjectSaveOptions{ SerializePurpose::Authoring } );
    }

    void PrefabAsset::collectReferencedPrefabPaths( vector<string>& outListPath ) const
    {
        outListPath.clear();
        vector<string> listFound;
        // 중첩 인스턴스의 원본이 먼저다 — 스폰이 바로 읽는다. 펼친 노드는 원본을 읽지 않으므로 그 안에 적힌 경로만 본다.
        for ( const PrefabInstanceNode& node : _listNestedInstance )
        {
            if ( node.isResolved() )
            {
                PrefabAssetInternal::collectPrefabRefsFromXMLText( node._resolvedStateXML, listFound );
                continue;
            }
            listFound.push_back( node._prefabPath );
            PrefabAssetInternal::collectPrefabRefsFromXMLText( node._overrideXML, listFound );
        }
        PrefabAssetInternal::collectPrefabRefsFromXMLText( _variantOverrideXML, listFound ); // 물려받은 부품에 덮어쓴 값(`<Part>`)은 상태에 합쳐지지 않는다

        const string trimmed{ StringUtil::trim( _stateData ) };
        if ( trimmed.empty() == false && _stateFormat == PrefabStateFormat::JSON )
        {
            JSONDocument doc;
            if ( doc.parse( trimmed ) )
                PrefabAssetInternal::collectPrefabRefsFromJSON( doc.getRoot(), listFound );
        }
        else
        {
            PrefabAssetInternal::collectPrefabRefsFromXMLText( trimmed, listFound );
        }

        for ( const string& path : listFound )
        {
            PrefabAssetInternal::appendUniquePath( path, outListPath );
        }
    }

    bool PrefabAsset::setNestedInstances( vector<PrefabInstanceNode> listNode )
    {
        if ( PrefabAssetInternal::areNodesWellFormed( listNode, _name ) == false )
            return false;
        _listNestedInstance = std::move( listNode );
        return true;
    }

    bool PrefabAsset::hasResolvedNestedInstances() const
    {
        return VectorUtil::containsIf( _listNestedInstance, [&]( const PrefabInstanceNode& node )
        { return node.isResolved(); } );
    }
} // namespace sw
