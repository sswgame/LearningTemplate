#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/UUID/UUID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct PrefabAssetInternal
        {
            /**
             * @brief 스폰 중인 프리팹 경로 스택(순환 참조 검사)이 힙 없이 담는 깊이입니다.
             * @details 스레드 지역 스택이라 힙에 잡으면 처음 스폰한 자리의 태그(Scene)로 스레드가 끝날 때까지 남는다 — 종료 보고
             *          (`AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline`)는 스레드보다 먼저 온다. 더 깊게 중첩하면 그때만 힙을 쓴다.
             */
            static constexpr size_t kInlineSpawnDepth = 8;
            /** @brief 로드 검증에서 순환이 없다는 값입니다(`getCycleFloor`). */
            static constexpr size_t kNoCycle = static_cast<size_t>( invalid_index::kUint64 );

            /** @brief 지금 펼치는 중인 프리팹 경로의 스택입니다(순환 검사). */
            using PrefabPathStack = small_vector<hashed_string, kInlineSpawnDepth>;

            /** @brief 스택에 넣은 경로를 구간이 끝날 때 뺍니다. */
            struct PathStackScope
            {
                PrefabPathStack& _stack;

                PathStackScope( PrefabPathStack& stack, const hashed_string& key )
                    : _stack{ stack }
                {
                    _stack.push_back( key );
                }
                ~PathStackScope()
                {
                    _stack.pop_back();
                }
                PathStackScope( const PathStackScope& )            = delete;
                PathStackScope& operator=( const PathStackScope& ) = delete;
            };

            /**
             * @brief 이 스레드가 스폰 중인 프리팹 경로입니다. 스폰 · 중첩 스폰 · 펼치기가 같이 씁니다.
             * @details 컴포넌트가 로드 뒤 같은 프리팹을 스폰하는 순환(`PrefabTest.CircularReferenceSpawnProtection`)과, 로드한 뒤 원본을 고쳐 생긴 중첩 순환을 막는다.
             */
            static PrefabPathStack& getSpawnStack()
            {
                thread_local PrefabPathStack t_listSpawnStack;
                return t_listSpawnStack;
            }

            /** @brief 이 스레드가 중첩 인스턴스를 검증하며 로드 중인 프리팹 경로입니다(`PrefabCache::validateNestedInstances`). 스폰 스택과 따로 둔다 — 런타임 스폰이 로드를 실패시키지 않게. */
            static PrefabPathStack& getLoadStack()
            {
                thread_local PrefabPathStack t_listLoadStack;
                return t_listLoadStack;
            }

            /** @brief 로드 스택에서 찾은 순환의 시작 깊이입니다. 순환에 든 프리팹(그 깊이 이상)은 로드에 실패하고, 시작 깊이의 프리팹이 끝날 때 지운다. */
            static size_t& getCycleFloor()
            {
                thread_local size_t t_cycleFloor = kNoCycle;
                return t_cycleFloor;
            }

            static bool isOnStack( const PrefabPathStack& stack, const hashed_string& key )
            {
                return std::find( stack.begin(), stack.end(), key ) != stack.end();
            }

            /**
             * @brief 프리팹 참조(경로 또는 GUID 문자열)를 경로로 풉니다. GUID 가 아니거나 데이터베이스에 없으면 받은 그대로 반환합니다.
             * @details 로드와 스폰이 함께 씁니다 — 한쪽만 GUID 를 풀면 같은 참조가 로드는 되고 스폰은 안 됩니다.
             */
            static string resolvePrefabPath( string_view assetReference )
            {
                if ( engine::areEngineServicesBound() == false )
                    return string{ assetReference };

                UUID   guid{};
                string resolvedPath{ assetReference };
                if ( UUID::tryParse( assetReference, guid ) == false || guid.isNull() )
                    return resolvedPath;

                string mappedPath;
                if ( engine::getSubsystem<AssetManager>().getAssetDatabase().tryGetPath( guid, mappedPath ) && mappedPath.empty() == false )
                    resolvedPath = std::move( mappedPath );
                return resolvedPath;
            }

            static constexpr const utf8* kRoot            = "Prefab";
            static constexpr const utf8* kName            = "name";
            static constexpr const utf8* kGameObject      = "GameObject";
            static constexpr const utf8* kDefaultInstance = "PrefabInstance";
            static constexpr const utf8* kNestedInstance  = "PrefabInstance";
            static constexpr const utf8* kNestedPrefab    = "prefab";
            static constexpr const utf8* kNestedGuid      = "prefabGuid";
            static constexpr const utf8* kNestedAttach    = "attach";
            static constexpr const utf8* kNestedOverrides = "overrides";
            static constexpr const utf8* kNestedList      = "_listPrefabInstance";
            static constexpr uint32      kBinMagic        = FourCcUtil::make( "PFB2" );
            /** @brief 버전 1 부터 상태 뒤에 중첩 인스턴스 노드가 붙고, 2 부터 노드에 부품 경로(`_partPath`)가 붙는다. 다른 버전은 읽지 않는다(쿠킹본은 다시 쿠킹한다). */
            static constexpr uint32 kBinVersion = 2;

            static string makePrefabCacheKey( string_view assetRelativePath )
            {
                string key = FileUtil::normalizePath( assetRelativePath );

                // 확장자 길이를 손으로 쓰지 않는다 — 숫자와 문자열이 떨어져 있으면 어긋난다.
                for ( const string_view extension : { ".bin", ".xml", ".json" } )
                {
                    if ( FileUtil::hasExtension( key, extension ) )
                    {
                        key.resize( key.size() - extension.size() );
                        break;
                    }
                }
                return key;
            }

            /** @brief 순환 검사 스택에 넣는 키입니다(캐시 키를 인턴한 것). */
            static hashed_string makePathKey( const string& cacheKey )
            {
                return hashed_string( cacheKey.data(), static_cast<uint32>( cacheKey.size() ) );
            }

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

            /** @brief @p path 가 목록에 없으면 끝에 더합니다(대소문자 · 구분자를 무시한 캐시 키로 견준다). */
            static void appendUniquePath( const string& path, vector<string>& inoutListPath )
            {
                if ( path.empty() )
                    return;
                const string key = makePrefabCacheKey( path );
                for ( const string& existing : inoutListPath )
                {
                    if ( StringUtil::equals( makePrefabCacheKey( existing ), key, true ) )
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
                if ( AssetFormatRegistry::upgradeXMLWithActiveRegistry( AssetKind::Prefab, wrapDoc, wrapRoot, AssetFormatVersions::kPrefab ) == false )
                    return false;
                XMLNode bodyNode = wrapRoot.findChild( kGameObject );
                if ( bodyNode.isValid() == false )
                    return false;
                xmlBody = bodyNode.toString();
                return true;
            }

            /** @brief 노드의 GUID 가 데이터베이스에 있으면 그 경로로 바꿉니다 — 원본을 옮겨도 노드가 이어진다(씬 엔티티의 `prefabGuid` 와 같다). */
            static void resolveNodePathByGuid( PrefabInstanceNode& node )
            {
                if ( node._prefabGuid.empty() )
                    return;
                const string mappedPath = resolvePrefabPath( node._prefabGuid );
                if ( mappedPath != node._prefabGuid )
                    node._prefabPath = mappedPath;
            }

            /** @brief 쓸 때 적을 노드의 GUID 입니다. 데이터베이스가 경로의 GUID 를 알면 그것, 아니면 읽은 값입니다. */
            static string makeNodeGuid( const PrefabInstanceNode& node )
            {
                if ( node._prefabPath.empty() || engine::areEngineServicesBound() == false )
                    return node._prefabGuid;
                UUID guid{};
                if ( engine::getSubsystem<AssetManager>().getAssetDatabase().tryGetGuid( node._prefabPath, guid ) && guid.isNull() == false )
                    return guid.toString();
                return node._prefabGuid;
            }

            /**
             * @brief 노드 목록이 규칙(부모가 자기보다 앞, 원본 경로나 GUID 나 펼친 상태가 있음, 펼친 노드는 부품 경로가 있고 저작본 노드는 없음)을 지키면 true 입니다.
             *        어긴 노드를 오류로 알립니다.
             */
            static bool areNodesWellFormed( const vector<PrefabInstanceNode>& listNode, string_view sourceName )
            {
                for ( size_t index = 0; index < listNode.size(); ++index )
                {
                    const PrefabInstanceNode& node        = listNode[index];
                    const bool                bParentOk   = -1 <= node._parentIndex && node._parentIndex < static_cast<int32>( index );
                    const bool                bHasSource  = node.isResolved() || node._prefabPath.empty() == false || node._prefabGuid.empty() == false;
                    const bool                bPartPathOk = node.isResolved() != node._partPath.empty();
                    const bool                bWellFormed = bParentOk && bHasSource && bPartPathOk;
                    if ( bWellFormed )
                        continue;
                    const utf8* pProblem = bParentOk == false  ? "a parent that does not come before it"
                                         : bHasSource == false ? "no prefab path"
                                                               : "a part path only a flattened (cooked) node may have, or a flattened node without one";
                    SW_LOG_ERROR( "Nested prefab instance #%# ('%#') in '%#' has %# - the prefab is not loaded", static_cast<uint32>( index ), node._name, sourceName, pProblem );
                    return false;
                }
                return true;
            }

            /** @brief `<PrefabInstance>` 원소들(@p parent 의 자식)을 읽어 끝에 더합니다. 안에 둔 `<PrefabInstance>` 는 그 노드에 붙는 노드입니다. */
            static void readNodesFromXML( const XMLNode& parent, int32 parentIndex, vector<PrefabInstanceNode>& inoutListNode )
            {
                for ( XMLNode element = parent.findChild( kNestedInstance, false ); element.isValid(); element = element.findNextSibling( kNestedInstance, false ) )
                {
                    PrefabInstanceNode node{};
                    const utf8*        pName   = element.findAttribute( kName, false );
                    const utf8*        pPrefab = element.findAttribute( kNestedPrefab, false );
                    const utf8*        pGuid   = element.findAttribute( kNestedGuid, false );
                    const utf8*        pAttach = element.findAttribute( kNestedAttach, false );
                    node._name                 = pName != nullptr ? pName : "";
                    node._prefabPath           = pPrefab != nullptr ? pPrefab : "";
                    node._prefabGuid           = pGuid != nullptr ? pGuid : "";
                    node._attachKey            = pAttach != nullptr ? pAttach : "";
                    node._parentIndex          = parentIndex;
                    const XMLNode overrides    = element.findChild( PrefabOverrides::kRootName, false );
                    if ( overrides.isValid() )
                        node._overrideXML = overrides.toString();
                    resolveNodePathByGuid( node );
                    inoutListNode.push_back( std::move( node ) );
                    readNodesFromXML( element, static_cast<int32>( inoutListNode.size() - 1 ), inoutListNode );
                }
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
                    if ( node._name.empty() == false )
                        element.appendAttribute( kName, string_view{ node._name } );
                    element.appendAttribute( kNestedPrefab, string_view{ node._prefabPath } );
                    const string guid = makeNodeGuid( node );
                    if ( guid.empty() == false )
                        element.appendAttribute( kNestedGuid, string_view{ guid } );
                    if ( node._attachKey.empty() == false )
                        element.appendAttribute( kNestedAttach, string_view{ node._attachKey } );
                    if ( node._overrideXML.empty() == false )
                    {
                        XMLDocument overrideDoc;
                        if ( overrideDoc.parse( node._overrideXML ) == false || overrideDoc.getRoot().isValid() == false )
                        {
                            SW_LOG_ERROR( "Nested prefab instance '%#' has overrides that are not XML", node._name );
                            return false;
                        }
                        element.appendClone( overrideDoc.getRoot() );
                    }
                    if ( writeNodesToXML( element, listNode, static_cast<int32>( index ) ) == false )
                        return false;
                }
                return true;
            }

            /** @brief JSON 배열 `_listPrefabInstance`(@p parent 의 멤버)의 노드를 읽어 끝에 더합니다. 객체마다 같은 이름의 배열이 그 노드에 붙는 노드입니다. */
            static void readNodesFromJSON( const JSONValue& parent, int32 parentIndex, vector<PrefabInstanceNode>& inoutListNode )
            {
                const JSONValue arrayValue = parent.get( kNestedList, false );
                if ( arrayValue.isArray() == false )
                    return;
                const size_t count = arrayValue.size();
                for ( size_t index = 0; index < count; ++index )
                {
                    const JSONValue element = arrayValue.at( index );
                    if ( element.isObject() == false )
                        continue;
                    PrefabInstanceNode node{};
                    node._name        = element.get( kName, false ).asString();
                    node._prefabPath  = element.get( kNestedPrefab, false ).asString();
                    node._prefabGuid  = element.get( kNestedGuid, false ).asString();
                    node._attachKey   = element.get( kNestedAttach, false ).asString();
                    node._overrideXML = element.get( kNestedOverrides, false ).asString();
                    node._parentIndex = parentIndex;
                    resolveNodePathByGuid( node );
                    inoutListNode.push_back( std::move( node ) );
                    readNodesFromJSON( element, static_cast<int32>( inoutListNode.size() - 1 ), inoutListNode );
                }
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
                        arrayValue = parent.set( kNestedList, false );
                        arrayValue.setArray();
                    }
                    const JSONValue element = arrayValue.pushBack();
                    element.setObject();
                    if ( node._name.empty() == false )
                        element.set( kName, false ).setString( node._name );
                    element.set( kNestedPrefab, false ).setString( node._prefabPath );
                    const string guid = makeNodeGuid( node );
                    if ( guid.empty() == false )
                        element.set( kNestedGuid, false ).setString( guid );
                    if ( node._attachKey.empty() == false )
                        element.set( kNestedAttach, false ).setString( node._attachKey );
                    if ( node._overrideXML.empty() == false )
                        element.set( kNestedOverrides, false ).setString( node._overrideXML );
                    writeNodesToJSON( element, listNode, static_cast<int32>( index ) );
                }
            }

            /**
             * @brief 부품 @p pChild 를 부모 오브젝트 @p pParent 의 `attachKey` 씬 컴포넌트(비면 primary)에 붙입니다. 씬 컴포넌트가 없는 부품은 붙이지 않습니다.
             * @details 상대 위치를 그대로 둔다(`KeepRelative`) — 노드의 오버라이드가 적은 위치가 부모 기준 위치다.
             */
            static void attachNestedInstance( GameObject* pChild, GameObject* pParent, const string& attachKey )
            {
                SceneComponent* pChildRoot = pChild->getPrimarySceneComponent();
                if ( pChildRoot == nullptr )
                    return;
                SceneComponent* pTarget = pParent->getPrimarySceneComponent();
                if ( attachKey.empty() == false )
                {
                    Component* pKeyed = ComponentStableKey::findComponent( pParent, attachKey );
                    if ( pKeyed != nullptr && pKeyed->isSceneComponent() )
                        pTarget = static_cast<SceneComponent*>( pKeyed );
                    else
                        SW_LOG_WARNING( "Nested prefab instance '%#' attaches to '%#', which '%#' does not have - it is attached to the primary scene component",
                                        pChild->getName().c_str(), attachKey, pParent->getName().c_str() );
                }
                if ( pTarget == nullptr )
                {
                    SW_LOG_WARNING( "Nested prefab instance '%#' has no scene component of '%#' to attach to - it is left at the root", pChild->getName().c_str(),
                                    pParent->getName().c_str() );
                    return;
                }
                if ( pChildRoot->attachToComponent( pTarget ) == false )
                    SW_LOG_WARNING( "Nested prefab instance '%#' could not be attached to '%#'", pChild->getName().c_str(), pParent->getName().c_str() );
            }

            /** @brief 부품 경로 @p parentPath 아래 토막 @p key 를 이은 경로입니다(`PrefabPartOverride`). */
            static string joinPartPath( string_view parentPath, string_view key )
            {
                string path{ parentPath };
                if ( path.empty() == false )
                    path += "/";
                path += key;
                return path;
            }

            /** @brief @p listPartOverride 가운데 경로가 @p partPath 인 것을 문서 순서대로 @p inoutStateXML 에 얹습니다. 얹지 못하면 false 입니다. */
            [[nodiscard]] static bool applyPartOverrides( const vector<PrefabPartOverride>& listPartOverride, const string& partPath, string& inoutStateXML )
            {
                for ( const PrefabPartOverride& part : listPartOverride )
                {
                    if ( part._partPath != partPath )
                        continue;
                    string merged;
                    if ( PrefabOverrides::makeInstanceState( inoutStateXML, part._overrideXML, {}, merged ) == false )
                        return false;
                    inoutStateXML = std::move( merged );
                }
                return true;
            }

            /**
             * @brief 노드가 자기 부품 아래에 덮어쓴 것 @p inoutListInner(부품 @p partPath 에서 잰 경로)를 바깥 루트에서 잰 경로로 바꾸고, 그 뒤에 바깥 것 @p listEnclosingPart 를 잇습니다.
             * @details 안쪽이 앞이라 같은 부품이면 바깥 것이 나중에 얹혀 이긴다(유니티: 씬 인스턴스의 오버라이드가 바깥 프리팹의 오버라이드를 이긴다).
             */
            static void prependInnerParts( string_view partPath, vector<PrefabPartOverride>& inoutListInner, const vector<PrefabPartOverride>& listEnclosingPart )
            {
                for ( PrefabPartOverride& inner : inoutListInner )
                {
                    inner._partPath = joinPartPath( partPath, inner._partPath );
                }
                inoutListInner.insert( inoutListInner.end(), listEnclosingPart.begin(), listEnclosingPart.end() );
            }

            /** @brief @p rootObjectID 인스턴스가 스폰한 부품과 그 부품들이 스폰한 부품을 모두 모읍니다(부모 부품이 앞). */
            static void collectPartTree( const PrefabInstanceRegistry& registry, uint64 rootObjectID, vector<uint64>& outListObjectID )
            {
                outListObjectID.clear();
                registry.collectNestedPartsOf( rootObjectID, outListObjectID );
                vector<uint64> listChildPart;
                for ( size_t index = 0; index < outListObjectID.size(); ++index )
                {
                    registry.collectNestedPartsOf( outListObjectID[index], listChildPart );
                    outListObjectID.insert( outListObjectID.end(), listChildPart.begin(), listChildPart.end() );
                }
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
                    SW_LOG_WARNING( "'%#' has nested prefab instances but no prefab cache is given - they are not written to the prefab", root.getName().c_str() );
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
                            SW_LOG_WARNING( "Nested prefab instance '%#' hangs under '%#', which is not part of '%#' - it is not written to the prefab", pPart->getName().c_str(),
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

                    const PrefabAsset* pSource = pPrefabs->loadPrefab( node._prefabPath );
                    string             baseState;
                    if ( pSource == nullptr || PrefabOverrides::makeBaseState( *pSource, baseState ) == false )
                    {
                        SW_LOG_WARNING( "Nested prefab '%#' of '%#' could not be read - instance '%#' is not written to the prefab", node._prefabPath, root.getName().c_str(),
                                        node._name );
                        continue;
                    }
                    ObjectSaveOptions options{ SerializePurpose::Authoring };
                    options._bOmitExternalParent = true; // 부모는 노드의 자리(부모 노드 · attach)가 적는다
                    // 부품 아래 부품(원본이 스폰하는 것)을 고친 것은 노드의 `<Part>` 로 싣는다 — 노드로 담지 않으므로 싣지 않으면 Apply 가 버린다.
                    vector<PrefabPartOverride> listDeepPart;
                    const bool                 bOverridesMade =
                        PrefabOverrides::computeOverrides( ObjectStateSerializer::saveToXMLString( pPart, options ), baseState, node._overrideXML ) &&
                        pPrefabs->collectPartOverrides( *pPart, *pSource, listDeepPart ) && PrefabOverrides::appendPartOverrides( node._overrideXML, listDeepPart );
                    if ( bOverridesMade == false )
                    {
                        SW_LOG_WARNING( "Overrides of nested prefab instance '%#' could not be computed - it is not written to the prefab", node._name );
                        continue;
                    }
                    listCaptured.push_back( NestedPartEntry{ pPart, static_cast<int32>( outListNode.size() ) } );
                    outListNode.push_back( std::move( node ) );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "PrefabAsset" );

    string PrefabInstanceNode::makePartKey( size_t index ) const
    {
        return isResolved() ? _partPath : to_string( static_cast<uint64>( index ) );
    }

    PrefabAsset::PrefabAsset()
        : _name{}
        , _stateData{}
        , _listNestedInstance{}
        , _stateFormat{ PrefabStateFormat::XML }
        , _bValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool PrefabAsset::loadFromXMLFile( string_view assetRelativePath )
    {
        return loadFromXMLSource( assetRelativePath, ReadSource::Resource );
    }

    bool PrefabAsset::loadFromJSONFile( string_view assetRelativePath )
    {
        return loadFromJSONSource( assetRelativePath, ReadSource::Resource );
    }

    bool PrefabAsset::loadFromDiskFile( string_view diskPath )
    {
        // 형식은 확장자 하나로 정한다 — `saveToFile` · 쿠커와 같은 규칙.
        if ( StringUtil::endsWith( diskPath, ".prefab.json", true ) )
            return loadFromJSONSource( diskPath, ReadSource::DiskFile );
        if ( StringUtil::endsWith( diskPath, ".prefab.xml", true ) )
            return loadFromXMLSource( diskPath, ReadSource::DiskFile );
        SW_LOG_ERROR( "Not a prefab source file (.prefab.xml / .prefab.json): %#", diskPath );
        return false;
    }

    bool PrefabAsset::loadFromXMLSource( string_view path, ReadSource source )
    {
        _bValid = SW_FALSE;
        AssetLoadScope loadScope( "Prefab", path );
        string         absPath;
        XMLDocument    doc;
        // 디스크 파일은 부르는 쪽이 이름(`loadFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        const bool bRead = source == ReadSource::DiskFile ? doc.loadFile( path ) : doc.loadResource( path, &absPath );
        if ( source == ReadSource::DiskFile )
            absPath = string( path );
        if ( bRead == false )
        {
            SW_LOG_ERROR( "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }
        loadScope.beginPhase( AssetLoadPhase::Decode );

        XMLNode root = doc.getRoot( PrefabAssetInternal::kRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <Prefab> root: %#", absPath );
            return false;
        }
        if ( AssetFormatRegistry::upgradeXMLWithActiveRegistry( AssetKind::Prefab, doc, root, AssetFormatVersions::kPrefab ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        const utf8* pNameAttr = root.findAttribute( PrefabAssetInternal::kName );
        if ( pNameAttr != nullptr )
            _name = pNameAttr;

        XMLNode bodyNode = root.findChild( PrefabAssetInternal::kGameObject );
        if ( bodyNode.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <GameObject> in <Prefab>: %#", absPath );
            return false;
        }
        _stateData = bodyNode.toString();

        vector<PrefabInstanceNode> listNode;
        PrefabAssetInternal::readNodesFromXML( root, -1, listNode );
        if ( PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;
        _listNestedInstance = std::move( listNode );

        if ( _name.empty() )
            _name = FileUtil::removeExtension( FileUtil::getFileNamePart( absPath ) );

        _stateFormat = PrefabStateFormat::XML;
        _bValid      = SW_TRUE;
        loadScope.setBytes( _stateData.size() );
        loadScope.setSucceeded();
        SW_LOG_INFO( "Loaded '%#' from %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::loadFromJSONSource( string_view path, ReadSource source )
    {
        _bValid = SW_FALSE;
        string       absPath;
        JSONDocument doc;
        const bool   bRead = source == ReadSource::DiskFile ? doc.loadFile( path ) : doc.loadResource( path, &absPath );
        if ( source == ReadSource::DiskFile )
            absPath = string( path );
        if ( bRead == false )
        {
            SW_LOG_ERROR( "Prefab not loaded - %#", doc.getLastError() );
            return false;
        }

        // 저장기가 쓰는 모양 하나 — GameObject 상태 JSON 그대로다(이름은 그 `_name`). 중첩 인스턴스(`_listPrefabInstance`)는 상태에서 떼어 둔다.
        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "Prefab JSON is not a GameObject state object: %#", absPath );
            return false;
        }
        _name = root.get( "_name" ).asString();

        vector<PrefabInstanceNode> listNode;
        PrefabAssetInternal::readNodesFromJSON( root, -1, listNode );
        if ( PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;
        _listNestedInstance = std::move( listNode );

        if ( root.has( PrefabAssetInternal::kNestedList, false ) )
        {
            JSONDocument    stateDoc;
            const JSONValue stateRoot = stateDoc.makeObject();
            for ( const string& key : root.getMemberNames() )
            {
                if ( key != PrefabAssetInternal::kNestedList )
                    stateRoot.set( key, false ).assignFrom( root.get( key, false ) );
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

    bool PrefabAsset::loadFromBinaryFile( string_view assetRelativePath )
    {
        _bValid = SW_FALSE;
        // 쿠킹한 프리팹은 팩에 든다 — 디스크 경로로 열지 않고 `ResourceIO` 로 읽는다(낱개 파일 허용 정책도 거기서 본다).
        const string  absPath{ assetRelativePath };
        vector<uint8> bytes;
        Archive       arch;
        if ( ResourceIO::readBinaryResource( assetRelativePath, bytes ) )
            arch = Archive( bytes.data(), bytes.size() );
        if ( arch.getSize() < 12 )
        {
            SW_LOG_ERROR( "Binary read failed or too small: %#", absPath );
            return false;
        }

        uint32 magic{ 0 };
        arch >> magic;
        if ( magic != PrefabAssetInternal::kBinMagic )
        {
            SW_LOG_ERROR( "Bad binary magic: %#", absPath );
            return false;
        }

        uint32 version{ 0 };
        arch >> version;
        if ( version != PrefabAssetInternal::kBinVersion )
        {
            SW_LOG_ERROR( "Cooked prefab version %# is not %# in %# - cook the prefabs again", version, PrefabAssetInternal::kBinVersion, absPath );
            return false;
        }

        uint32 nodeCount{ 0 };
        arch >> _name >> _stateData >> nodeCount;
        if ( arch.isError() || nodeCount > arch.getSize() )
        {
            SW_LOG_ERROR( "Binary prefab stream corrupted in %#", absPath );
            return false;
        }
        vector<PrefabInstanceNode> listNode( nodeCount );
        for ( PrefabInstanceNode& node : listNode )
        {
            arch >> node._name >> node._prefabPath >> node._prefabGuid >> node._attachKey >> node._overrideXML >> node._resolvedStateXML >> node._partPath >> node._parentIndex;
        }
        if ( arch.isError() )
        {
            SW_LOG_ERROR( "Binary prefab stream corrupted in %#", absPath );
            return false;
        }
        if ( PrefabAssetInternal::areNodesWellFormed( listNode, absPath ) == false )
            return false;
        _listNestedInstance = std::move( listNode );

        _stateFormat = PrefabAssetInternal::detectStateFormat( _stateData );
        if ( PrefabAssetInternal::upgradePrefabXMLBody( _stateData, _stateFormat ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        _bValid = SW_TRUE;
        SW_LOG_INFO( "Loaded '%#' from binary %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToXMLFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourcePaths::getWritePath( assetRelativePath );
        if ( hasResolvedNestedInstances() )
        {
            SW_LOG_ERROR( "Prefab '%#' holds cooked (resolved) nested instances and cannot be written as a source file - '%#' is left as it was", _name,
                          assetRelativePath );
            return false;
        }

        const string xmlBody = ( _stateFormat == PrefabStateFormat::XML ) ? _stateData : convertState( PrefabStateFormat::XML );

        XMLDocument xmlDoc;
        XMLNode     root = xmlDoc.appendRoot( PrefabAssetInternal::kRoot );
        root.appendAttribute( "formatVersion", static_cast<uint32>( AssetFormatVersions::kPrefab ) );
        root.appendAttribute( PrefabAssetInternal::kName, _name );
        if ( xmlBody.empty() == false )
        {
            XMLDocument bodyDoc;
            const bool  bBodyParsed = bodyDoc.parse( xmlBody );
            XMLNode     bodyRoot    = bBodyParsed ? bodyDoc.getRoot() : XMLNode{};
            if ( bodyRoot.isValid() == false )
            {
                SW_LOG_ERROR( "Prefab '%#' has a state that cannot be written as XML - '%#' is left as it was", _name, assetRelativePath );
                return false;
            }
            root.appendClone( bodyRoot );
        }
        else if ( _stateData.empty() == false )
        {
            // 상태가 있는데 XML 로 옮기지 못했다(타입이 빠진 JSON 본문 등). 빈 `<Prefab>` 을 쓰면 파일의 내용이 사라지므로 실패로 돌려준다.
            SW_LOG_ERROR( "Prefab '%#' could not be converted to XML - '%#' is left as it was", _name, assetRelativePath );
            return false;
        }
        if ( PrefabAssetInternal::writeNodesToXML( root, _listNestedInstance, -1 ) == false )
        {
            SW_LOG_ERROR( "Prefab '%#' has nested instances that cannot be written - '%#' is left as it was", _name, assetRelativePath );
            return false;
        }

        // 상위 폴더가 없으면 쓰기가 실패한다. 실패는 로그로 남긴다(SceneDocument::saveXML 과 같은 형태).
        FileUtil::ensureParentDirectoryExists( absPath );

        const bool writeOk = xmlDoc.saveFile( absPath );
        if ( writeOk == false )
        {
            SW_LOG_ERROR( "Failed to write prefab XML: %#", absPath );
            return false;
        }

        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<AssetManager>().getAssetDatabase().ensureMeta( assetRelativePath );
        SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToJSONFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourcePaths::getWritePath( assetRelativePath );
        if ( hasResolvedNestedInstances() )
        {
            SW_LOG_ERROR( "Prefab '%#' holds cooked (resolved) nested instances and cannot be written as a source file; nothing written: %#", _name, absPath );
            return false;
        }

        string jsonStr = ( _stateFormat == PrefabStateFormat::JSON ) ? _stateData : convertState( PrefabStateFormat::JSON );
        if ( jsonStr.empty() )
        {
            // 상태를 읽지 못했다(타입이 빠진 본문 등). 로더가 읽을 수 없는 파일을 쓰지 않는다 — XML 로는 그대로 저장할 수 있다.
            SW_LOG_ERROR( "Prefab '%#' state could not be converted to JSON; nothing written: %#", _name, absPath );
            return false;
        }
        if ( _listNestedInstance.empty() == false )
        {
            JSONDocument doc;
            if ( doc.parse( jsonStr ) == false || doc.getRoot().isObject() == false )
            {
                SW_LOG_ERROR( "Prefab '%#' state is not a JSON object; nothing written: %#", _name, absPath );
                return false;
            }
            PrefabAssetInternal::writeNodesToJSON( doc.getRoot(), _listNestedInstance, -1 );
            jsonStr = doc.dump( 0 );
        }

        FileUtil::ensureParentDirectoryExists( absPath );

        const bool writeOk = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( jsonStr.data() ),
                                                  jsonStr.size() );
        if ( writeOk == false )
        {
            SW_LOG_ERROR( "Failed to write prefab JSON: %#", absPath );
            return false;
        }

        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<AssetManager>().getAssetDatabase().ensureMeta( assetRelativePath );
        SW_LOG_INFO( "Saved '%#' JSON %#", _name, absPath );
        return true;
    }

    bool PrefabAsset::saveToFile( string_view assetRelativePath ) const
    {
        // 형식은 경로가 정한다 — 쿠커(`cookAllPrefabs`)와 로더(`loadPrefab`)가 읽는 규칙과 같다. 늘 XML 로 쓰면 `.prefab.json` 에 XML 이
        // 들어가 그 프리팹이 다시는 읽히지 않는다. 프리팹이 아닌 경로(씬 · 머티리얼)는 덮지 않는다.
        if ( StringUtil::endsWith( assetRelativePath, ".prefab.json", true ) )
            return saveToJSONFile( assetRelativePath );
        if ( StringUtil::endsWith( assetRelativePath, ".prefab.xml", true ) )
            return saveToXMLFile( assetRelativePath );
        SW_LOG_ERROR( "'%#' is not a prefab source path (.prefab.xml / .prefab.json) - nothing was written", assetRelativePath );
        return false;
    }

    bool PrefabAsset::saveToBinaryFile( string_view assetRelativePath ) const
    {
        const string absPath = ResourcePaths::getWritePath( assetRelativePath );

        Archive arch;
        arch << PrefabAssetInternal::kBinMagic;
        arch << PrefabAssetInternal::kBinVersion;
        arch << _name;
        arch << _stateData;
        arch << static_cast<uint32>( _listNestedInstance.size() );
        for ( const PrefabInstanceNode& node : _listNestedInstance )
        {
            arch << node._name << node._prefabPath << node._prefabGuid << node._attachKey << node._overrideXML << node._resolvedStateXML << node._partPath << node._parentIndex;
        }

        FileUtil::ensureParentDirectoryExists( absPath );
        if ( arch.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write prefab binary: %#", absPath );
            return false;
        }
        return true;
    }

    void PrefabAsset::setFromGameObject( const GameObject* pGameObject, PrefabCache* pNestedPrefabs )
    {
        _listNestedInstance.clear();
        if ( pGameObject == nullptr )
        {
            _bValid = SW_FALSE;
            return;
        }
        // 프리팹 루트에는 부모가 없다 — 자식 인스턴스로 프리팹을 만들어도 원래 부모를 싣지 않는다. 실으면 그 프리팹을 스폰할 때마다
        // 그 이름의 오브젝트에 붙는다(언리얼 · 유니티의 프리팹 루트도 부모를 들지 않는다).
        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = true;
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
            listFound.push_back( node._prefabPath.empty() ? node._prefabGuid : node._prefabPath );
            PrefabAssetInternal::collectPrefabRefsFromXMLText( node._overrideXML, listFound );
        }

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
        for ( const PrefabInstanceNode& node : _listNestedInstance )
        {
            if ( node.isResolved() )
                return true;
        }
        return false;
    }

    void PrefabCache::reload( string_view assetRelativePath, IRHIDevice* )
    {
        if ( assetRelativePath.empty() )
            return;

        const string cacheKey = PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );

        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        _mapCache.erase( cacheKey );
        // 중첩 인스턴스(원본 경로)를 가진 프리팹은 다음 로드가 순환을 다시 보게 버린다. 고친 원본이 상위를 놓으면 순환이 된다.
        for ( auto cacheIt = _mapCache.begin(); cacheIt != _mapCache.end(); )
        {
            const vector<PrefabInstanceNode>& listNode       = cacheIt->second->getNestedInstances();
            bool                              bHasSourceNode = false;
            for ( const PrefabInstanceNode& node : listNode )
            {
                bHasSourceNode = bHasSourceNode || node.isResolved() == false;
            }
            if ( bHasSourceNode )
                cacheIt = _mapCache.erase( cacheIt );
            else
                ++cacheIt;
        }
    }

    bool PrefabCache::isCached( string_view assetRelativePath ) const
    {
        if ( assetRelativePath.empty() )
            return false;

        const string                        cacheKey = PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );
        std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.find( cacheKey ) != _mapCache.end();
    }

    size_t PrefabCache::getCachedCount() const
    {
        std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.size();
    }

    void PrefabCache::clear()
    {
        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        _mapCache.clear();
    }
    PrefabAsset* PrefabCache::loadPrefab( string_view assetRelativePath )
    {
        SW_MEMORY_SCOPE( Scene );
        const string resolvedPath = PrefabAssetInternal::resolvePrefabPath( assetRelativePath );

        const string cacheKey = PrefabAssetInternal::makePrefabCacheKey( resolvedPath );
        {
            std::shared_lock<std::shared_mutex> readLock{ _mapCacheMutex };
            const auto                          cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
                return cacheIt->second.get();
        }

        unique_ptr<PrefabAsset> asset = make_unique<PrefabAsset>();

        // 쿠킹본 이름은 쿠커(`cookAllPrefabs`)와 같은 규칙 하나다(`AssetCookPath`).
        string binPath = AssetCookPath::toCookedPath( resolvedPath );
        if ( binPath.empty() )
            binPath = resolvedPath;

        // 배포 구성은 쿠킹본만 읽는다. 쿠킹은 소스를 읽는다 — 쿠킹의 입력은 소스다. 쿠킹인지는 둘 중 하나로 안다: 소스 트리를 올린 쿠킹 프로세스
        // (`ContentSource::SourceTree`), 또는 이 스레드의 쿠킹 작업(`CookJob` — 쿠커가 작업 안에서 씬을 지으며 프리팹을 부른다. 프로세스의 콘텐츠
        // 출처와 상관없이 그 작업의 입력은 소스이고, 쿠킹본을 읽으면 아직 굽지 않은 프리팹이 없다고 지거나 낡은 것을 읽는다).
#if defined( SW_SHIPPING )
        const bool bSourceTreeProcess = engine::areEngineServicesBound() && engine::getSubsystem<AssetManager>().getContentSource() == ContentSource::SourceTree;
        const bool bReadsSource       = bSourceTreeProcess || CookJob::findCurrent() != nullptr;
#else
        constexpr bool bReadsSource = true;
#endif
        if ( bReadsSource == false )
        {
            if ( asset->loadFromBinaryFile( binPath ) == false )
            {
                SW_LOG_ERROR( "Shipping requires cooked binary: %#", binPath );
                return nullptr;
            }
        }
        else
        {
            // 소스를 읽는 구성은 소스(XML/JSON)만 본다. 소스가 없을 때 옆에 남은 쿠킹본(.bin)을 대신 읽으면 옮기거나 지운 프리팹이
            // 낡은 내용으로 살아나 실패가 가려진다(언리얼 · 유니티의 에디터도 쿠킹 데이터를 보지 않는다).
            const bool bJSON         = FileUtil::hasExtension( resolvedPath, ".json" );
            const bool bSourceLoaded = bJSON ? asset->loadFromJSONFile( resolvedPath ) : asset->loadFromXMLFile( resolvedPath );
            if ( bSourceLoaded == false )
            {
                SW_LOG_ERROR( "Prefab source could not be loaded: %#", resolvedPath );
                return nullptr;
            }
        }

        // 중첩 인스턴스가 순환하면 로드 오류다(캐시에 넣지 않는다). 원본은 여기서 재귀로 로드되어 캐시에 든다.
        if ( validateNestedInstances( *asset, cacheKey ) == false )
            return nullptr;

        std::unique_lock<std::shared_mutex> writeLock{ _mapCacheMutex };
        const auto                          cacheIt = _mapCache.find( cacheKey );
        if ( cacheIt != _mapCache.end() )
            return cacheIt->second.get();

        PrefabAsset* pCached = asset.get();
        _mapCache.emplace( cacheKey, std::move( asset ) );
        return pCached;
    }

    bool PrefabCache::validateNestedInstances( const PrefabAsset& asset, const string& cacheKey )
    {
        bool bHasSourceNode = false;
        for ( const PrefabInstanceNode& node : asset.getNestedInstances() )
        {
            bHasSourceNode = bHasSourceNode || node.isResolved() == false;
        }
        if ( bHasSourceNode == false )
            return true;

        PrefabAssetInternal::PrefabPathStack& stack      = PrefabAssetInternal::getLoadStack();
        size_t&                               cycleFloor = PrefabAssetInternal::getCycleFloor();
        const size_t                          depth      = stack.size();
        {
            const PrefabAssetInternal::PathStackScope scope{ stack, PrefabAssetInternal::makePathKey( cacheKey ) };
            for ( const PrefabInstanceNode& node : asset.getNestedInstances() )
            {
                if ( node.isResolved() )
                    continue;
                const string childPath = PrefabAssetInternal::resolvePrefabPath( node._prefabPath.empty() ? node._prefabGuid : node._prefabPath );
                const auto   stackIt   = std::find( stack.begin(), stack.end(), PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( childPath ) ) );
                if ( stackIt != stack.end() )
                {
                    cycleFloor = MathUtil::min( cycleFloor, static_cast<size_t>( stackIt - stack.begin() ) );
                    SW_LOG_ERROR( "Circular nested prefab reference: '%#' places '%#', which is already being loaded", asset.getName(), childPath );
                    continue;
                }
                const bool bLoadFailed = loadPrefab( childPath ) == nullptr;
                if ( bLoadFailed && cycleFloor > depth )
                    SW_LOG_WARNING( "Nested prefab '%#' in '%#' could not be loaded - that instance is skipped when spawning", childPath, asset.getName() );
            }
        }

        const bool bInCycle = cycleFloor <= depth;
        if ( cycleFloor == depth )
            cycleFloor = PrefabAssetInternal::kNoCycle; // 순환이 여기서 시작했다 — 바깥 프리팹은 그 노드만 건너뛰고 로드된다
        if ( bInCycle )
            SW_LOG_ERROR( "Prefab '%#' is part of a circular nested prefab reference - it is not loaded", asset.getName() );
        return bInCycle == false;
    }

    bool PrefabCache::revertInstance( GameObject* pInstance, string_view assetRelativePath )
    {
        if ( pInstance == nullptr )
            return false;
        const PrefabAsset* pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return false;

        // 인스턴스의 자리를 적어 둔다. 상태를 읽으면 컴포넌트가 모두 새로 만들어지고 이름 · 부착 · 트랜스폼이 프리팹의 것이 된다.
        // 부모는 오브젝트가 아니라 **붙어 있던 컴포넌트**(소켓일 수 있다)를 핸들로 적는다 — 부모 오브젝트의 primary 에 다시 붙이면
        // 소켓에 달린 무기가 튄다. 컴포넌트 id 도 복원한다 — 이 인스턴스의 컴포넌트를 가리키던 핸들(활성 카메라 · 게임 코드)이 이어지게.
        const hashed_string   name         = pInstance->getName();
        const SceneComponent* pOldRoot     = pInstance->getPrimarySceneComponent();
        const bool            bHadRoot     = pOldRoot != nullptr;
        const ComponentHandle parentHandle = ( bHadRoot && pOldRoot->getParent() != nullptr ) ? pOldRoot->getParent()->getHandle() : ComponentHandle{};
        const float3          position     = bHadRoot ? pOldRoot->getLocalPosition() : float3{};
        const float3          rotation     = bHadRoot ? pOldRoot->getLocalRotation() : float3{};
        const ObjectIdentity  identity     = ObjectStateSerializer::captureIdentity( pInstance );

        if ( pAsset->applyStateTo( pInstance, &identity ) == false )
            return false;

        pInstance->setName( name );
        SceneComponent* pNewRoot = pInstance->getPrimarySceneComponent();
        if ( pNewRoot != nullptr )
        {
            GameObjectManager* pManager        = pInstance->getManager();
            Component*         pParent         = ( pManager != nullptr && parentHandle.isValid() ) ? pManager->resolveComponent( parentHandle ) : nullptr;
            const bool         bReattachNeeded = pParent != nullptr && pParent->isSceneComponent() && pNewRoot->getParent() != pParent &&
                                         pNewRoot->attachToComponent( static_cast<SceneComponent*>( pParent ) ) == false;
            if ( bReattachNeeded )
                SW_LOG_WARNING( "Revert of '%#' could not re-attach it to its parent - it stays at the root", name.c_str() );
            if ( bHadRoot )
            {
                pNewRoot->setLocalPosition( position );
                pNewRoot->setLocalRotation( rotation );
            }
        }
        return true;
    }

    bool PrefabCache::updateInstance( GameObject* pInstance, string_view oldBaseStateXML, string_view assetRelativePath )
    {
        if ( pInstance == nullptr || oldBaseStateXML.empty() )
            return false;
        const PrefabAsset* pAsset = loadPrefab( assetRelativePath );
        string             newBaseState;
        const bool         bNoNewBase = pAsset == nullptr || pAsset->isValid() == false || PrefabOverrides::makeBaseState( *pAsset, newBaseState ) == false;
        if ( bNoNewBase )
            return false;
        // 덮어쓴 것은 옛 원형과 견줘 뽑는다. 새 원형과 견주면 프리팹에서 바뀐 칸이 모두 "인스턴스가 덮어쓴 값" 으로 보여 퍼지지 않는다.
        const string instanceState = ObjectStateSerializer::saveToXMLString( pInstance, ObjectSaveOptions{ SerializePurpose::Authoring } );
        string       overrideXML;
        string       newInstanceState;
        const bool   bStateMade = PrefabOverrides::computeOverrides( instanceState, oldBaseStateXML, overrideXML ) &&
                                PrefabOverrides::makeInstanceState( newBaseState, overrideXML, pInstance->getName().c_str(), newInstanceState );
        if ( bStateMade == false )
            return false;
        const ObjectIdentity identity = ObjectStateSerializer::captureIdentity( pInstance );
        ObjectLoadContext    context{ SerializePurpose::Authoring };
        context._pIdentity = &identity;
        return ObjectStateSerializer::loadFromXMLString( pInstance, newInstanceState, context );
    }

    GameObject* PrefabCache::spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName )
    {
        SW_MEMORY_SCOPE( Scene );
        // 이 함수는 나머지 포인터를 모두 검사한다(`pAsset` · `pGameObject` · `pInstanceName`).
        // 활성 씬이 없을 때 `getObjectManager()` 는 nullptr 를 반환한다.
        if ( pGameObjectManager == nullptr )
        {
            SW_LOG_WARNING( "Cannot spawn prefab '%#' without a GameObjectManager.", assetRelativePath );
            return nullptr;
        }

        const string        resolvedPath = PrefabAssetInternal::resolvePrefabPath( assetRelativePath );
        const hashed_string pathKey      = PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( resolvedPath ) );

        PrefabAssetInternal::PrefabPathStack& spawnStack = PrefabAssetInternal::getSpawnStack();
        if ( PrefabAssetInternal::isOnStack( spawnStack, pathKey ) )
        {
            SW_LOG_ERROR( "Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
                          assetRelativePath );
            return nullptr;
        }
        const PrefabAssetInternal::PathStackScope scope{ spawnStack, pathKey };

        PrefabAsset* pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return nullptr;

        const utf8* pInstanceNameUtf8 = pInstanceName != nullptr ? pInstanceName : pAsset->getName().c_str();
        if ( StringUtil::isNullOrEmpty( pInstanceNameUtf8 ) )
            pInstanceNameUtf8 = PrefabAssetInternal::kDefaultInstance;

        GameObject* pGameObject = pGameObjectManager->createGameObject( hashed_string( pInstanceNameUtf8 ) );
        if ( pGameObject == nullptr )
            return nullptr;

        // 컴포넌트 틱 중이면 오브젝트는 지금 만들어 돌려주고(부르는 쪽이 핸들을 든다), 상태는 틱 직후 구조 변경 큐에서 채운다. 그 자리에서
        // 상태를 읽으면 컴포넌트 추가가 모두 미뤄져(nullptr) **프리팹의 값이 버려지고** 빈 오브젝트만 남는다.
        if ( pGameObjectManager->isStructuralMutationFrozen() )
        {
            const uint64 objectID = pGameObject->getObjectID();
            const string instanceName( pInstanceNameUtf8 );
            // 캐시의 프리팹은 그 사이 다시 읽힐 수 있다(에디터 핫 리로드) — 포인터가 아니라 경로를 들고 그때 다시 찾는다.
            pGameObjectManager->deferStructuralChange( [this, pGameObjectManager, objectID, resolvedPath, instanceName, pathKey]()
            {
                // 미룬 자리에서도 이 프리팹을 펼치는 중이라고 적는다 — 중첩 스폰의 순환 검사가 이 경로를 본다.
                const PrefabAssetInternal::PathStackScope laterScope{ PrefabAssetInternal::getSpawnStack(), pathKey };
                GameObject*                               pSpawned      = pGameObjectManager->findGameObjectByID( objectID );
                PrefabAsset*                              pLaterAsset   = ( pSpawned != nullptr ) ? loadPrefab( resolvedPath ) : nullptr;
                const bool                                bStateWritten = pLaterAsset != nullptr && applySpawnState( pSpawned, *pLaterAsset, instanceName );
                if ( pSpawned != nullptr && bStateWritten == false )
                    pGameObjectManager->destroyObject( pSpawned );
            } );
            return pGameObject;
        }

        if ( applySpawnState( pGameObject, *pAsset, pInstanceNameUtf8 ) == false )
        {
            pGameObjectManager->destroyObject( pGameObject );
            return nullptr;
        }
        return pGameObject;
    }

    bool PrefabCache::applySpawnState( GameObject* pGameObject, const PrefabAsset& asset, string_view instanceName )
    {
        if ( StringUtil::trim( asset.getStateData() ).empty() == false )
        {
            if ( asset.applyStateTo( pGameObject ) == false )
            {
                SW_LOG_ERROR( "ObjectState apply failed for '%#' — spawn aborted", instanceName );
                return false;
            }
            pGameObject->setName( hashed_string( string( instanceName ).c_str() ) );
        }
        (void)spawnNestedInstances( pGameObject, asset, {} ); // 건너뛴 노드는 노드마다 경고했다 — 바깥 인스턴스는 남는다
        return true;
    }

    bool PrefabCache::makeNestedInstanceState( const PrefabInstanceNode& node, string& outStateXML, const PrefabAsset** ppOutSource )
    {
        *ppOutSource = nullptr;
        outStateXML.clear();
        if ( node.isResolved() )
        {
            outStateXML = node._resolvedStateXML;
            return true;
        }

        const PrefabAsset* pSource = loadPrefab( node._prefabPath.empty() ? node._prefabGuid : node._prefabPath );
        if ( pSource == nullptr || pSource->isValid() == false )
        {
            SW_LOG_WARNING( "Nested prefab '%#' could not be loaded - instance '%#' is skipped", node._prefabPath, node._name );
            return false;
        }
        // XML 본문은 그대로 원형으로 쓴다(스폰마다 임시 매니저를 만들지 않는다). 오버라이드의 덮어쓴 칸은 이름으로 얹히므로 원형에 없는 칸은 더해진다.
        string      convertedBase;
        string_view baseState = pSource->getStateData();
        if ( pSource->getStateFormat() != PrefabStateFormat::XML )
        {
            if ( PrefabOverrides::makeBaseState( *pSource, convertedBase ) == false )
            {
                SW_LOG_WARNING( "Nested prefab '%#' could not be built as a base state - instance '%#' is skipped", node._prefabPath, node._name );
                return false;
            }
            baseState = convertedBase;
        }
        if ( PrefabOverrides::makeInstanceState( baseState, node._overrideXML, node._name, outStateXML ) == false )
        {
            SW_LOG_WARNING( "Overrides of nested prefab instance '%#' (%#) could not be applied - it is skipped", node._name, node._prefabPath );
            return false;
        }
        *ppOutSource = pSource;
        return true;
    }

    uint32 PrefabCache::spawnNestedInstances( GameObject* pRoot, const PrefabAsset& asset, const vector<PrefabPartOverride>& listPartOverride )
    {
        vector<string> listSpawnedPath;
        const uint32   createdCount = spawnNestedInstancesUnder( pRoot, asset, {}, listPartOverride, listSpawnedPath );
        for ( const PrefabPartOverride& part : listPartOverride )
        {
            if ( std::find( listSpawnedPath.begin(), listSpawnedPath.end(), part._partPath ) == listSpawnedPath.end() )
                SW_LOG_WARNING( "Override of nested prefab part '%#' in '%#' is dropped - '%#' no longer spawns that part", part._partPath,
                                pRoot != nullptr ? pRoot->getName().c_str() : "?", asset.getName() );
        }
        return createdCount;
    }

    uint32 PrefabCache::spawnNestedInstancesUnder( GameObject* pRoot, const PrefabAsset& asset, string_view rootPath, const vector<PrefabPartOverride>& listPartOverride,
                                                   vector<string>& inoutListSpawnedPath )
    {
        const vector<PrefabInstanceNode>& listNode = asset.getNestedInstances();
        GameObjectManager*                pManager = pRoot != nullptr ? pRoot->getManager() : nullptr;
        if ( listNode.empty() || pManager == nullptr )
            return 0;
        SW_MEMORY_SCOPE( Scene );

        PrefabInstanceRegistry&               registry   = pManager->getSubsystem<PrefabInstanceRegistry>();
        PrefabAssetInternal::PrefabPathStack& spawnStack = PrefabAssetInternal::getSpawnStack();
        vector<GameObject*>                   listCreated( listNode.size(), nullptr );
        uint32                                createdCount = 0;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const PrefabInstanceNode& node    = listNode[index];
            GameObject*               pParent = node._parentIndex < 0 ? pRoot : listCreated[static_cast<size_t>( node._parentIndex )];
            if ( pParent == nullptr )
                continue; // 부모 노드를 건너뛰었다 — 붙을 곳이 없다

            hashed_string sourceKey{};
            if ( node.isResolved() == false )
            {
                sourceKey = PrefabAssetInternal::makePathKey(
                    PrefabAssetInternal::makePrefabCacheKey( PrefabAssetInternal::resolvePrefabPath( node._prefabPath.empty() ? node._prefabGuid : node._prefabPath ) ) );
                if ( PrefabAssetInternal::isOnStack( spawnStack, sourceKey ) )
                {
                    SW_LOG_ERROR( "Circular nested prefab '%#' in '%#' - instance '%#' is not spawned", node._prefabPath, asset.getName(), node._name );
                    continue;
                }
            }

            string             state;
            const PrefabAsset* pSource = nullptr;
            if ( makeNestedInstanceState( node, state, &pSource ) == false )
                continue;
            const string partKey  = node.makePartKey( index );
            const string partPath = PrefabAssetInternal::joinPartPath( rootPath, partKey );
            if ( PrefabAssetInternal::applyPartOverrides( listPartOverride, partPath, state ) == false )
            {
                SW_LOG_WARNING( "Overrides of nested prefab part '%#' (%#) could not be applied - it is skipped", partPath, node._prefabPath );
                continue;
            }

            const string& sourceName = pSource != nullptr ? pSource->getName() : node._name;
            const utf8*   pName      = node._name.empty() == false ? node._name.c_str() : sourceName.c_str();
            GameObject*   pPart      = pManager->createGameObject( hashed_string( StringUtil::isNullOrEmpty( pName ) ? PrefabAssetInternal::kDefaultInstance : pName ) );
            if ( pPart == nullptr )
                continue;
            ObjectLoadContext context{ SerializePurpose::Authoring };
            context._bExternalParentAllowed = false; // 부모는 노드의 자리가 정한다(아래에서 붙인다)
            if ( ObjectStateSerializer::loadFromXMLString( pPart, state, context ) == false )
            {
                SW_LOG_WARNING( "Nested prefab instance '%#' (%#) state could not be applied - it is skipped", node._name, node._prefabPath );
                pManager->destroyObject( pPart );
                continue;
            }
            PrefabAssetInternal::attachNestedInstance( pPart, pParent, node._attachKey );
            registry.registerNestedPart( pPart->getObjectID(), pRoot->getObjectID(), node._prefabPath, partKey );
            inoutListSpawnedPath.push_back( partPath );
            listCreated[index] = pPart;
            ++createdCount;

            // 원본 노드면 원본의 중첩 인스턴스를 이 부품 아래에 스폰한다(펼친 노드는 그것이 뒤 노드로 이미 있다). 노드가 부품 아래에 덮어쓴 것(`<Part>`)을
            // 바깥 것보다 앞에 둔다 — 같은 부품이면 바깥 것이 나중에 얹혀 이긴다.
            if ( pSource != nullptr && pSource->getNestedInstances().empty() == false )
            {
                vector<PrefabPartOverride> listInnerPart;
                if ( PrefabOverrides::collectPartOverrides( node._overrideXML, listInnerPart ) == false )
                    listInnerPart.clear(); // 오류는 알렸다 — 안쪽 덮어쓴 것 없이 스폰한다
                const PrefabAssetInternal::PathStackScope scope{ spawnStack, sourceKey };
                if ( listInnerPart.empty() )
                {
                    createdCount += spawnNestedInstancesUnder( pPart, *pSource, partPath, listPartOverride, inoutListSpawnedPath );
                    continue;
                }
                PrefabAssetInternal::prependInnerParts( partPath, listInnerPart, listPartOverride );
                createdCount += spawnNestedInstancesUnder( pPart, *pSource, partPath, listInnerPart, inoutListSpawnedPath );
            }
        }
        return createdCount;
    }

    bool PrefabCache::collectPartOverrides( const GameObject& root, const PrefabAsset& asset, vector<PrefabPartOverride>& outListPartOverride )
    {
        outListPartOverride.clear();
        GameObjectManager* pManager = root.getManager();
        if ( asset.getNestedInstances().empty() || pManager == nullptr )
            return true;
        const PrefabInstanceRegistry& registry = pManager->getSubsystem<PrefabInstanceRegistry>();
        vector<uint64>                listPartID;
        PrefabAssetInternal::collectPartTree( registry, root.getObjectID(), listPartID );
        if ( listPartID.empty() )
            return true;

        // 기준: 같은 프리팹을 덮어쓴 것 없이 씬 밖에서 스폰한 부품(스폰과 같은 식이라 고치지 않은 부품은 차이가 없다).
        GameObjectManager scratch;
        GameObject*       pScratchRoot = scratch.createGameObject( hashed_string( asset.getName().empty() ? PrefabAssetInternal::kDefaultInstance : asset.getName().c_str() ) );
        if ( pScratchRoot == nullptr || applySpawnState( pScratchRoot, asset, asset.getName() ) == false )
            return false;
        const PrefabInstanceRegistry& scratchRegistry = scratch.getSubsystem<PrefabInstanceRegistry>();
        vector<uint64>                listScratchPartID;
        PrefabAssetInternal::collectPartTree( scratchRegistry, pScratchRoot->getObjectID(), listScratchPartID );
        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = true; // 부모는 노드의 자리가 정한다
        unordered_map<string, string> mapBaseState;
        for ( const uint64 scratchPartID : listScratchPartID )
        {
            string partPath;
            if ( scratchRegistry.makePartPath( scratchPartID, pScratchRoot->getObjectID(), partPath ) )
                mapBaseState[partPath] = ObjectStateSerializer::saveToXMLString( scratch.findGameObjectByID( scratchPartID ), options );
        }

        for ( const uint64 partID : listPartID )
        {
            const GameObject* pPart       = pManager->findGameObjectByID( partID );
            const bool        bLivingPart = pPart != nullptr && pPart->isPendingDestroy() == false && registry.isAttachedNestedPart( pPart );
            string            partPath;
            if ( bLivingPart == false || registry.makePartPath( partID, root.getObjectID(), partPath ) == false )
                continue; // 지운 · 옮긴 부품, 키 없이 들인 부품은 이 인스턴스의 덮어쓴 것이 아니다
            const auto baseIt = mapBaseState.find( partPath );
            if ( baseIt == mapBaseState.end() )
                continue; // 프리팹이 더는 스폰하지 않는 부품 — 다음 로드에 다시 생기지 않는다
            PrefabPartOverride part{ partPath, {} };
            if ( PrefabOverrides::computeOverrides( ObjectStateSerializer::saveToXMLString( pPart, options ), baseIt->second, part._overrideXML ) == false )
                return false;
            if ( part._overrideXML.empty() == false )
                outListPartOverride.push_back( std::move( part ) );
        }
        return true;
    }

    bool PrefabCache::flattenNestedInstances( PrefabAsset& inoutAsset )
    {
        if ( inoutAsset.getNestedInstances().empty() )
            return true;
        vector<PrefabInstanceNode> listFlat;
        if ( appendFlattenedNodes( inoutAsset, -1, {}, {}, listFlat ) == false )
            return false;
        return inoutAsset.setNestedInstances( std::move( listFlat ) );
    }

    bool PrefabCache::appendFlattenedNodes( const PrefabAsset& asset, int32 parentIndex, string_view parentPath, const vector<PrefabPartOverride>& listPartOverride,
                                            vector<PrefabInstanceNode>& inoutListNode )
    {
        const vector<PrefabInstanceNode>&     listNode = asset.getNestedInstances();
        vector<int32>                         listFlatIndex( listNode.size(), -1 );
        PrefabAssetInternal::PrefabPathStack& spawnStack = PrefabAssetInternal::getSpawnStack();
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const PrefabInstanceNode& node       = listNode[index];
            const int32               flatParent = node._parentIndex < 0 ? parentIndex : listFlatIndex[static_cast<size_t>( node._parentIndex )];
            const string              partPath   = PrefabAssetInternal::joinPartPath( parentPath, node.makePartKey( index ) );
            if ( node.isResolved() )
            {
                PrefabInstanceNode copied = node;
                copied._parentIndex       = flatParent;
                copied._partPath          = partPath;
                if ( PrefabAssetInternal::applyPartOverrides( listPartOverride, partPath, copied._resolvedStateXML ) == false )
                {
                    SW_LOG_ERROR( "Overrides of nested prefab part '%#' in '%#' could not be applied - the prefab is not flattened", partPath, asset.getName() );
                    return false;
                }
                listFlatIndex[index] = static_cast<int32>( inoutListNode.size() );
                inoutListNode.push_back( std::move( copied ) );
                continue;
            }

            const string        sourcePath = PrefabAssetInternal::resolvePrefabPath( node._prefabPath.empty() ? node._prefabGuid : node._prefabPath );
            const hashed_string sourceKey  = PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( sourcePath ) );
            const PrefabAsset*  pSource    = loadPrefab( sourcePath );
            if ( pSource == nullptr || PrefabAssetInternal::isOnStack( spawnStack, sourceKey ) )
            {
                SW_LOG_ERROR( "Nested prefab '%#' in '%#' could not be loaded (missing or circular) - the prefab is not flattened", sourcePath, asset.getName() );
                return false;
            }
            string baseState;
            if ( PrefabOverrides::makeBaseState( *pSource, baseState ) == false )
            {
                SW_LOG_ERROR( "Nested prefab '%#' could not be built as a base state - '%#' is not flattened", sourcePath, asset.getName() );
                return false;
            }
            PrefabInstanceNode flat{};
            flat._name        = node._name.empty() ? pSource->getName() : node._name;
            flat._prefabPath  = node._prefabPath;
            flat._prefabGuid  = node._prefabGuid;
            flat._attachKey   = node._attachKey;
            flat._partPath    = partPath;
            flat._parentIndex = flatParent;
            vector<PrefabPartOverride> listInnerPart;
            const bool                 bStateMade = PrefabOverrides::makeInstanceState( baseState, node._overrideXML, flat._name, flat._resolvedStateXML ) &&
                                    PrefabAssetInternal::applyPartOverrides( listPartOverride, partPath, flat._resolvedStateXML ) &&
                                    PrefabOverrides::collectPartOverrides( node._overrideXML, listInnerPart );
            if ( bStateMade == false || flat.isResolved() == false )
            {
                SW_LOG_ERROR( "Overrides of nested prefab instance '%#' in '%#' could not be applied - the prefab is not flattened", flat._name, asset.getName() );
                return false;
            }
            const int32 flatIndex = static_cast<int32>( inoutListNode.size() );
            listFlatIndex[index]  = flatIndex;
            inoutListNode.push_back( std::move( flat ) );

            // 노드가 부품 아래에 덮어쓴 것(`<Part>`)은 스폰과 같은 순서(안쪽 먼저, 바깥 것이 이김)로 아래 노드의 펼친 상태에 굽는다.
            PrefabAssetInternal::prependInnerParts( partPath, listInnerPart, listPartOverride );
            const PrefabAssetInternal::PathStackScope scope{ spawnStack, sourceKey };
            if ( appendFlattenedNodes( *pSource, flatIndex, partPath, listInnerPart, inoutListNode ) == false )
                return false;
        }
        return true;
    }

    uint32 PrefabCache::cookAllPrefabs( string_view sourceRoot, string_view cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        outFailedCount = 0;
        if ( sourceRoot.empty() || cookedDir.empty() )
        {
            SW_LOG_ERROR( "Prefab cook needs a source root and an output directory (--cooked-dir)." );
            return 0;
        }

        vector<string> listFile;
        FileUtil::collectFiles( sourceRoot, {}, listFile, true );
        // 순서를 정한다 — 같은 쿠킹본을 쓰는 두 소스 가운데 어느 것이 먼저인지가 실행마다 같아야 한다.
        std::sort( listFile.begin(), listFile.end() );

        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot ) );
        string       out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir ) );
        out += '/';

        vector<string>    listWrittenKey;
        CookTally         tally;
        const CookJobDesc desc = makeCookJobDesc();
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            const bool   bXML       = StringUtil::endsWith( normalized, ".prefab.xml", true );
            const bool   bJSON      = StringUtil::endsWith( normalized, ".prefab.json", true );
            if ( ( bXML || bJSON ) == false || normalized.size() <= root.size() + 1 )
                continue;

            // 출력은 `<cookedDir>/<소스 루트 기준 상대 경로>` 의 쿠킹본 이름이다 — 런타임 `loadPrefab` 과 같은 규칙 하나(`AssetCookPath`).
            const string outputID   = AssetCookPath::toCookedPath( normalized.substr( root.size() + 1 ) );
            const string outputPath = out + outputID;
            const string outputKey  = StringUtil::toLower( outputPath.c_str() );
            if ( std::find( listWrittenKey.begin(), listWrittenKey.end(), outputKey ) != listWrittenKey.end() )
            {
                SW_LOG_WARNING( "Prefab cook: '%#' would overwrite the cooked file another source already wrote ('%#') - skipped", normalized, outputPath );
                ++outFailedCount;
                continue;
            }

            const CookOutcome outcome = store.cookFile( desc, outputID, outputPath, [&]()
            {
                // 중첩 인스턴스의 원본을 읽는 캐시는 작업마다 새로 둔다 — 앞 작업이 읽어 둔 원본을 쓰면 그 읽기가 이 작업의 입력에서 빠진다.
                PrefabCache nestedPrefabs;
                PrefabAsset asset;
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                const bool bLoaded    = asset.loadFromDiskFile( normalized );
                const bool bFlattened = bLoaded && asset.isValid() && nestedPrefabs.flattenNestedInstances( asset );
                if ( bFlattened == false || asset.saveToBinaryFile( outputPath ) == false )
                {
                    SW_LOG_WARNING( "Prefab cook failed for '%#'", normalized );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            } );
            if ( outcome == CookOutcome::Failed )
            {
                ++outFailedCount;
                continue;
            }
            listWrittenKey.push_back( outputKey );
            tally.add( outcome );
        }
        SW_LOG_INFO( "Prefab cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }

    CookJobDesc PrefabCache::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName    = "Prefab";
        desc._cookerVersion = kCookVersion;
        desc._target        = "Any";
        desc._settingsHash  = 0; // 프리팹 쿠킹에는 설정이 없다
        return desc;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( PrefabCache );
} // namespace sw
