#include "pch.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        struct PrefabAssetInternal
        {
            /** @brief 프리팹 캐시의 계측 칸입니다(`CacheStats`). 캐시는 에셋 매니저마다 하나지만 칸은 프로세스에 하나라 매니저를 다시 세워도 이어 센다. */
            static const CacheStatsHandle& getCacheStats()
            {
                static const CacheStatsHandle s_stats = CacheStats::getOrCreate( PrefabCache::kAssetKindName );
                return s_stats;
            }

            /**
             * @brief 스폰 중인 프리팹 경로 스택(순환 참조 검사)이 힙 없이 담는 깊이입니다.
             * @details 스레드 지역 스택이라 힙에 잡으면 처음 스폰한 자리의 태그(Scene)로 스레드가 끝날 때까지 남는다 — 종료 보고는
             *          스레드보다 먼저 온다. 더 깊게 중첩하면 그때만 힙을 쓴다.
             */
            static constexpr size_t kInlineSpawnDepth = 8;
            /** @brief 로드 검증에서 순환이 없다는 값입니다(`PrefabThreadChain::_cycleFloor`). */
            static constexpr size_t kNoCycle = static_cast<size_t>( constant::invalid_index::kUint64 );

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

            static constexpr const utf8* kRoot            = "Prefab";
            static constexpr const utf8* kName            = "name";
            static constexpr const utf8* kGameObject      = "GameObject";
            static constexpr const utf8* kDefaultInstance = "PrefabInstance";
            static constexpr const utf8* kNestedInstance  = "PrefabInstance";
            static constexpr const utf8* kNestedPrefab    = "prefab";
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

            /** @brief @p path 가 목록에 없으면 끝에 더합니다(대소문자 · 구분자를 무시한 캐시 키로 비교한다). */
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
                        SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceAttaches", "Nested prefab instance '%#' attaches to '%#', which '%#' does not have - it is attached to the primary scene component",
                                           pChild->getName().c_str(), attachKey, pParent->getName().c_str() );
                }
                if ( pTarget == nullptr )
                {
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceNo", "Nested prefab instance '%#' has no scene component of '%#' to attach to - it is left at the root", pChild->getName().c_str(),
                                       pParent->getName().c_str() );
                    return;
                }
                if ( pChildRoot->attachToComponent( pTarget ) == false )
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceCould", "Nested prefab instance '%#' could not be attached to '%#'", pChild->getName().c_str(), pParent->getName().c_str() );
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
             * @brief 노드가 자기 부품 아래에 덮어쓴 것 @p inoutListInner(부품 @p partPath 에서 측정한 경로)를 바깥 루트에서 측정한 경로로 바꾸고, 그 뒤에 바깥 것 @p listEnclosingPart 를 잇습니다.
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
                        deepPart._partPath = joinPartPath( to_string( static_cast<uint64>( index ) ), deepPart._partPath );
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
         * @brief 한 스레드가 프리팹을 펼치는 호출 사슬의 상태입니다 — 소유자는 그 스레드입니다.
         * @details 재진입하는 호출 사슬(컴포넌트가 로드 뒤 같은 프리팹을 스폰한다 · 원본을 로드하다 중첩 원본을 로드한다)의 순환을 막는 기록이라, 공유 상태가 아니라
         *          사슬을 도는 스레드의 상태다. 씬을 읽는 워커와 게임 스레드가 동시에 펼치므로 스레드마다 하나이고, 한 이름공간 변수(함수 안 정적 지역 셋이 아니다)에 모았다.
         */
        struct PrefabThreadChain
        {
            /** @brief 이 스레드가 스폰 중인 프리팹 경로입니다. 스폰 · 중첩 스폰 · 펼치기가 같이 씁니다.
             *         컴포넌트가 로드 뒤 같은 프리팹을 스폰하는 순환(`PrefabTest.CircularReferenceSpawnProtection`)과, 로드한 뒤 원본을 고쳐 생긴 중첩 순환을 막는다. */
            PrefabAssetInternal::PrefabPathStack _spawnStack;
            /** @brief 이 스레드가 중첩 인스턴스를 검증하며 로드 중인 프리팹 경로입니다(`PrefabCache::validateNestedInstances`). 스폰 스택과 따로 둔다 — 런타임 스폰이 로드를 실패시키지 않게. */
            PrefabAssetInternal::PrefabPathStack _loadStack;
            /** @brief 로드 스택에서 찾은 순환의 시작 깊이입니다. 순환에 든 프리팹(그 깊이 이상)은 로드에 실패하고, 시작 깊이의 프리팹이 끝날 때 지운다. */
            size_t _cycleFloor{ PrefabAssetInternal::kNoCycle };
            /** @brief 이 스레드가 변형을 읽으며 따라가는 부모 경로입니다(순환 · 깊이 검사, `PrefabAsset::resolveVariant`). */
            PrefabAssetInternal::PrefabPathStack _variantStack;
        };

        thread_local PrefabThreadChain t_prefabChain;
    } // namespace
} // namespace sw

namespace sw
{
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
        PrefabAssetInternal::PrefabPathStack& stack = t_prefabChain._variantStack;
        const string                          baseID{ basePath };
        const hashed_string                   baseKey = PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( baseID ) );
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
        const PrefabAssetInternal::PathStackScope scope{ stack, baseKey };

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

    string PrefabCache::makeCacheKey( string_view assetRelativePath )
    {
        return PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );
    }

    bool PrefabCache::isCached( string_view assetRelativePath ) const
    {
        if ( assetRelativePath.empty() )
            return false;

        const string                   cacheKey = PrefabAssetInternal::makePrefabCacheKey( assetRelativePath );
        ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.find( cacheKey ) != _mapCache.end();
    }

    size_t PrefabCache::getCachedCount() const
    {
        ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
        return _mapCache.size();
    }

    void PrefabCache::clear()
    {
        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        _cacheGeneration.fetch_add( 1, std::memory_order_acq_rel );
        _mapLoading.clear();
        _mapCache.clear();
        PrefabAssetInternal::getCacheStats().recordClear();
    }

    PrefabCache::PrefabCache()
        : AssetCacheBase{ CacheTracking::counted( kAssetKindName ) }
        , _mapCacheMutex{}
        , _mapCache{}
        , _mapLoading{}
        , _cacheGeneration{ 0 }
    {
    }

    PrefabCache::~PrefabCache()
    {
        // 워커가 아직 이 캐시로 읽고 있을 수 있다(로드 작업이 원시 포인터를 쥔다). 모두 끝날 때까지 기다린다 — 아무도 집지 않은 로드는 여기서 실행한다.
        vector<shared_ptr<PrefabLoadOperation>> listLoading;
        {
            UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
            for ( const auto& [key, weakOperation] : _mapLoading )
            {
                shared_ptr<PrefabLoadOperation> pOperation = weakOperation.lock();
                if ( pOperation != nullptr )
                    listLoading.push_back( std::move( pOperation ) );
            }
        }
        for ( const shared_ptr<PrefabLoadOperation>& pOperation : listLoading )
        {
            pOperation->wait();
        }
    }

    PrefabLoadOperation::PrefabLoadOperation( PrefabCache* pCache, string_view resolvedPath, string cacheKey )
        : AssetLoadOperation{ PrefabCache::kAssetKindName, resolvedPath, MemoryTag::Scene }
        , _pCache{ pCache }
        , _cacheKey{ std::move( cacheKey ) }
        , _pAsset{}
    {
    }

    expected<void> PrefabLoadOperation::runLoad()
    {
        _pAsset = _pCache->loadAndCache( getPath() );
        {
            // 결과가 캐시에 든 **뒤에** 진행 중 표에서 뺀다 — 그 사이에 온 요청은 진행 중 로드 작업이나 캐시 둘 중 하나를 반드시 본다.
            // 이 로드 작업의 칸일 때만 뺀다(다시 읽기가 표를 비운 뒤 새 요청이 새 로드 작업을 넣었을 수 있다).
            UniqueLock<shared_mutex> writeLock{ _pCache->_mapCacheMutex };
            const auto               it = _pCache->_mapLoading.find( _cacheKey );
            if ( it != _pCache->_mapLoading.end() && ( it->second.expired() || it->second.lock().get() == this ) )
                _pCache->_mapLoading.erase( it );
        }
        if ( _pAsset == nullptr )
        {
            return unexpected( Error{ ErrorKind::InvalidArgument, "the prefab could not be loaded (the loader logged why)", getPath() } );
        }
        return {};
    }

    PrefabHandle PrefabCache::request( string_view assetRelativePath )
    {
        return requestWith( assetRelativePath, AssetLoadStart::Async );
    }

    shared_ptr<const void> PrefabCache::pinAsset( string_view relativePath )
    {
        if ( AssetTypeRegistry::isPathOfType( relativePath, PrefabAsset::kAssetType ) == false )
            return {};
        return request( relativePath ).getOperation();
    }

    PrefabHandle PrefabCache::requestWith( string_view assetRelativePath, AssetLoadStart start )
    {
        const string resolvedPath = string( assetRelativePath );
        const string cacheKey     = PrefabAssetInternal::makePrefabCacheKey( resolvedPath );

        shared_ptr<PrefabLoadOperation> pOperation;
        {
            UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
            const auto               loadingIt = _mapLoading.find( cacheKey );
            if ( loadingIt != _mapLoading.end() )
                pOperation = loadingIt->second.lock();
            if ( pOperation != nullptr )
            {
                PrefabAssetInternal::getCacheStats().recordHit();
                return PrefabHandle{ std::move( pOperation ) };
            }

            // 진행 중 표를 잡은 채 캐시를 본다 — 로드는 캐시에 넣은 뒤 표에서 빼므로, 여기서 둘 다 없으면 정말 아무도 읽지 않았다.
            pOperation         = sw::make_shared<PrefabLoadOperation>( this, resolvedPath, cacheKey );
            const auto cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
            {
                pOperation->_pAsset = cacheIt->second;
                pOperation->markReadyWithoutLoad();
                PrefabAssetInternal::getCacheStats().recordHit();
                return PrefabHandle{ std::move( pOperation ) };
            }
            _mapLoading[cacheKey] = pOperation;
            PrefabAssetInternal::getCacheStats().recordMiss();
        }
        AssetLoadOperation::startLoad( pOperation, start );
        return PrefabHandle{ std::move( pOperation ) };
    }

    shared_ptr<const PrefabAsset> PrefabCache::loadPrefab( string_view assetRelativePath )
    {
        // 기다림이 계약이다 — 씬 · 중첩 프리팹 · 에디터 명령은 받은 자산으로 그 자리에서 객체를 짓는다. 기다리지 않을 곳은 `request` 핸들을 든다.
        return requestWith( assetRelativePath, AssetLoadStart::OnCallingThread ).wait();
    }

    shared_ptr<const PrefabAsset> PrefabCache::loadAndCache( string_view assetRelativePath )
    {
        SW_MEMORY_SCOPE( Scene );
        const string resolvedPath = string( assetRelativePath );

        const string cacheKey   = PrefabAssetInternal::makePrefabCacheKey( resolvedPath );
        const uint32 generation = _cacheGeneration.load( std::memory_order_acquire );
        {
            ScopedSharedLock<shared_mutex> readLock{ _mapCacheMutex };
            const auto                     cacheIt = _mapCache.find( cacheKey );
            if ( cacheIt != _mapCache.end() )
                return cacheIt->second;
        }

        shared_ptr<PrefabAsset> asset = sw::make_shared<PrefabAsset>();

        // 쿠킹본 이름은 쿠커(`cookAllPrefabs`)와 같은 규칙 하나다(`AssetCookPath`).
        string binPath = AssetCookPath::toCookedPath( resolvedPath );
        if ( binPath.empty() )
            binPath = resolvedPath;

        if ( AssetIO::readsSource() == false )
        {
            if ( asset->loadFromBinaryFile( AssetID{ binPath } ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.CookedContentRequiresCooked", "Cooked content requires the cooked prefab: %#", binPath );
                return nullptr;
            }
        }
        else
        {
            // 소스를 읽는 구성은 소스(XML/JSON)만 본다. 소스가 없을 때 옆에 남은 쿠킹본(.bin)을 대신 읽으면 옮기거나 지운 프리팹이
            // 낡은 내용으로 살아나 실패가 가려진다(언리얼 · 유니티의 에디터도 쿠킹 데이터를 보지 않는다).
            const bool bJSON         = FileUtil::hasExtension( resolvedPath, ".json" );
            const bool bSourceLoaded = bJSON ? asset->loadFromJSONFile( AssetID{ resolvedPath } ) : asset->loadFromXMLFile( AssetID{ resolvedPath } );
            if ( bSourceLoaded == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.PrefabSourceCouldNot", "Prefab source could not be loaded: %#", resolvedPath );
                return nullptr;
            }
        }

        // 중첩 인스턴스가 순환하면 로드 오류다(캐시에 넣지 않는다). 원본은 여기서 재귀로 로드되어 캐시에 든다.
        if ( validateNestedInstances( *asset, cacheKey ) == false )
            return nullptr;

        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        const auto               cacheIt = _mapCache.find( cacheKey );
        if ( cacheIt != _mapCache.end() )
            return cacheIt->second;
        // 읽는 사이 캐시를 버렸다(다시 읽기 · 비우기) — 옛 파일을 읽었을 수 있으니 넣지 않고 요청한 쪽에만 준다.
        if ( _cacheGeneration.load( std::memory_order_acquire ) != generation )
            return asset;

        _mapCache.emplace( cacheKey, asset );
        return asset;
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

        PrefabAssetInternal::PrefabPathStack& stack      = t_prefabChain._loadStack;
        size_t&                               cycleFloor = t_prefabChain._cycleFloor;
        const size_t                          depth      = stack.size();
        {
            const PrefabAssetInternal::PathStackScope scope{ stack, PrefabAssetInternal::makePathKey( cacheKey ) };
            for ( const PrefabInstanceNode& node : asset.getNestedInstances() )
            {
                if ( node.isResolved() )
                    continue;
                const string           childPath  = string( node._prefabPath );
                const optional<size_t> stackIndex = VectorUtil::findIndex( stack, PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( childPath ) ) );
                if ( stackIndex.has_value() )
                {
                    cycleFloor = MathUtil::min( cycleFloor, *stackIndex );
                    SW_LOG_ERROR_ID( "PrefabAsset.CircularNestedPrefabReference", "Circular nested prefab reference: '%#' places '%#', which is already being loaded", asset.getName(), childPath );
                    continue;
                }
                // 원본은 이 스레드에서 읽는다(`loadAndCache`) — 다른 스레드의 로드 작업을 기다리면 순환한 두 프리팹을 두 워커가 읽을 때 서로를 기다린다.
                const bool bLoadFailed = loadAndCache( childPath ) == nullptr;
                if ( bLoadFailed && cycleFloor > depth )
                    SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot2", "Nested prefab '%#' in '%#' could not be loaded - that instance is skipped when spawning", childPath, asset.getName() );
            }
        }

        const bool bInCycle = cycleFloor <= depth;
        if ( cycleFloor == depth )
            cycleFloor = PrefabAssetInternal::kNoCycle; // 순환이 여기서 시작했다 — 바깥 프리팹은 그 노드만 건너뛰고 로드된다
        if ( bInCycle )
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabPartCircularNested", "Prefab '%#' is part of a circular nested prefab reference - it is not loaded", asset.getName() );
        return bInCycle == false;
    }

    GameObject* PrefabCache::spawn( GameObjectManager* pGameObjectManager, string_view assetRelativePath, const utf8* pInstanceName )
    {
        SW_MEMORY_SCOPE( Scene );
        // 이 함수는 나머지 포인터를 모두 검사한다(`pAsset` · `pGameObject` · `pInstanceName`).
        // 활성 씬이 없을 때 `getObjectManager()` 는 nullptr 를 반환한다.
        if ( pGameObjectManager == nullptr )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.CannotSpawnPrefabWithout", "Cannot spawn prefab '%#' without a GameObjectManager.", assetRelativePath );
            return nullptr;
        }

        const string        resolvedPath = string( assetRelativePath );
        const hashed_string pathKey      = PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( resolvedPath ) );

        PrefabAssetInternal::PrefabPathStack& spawnStack = t_prefabChain._spawnStack;
        if ( VectorUtil::contains( spawnStack, pathKey ) )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.CircularPrefabReferenceDetected", "Circular prefab reference detected for '%#' — spawn aborted to prevent recursion overflow",
                             assetRelativePath );
            return nullptr;
        }
        const PrefabAssetInternal::PathStackScope scope{ spawnStack, pathKey };

        const shared_ptr<const PrefabAsset> pAsset = loadPrefab( assetRelativePath );
        if ( pAsset == nullptr || pAsset->isValid() == false )
            return nullptr;

        const utf8* pInstanceNameUTF8 = pInstanceName != nullptr ? pInstanceName : pAsset->getName().c_str();
        if ( StringUtil::isNullOrEmpty( pInstanceNameUTF8 ) )
            pInstanceNameUTF8 = PrefabAssetInternal::kDefaultInstance;

        GameObject* pGameObject = pGameObjectManager->createGameObject( hashed_string( pInstanceNameUTF8 ) );
        if ( pGameObject == nullptr )
            return nullptr;

        // 컴포넌트 틱 중이면 오브젝트는 지금 만들어 돌려주고(부르는 쪽이 핸들을 든다), 상태는 틱 직후 구조 변경 큐에서 채운다. 그 자리에서
        // 상태를 읽으면 컴포넌트 추가가 모두 미뤄져(nullptr) **프리팹의 값이 버려지고** 빈 오브젝트만 남는다.
        if ( pGameObjectManager->isStructuralMutationFrozen() )
        {
            const uint64 objectID = pGameObject->getObjectID();
            const string instanceName( pInstanceNameUTF8 );
            // 캐시의 프리팹은 그 사이 다시 읽힐 수 있다(에디터 핫 리로드) — 포인터가 아니라 경로를 들고 그때 다시 찾는다.
            pGameObjectManager->deferStructuralChange( [this, pGameObjectManager, objectID, resolvedPath, instanceName, pathKey]()
            {
                // 미룬 자리에서도 이 프리팹을 펼치는 중이라고 적는다 — 중첩 스폰의 순환 검사가 이 경로를 본다.
                const PrefabAssetInternal::PathStackScope laterScope{ t_prefabChain._spawnStack, pathKey };
                GameObject*                               pSpawned      = pGameObjectManager->findGameObjectByID( objectID );
                const shared_ptr<const PrefabAsset>       pLaterAsset   = ( pSpawned != nullptr ) ? loadPrefab( resolvedPath ) : nullptr;
                const bool                                bStateWritten = pLaterAsset != nullptr && applySpawnState( pSpawned, *pLaterAsset, instanceName );
                if ( pSpawned != nullptr && bStateWritten == false )
                    pGameObjectManager->destroyGameObject( pSpawned );
            } );
            return pGameObject;
        }

        if ( applySpawnState( pGameObject, *pAsset, pInstanceNameUTF8 ) == false )
        {
            pGameObjectManager->destroyGameObject( pGameObject );
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
                SW_LOG_ERROR_ID( "PrefabAsset.ObjectStateApplyFailedSpawn", "ObjectState apply failed for '%#' — spawn aborted", instanceName );
                return false;
            }
            pGameObject->setName( hashed_string( string( instanceName ).c_str() ) );
        }
        (void)spawnNestedInstances( pGameObject, asset, {} ); // 건너뛴 노드는 노드마다 경고했다 — 바깥 인스턴스는 남는다
        return true;
    }

    bool PrefabCache::makeNestedInstanceState( const PrefabInstanceNode& node, string& outStateXML, shared_ptr<const PrefabAsset>& outSource )
    {
        outSource.reset();
        outStateXML.clear();
        if ( node.isResolved() )
        {
            outStateXML = node._resolvedStateXML;
            return true;
        }

        const shared_ptr<const PrefabAsset> pSource = loadPrefab( node._prefabPath );
        if ( pSource == nullptr || pSource->isValid() == false )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot3", "Nested prefab '%#' could not be loaded - instance '%#' is skipped", node._prefabPath, node._name );
            return false;
        }
        // XML 본문은 그대로 원형으로 쓴다(스폰마다 임시 매니저를 만들지 않는다). 오버라이드의 덮어쓴 칸은 이름으로 얹히므로 원형에 없는 칸은 더해진다.
        string      convertedBase;
        string_view baseState = pSource->getStateData();
        if ( pSource->getStateFormat() != PrefabStateFormat::XML )
        {
            if ( PrefabOverrides::makeBaseState( *pSource, convertedBase ) == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabCouldNot4", "Nested prefab '%#' could not be built as a base state - instance '%#' is skipped", node._prefabPath, node._name );
                return false;
            }
            baseState = convertedBase;
        }
        if ( PrefabOverrides::makeInstanceState( baseState, node._overrideXML, node._name, outStateXML ) == false )
        {
            SW_LOG_WARNING_ID( "PrefabAsset.OverridesNestedPrefabInstance2", "Overrides of nested prefab instance '%#' (%#) could not be applied - it is skipped", node._name, node._prefabPath );
            return false;
        }
        outSource = pSource;
        return true;
    }

    uint32 PrefabCache::spawnNestedInstances( GameObject* pRoot, const PrefabAsset& asset, const vector<PrefabPartOverride>& listPartOverride )
    {
        // 변형이 물려받은 부품에 덮어쓴 것이 앞이다 — 인스턴스가 덮어쓴 것이 나중에 얹혀 이긴다.
        vector<PrefabPartOverride> listAllPart = asset.getVariantPartOverrides();
        listAllPart.insert( listAllPart.end(), listPartOverride.begin(), listPartOverride.end() );
        vector<string> listSpawnedPath;
        const uint32   createdCount = spawnNestedInstancesUnder( pRoot, asset, {}, listAllPart, listSpawnedPath );
        for ( const PrefabPartOverride& part : listAllPart )
        {
            if ( VectorUtil::contains( listSpawnedPath, part._partPath ) == false )
                SW_LOG_WARNING_ID( "Prefab.NestedPartOverrideDropped", "Override of nested prefab part '%#' in '%#' is dropped - '%#' no longer spawns that part", part._partPath,
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
        PrefabAssetInternal::PrefabPathStack& spawnStack = t_prefabChain._spawnStack;
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
                    PrefabAssetInternal::makePrefabCacheKey( string( node._prefabPath ) ) );
                if ( VectorUtil::contains( spawnStack, sourceKey ) )
                {
                    SW_LOG_ERROR_ID( "PrefabAsset.CircularNestedPrefabInstance", "Circular nested prefab '%#' in '%#' - instance '%#' is not spawned", node._prefabPath, asset.getName(), node._name );
                    continue;
                }
            }

            string                        state;
            shared_ptr<const PrefabAsset> pSource;
            if ( makeNestedInstanceState( node, state, pSource ) == false )
                continue;
            const string partKey  = node.makePartKey( index );
            const string partPath = PrefabAssetInternal::joinPartPath( rootPath, partKey );
            if ( PrefabAssetInternal::applyPartOverrides( listPartOverride, partPath, state ) == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.OverridesNestedPrefabPart", "Overrides of nested prefab part '%#' (%#) could not be applied - it is skipped", partPath, node._prefabPath );
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
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPrefabInstanceState", "Nested prefab instance '%#' (%#) state could not be applied - it is skipped", node._name, node._prefabPath );
                pManager->destroyGameObject( pPart );
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
                    listInnerPart.clear();                                                                                                           // 오류는 알렸다 — 안쪽 덮어쓴 것 없이 스폰한다
                listInnerPart.insert( listInnerPart.begin(), pSource->getVariantPartOverrides().begin(), pSource->getVariantPartOverrides().end() ); // 원본이 변형이면 그것이 가장 안쪽이다
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
        options._bOmitExternalParent = SW_TRUE; // 부모는 노드의 자리가 정한다
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
            if ( const expected<void> overrides = PrefabOverrides::computeOverrides( ObjectStateSerializer::saveToXMLString( pPart, options ), baseIt->second, part._overrideXML );
                 overrides.has_value() == false )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.NestedPartOverridesCould", "Nested part '%#' overrides could not be computed: %# (%#)", partPath.c_str(), overrides.error().message(), overrides.error().detail() );
                return false;
            }
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
        if ( appendFlattenedNodes( inoutAsset, -1, {}, inoutAsset.getVariantPartOverrides(), listFlat ) == false )
            return false;
        return inoutAsset.setNestedInstances( std::move( listFlat ) );
    }

    bool PrefabCache::appendFlattenedNodes( const PrefabAsset& asset, int32 parentIndex, string_view parentPath, const vector<PrefabPartOverride>& listPartOverride,
                                            vector<PrefabInstanceNode>& inoutListNode )
    {
        const vector<PrefabInstanceNode>&     listNode = asset.getNestedInstances();
        vector<int32>                         listFlatIndex( listNode.size(), -1 );
        PrefabAssetInternal::PrefabPathStack& spawnStack = t_prefabChain._spawnStack;
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
                    SW_LOG_ERROR_ID( "PrefabAsset.OverridesNestedPrefabPart2", "Overrides of nested prefab part '%#' in '%#' could not be applied - the prefab is not flattened", partPath, asset.getName() );
                    return false;
                }
                listFlatIndex[index] = static_cast<int32>( inoutListNode.size() );
                inoutListNode.push_back( std::move( copied ) );
                continue;
            }

            const string                        sourcePath = string( node._prefabPath );
            const hashed_string                 sourceKey  = PrefabAssetInternal::makePathKey( PrefabAssetInternal::makePrefabCacheKey( sourcePath ) );
            const shared_ptr<const PrefabAsset> pSource    = loadPrefab( sourcePath );
            if ( pSource == nullptr || VectorUtil::contains( spawnStack, sourceKey ) )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabCouldNot5", "Nested prefab '%#' in '%#' could not be loaded (missing or circular) - the prefab is not flattened", sourcePath, asset.getName() );
                return false;
            }
            string baseState;
            if ( PrefabOverrides::makeBaseState( *pSource, baseState ) == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.NestedPrefabCouldNot6", "Nested prefab '%#' could not be built as a base state - '%#' is not flattened", sourcePath, asset.getName() );
                return false;
            }
            PrefabInstanceNode flat{};
            flat._name        = node._name.empty() ? pSource->getName() : node._name;
            flat._prefabPath  = node._prefabPath;
            flat._attachKey   = node._attachKey;
            flat._partPath    = partPath;
            flat._parentIndex = flatParent;
            vector<PrefabPartOverride> listInnerPart;
            const bool                 bStateMade = PrefabOverrides::makeInstanceState( baseState, node._overrideXML, flat._name, flat._resolvedStateXML ) &&
                                    PrefabAssetInternal::applyPartOverrides( listPartOverride, partPath, flat._resolvedStateXML ) &&
                                    PrefabOverrides::collectPartOverrides( node._overrideXML, listInnerPart );
            if ( bStateMade == false || flat.isResolved() == false )
            {
                SW_LOG_ERROR_ID( "PrefabAsset.OverridesNestedPrefabInstance3", "Overrides of nested prefab instance '%#' in '%#' could not be applied - the prefab is not flattened", flat._name, asset.getName() );
                return false;
            }
            const int32 flatIndex = static_cast<int32>( inoutListNode.size() );
            listFlatIndex[index]  = flatIndex;
            inoutListNode.push_back( std::move( flat ) );

            // 노드가 부품 아래에 덮어쓴 것(`<Part>`)은 스폰과 같은 순서(안쪽 먼저, 바깥 것이 이김)로 아래 노드의 펼친 상태에 베이크한다.
            listInnerPart.insert( listInnerPart.begin(), pSource->getVariantPartOverrides().begin(), pSource->getVariantPartOverrides().end() );
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
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabCookNeedsSource", "Prefab cook needs a source root and an output directory (--cooked-dir)." );
            return 0;
        }

        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( sourceRoot, {}, listFile ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabAsset.PrefabCookCouldNot", "Prefab cook could not list '%#' - nothing is cooked", sourceRoot );
            ++outFailedCount;
            return 0;
        }
        // 순서를 정한다 — 같은 쿠킹본을 쓰는 두 소스 가운데 어느 것이 먼저인지가 실행마다 같아야 한다.
        VectorUtil::sort( listFile );

        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( sourceRoot ) );
        string       out  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( cookedDir ) );
        out += '/';

        unordered_set<string> uniqueSetWrittenKey; // 중복 판정만 한다 — 출력 순서는 정렬된 `listFile` 가 정한다
        CookStats             tally;
        const CookJobDesc     desc = makeCookJobDesc();
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
            if ( uniqueSetWrittenKey.contains( outputKey ) )
            {
                SW_LOG_WARNING_ID( "PrefabAsset.PrefabCookWouldOverwrite", "Prefab cook: '%#' would overwrite the cooked file another source already wrote ('%#') - skipped", normalized, outputPath );
                ++outFailedCount;
                continue;
            }

            const CookResult result = store.cookFile( desc, outputID, outputPath, [&]()
            {
                // 중첩 인스턴스의 원본을 읽는 캐시는 작업마다 새로 둔다 — 앞 작업이 읽어 둔 원본을 쓰면 그 읽기가 이 작업의 입력에서 빠진다.
                PrefabCache nestedPrefabs;
                PrefabAsset asset;
                // 디스크로 훑은 원본이다 — 디스크 입구로 읽는다(리소스 id 입구는 팩 전용 구성에서 리소스 루트 안 절대 경로를 거절한다).
                const bool                   bLoaded    = asset.loadFromDiskFile( normalized );
                const bool                   bFlattened = bLoaded && asset.isValid() && nestedPrefabs.flattenNestedInstances( asset );
                const optional<AbsolutePath> outputFile = FileUtil::requireAbsolutePath( outputPath );
                if ( outputFile.has_value() == false || bFlattened == false || asset.saveToBinaryFile( *outputFile ) == false )
                {
                    SW_LOG_WARNING_ID( "PrefabAsset.PrefabCookFailed", "Prefab cook failed for '%#'", normalized );
                    return CookStep::Failed;
                }
                return CookStep::Written;
            } );
            if ( result == CookResult::Failed )
            {
                ++outFailedCount;
                continue;
            }
            uniqueSetWrittenKey.insert( outputKey );
            tally.add( result );
        }
        SW_LOG_INFO_ID( "PrefabAsset.PrefabCookReusedDDC", "Prefab cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }

    CookJobDesc PrefabCache::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName      = "Prefab";
        desc._codeFingerprint = CookCodeFingerprint::find( "Prefab" );
        desc._target          = "Any";
        desc._settingsHash    = 0; // 프리팹 쿠킹에는 설정이 없다
        return desc;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( PrefabCache );
    SW_ASSET_TYPE( Prefab, PrefabAsset::kAssetType );

    string PrefabCache::findFieldOrigin( string_view assetRelativePath, string_view componentKey, string_view fieldName )
    {
        string path{ assetRelativePath };
        for ( uint32 depth = 0; depth <= DataInheritance::getMaxDepth(); ++depth ) // 읽힌 사슬은 상한 안이다 — 읽는 사이 파일이 바뀌어도 끝난다
        {
            const shared_ptr<const PrefabAsset> pAsset = loadPrefab( path );
            if ( pAsset == nullptr )
                return {};
            if ( pAsset->getBasePath().empty() || PrefabOverrides::writesField( pAsset->getVariantOverrides(), componentKey, fieldName ) )
                return path;
            path = pAsset->getBasePath();
        }
        return {};
    }

    void PrefabCache::reload( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        if ( assetRelativePath.empty() )
            return;

        const string cacheKey = makeCacheKey( assetRelativePath );

        UniqueLock<shared_mutex> writeLock{ _mapCacheMutex };
        // 읽는 중인 로드는 옛 파일을 읽었을 수 있다 — 세대를 올려 그 결과가 캐시에 들지 않게 하고, 다음 요청은 새로 읽게 진행 중 표에서 뗀다.
        _cacheGeneration.fetch_add( 1, std::memory_order_acq_rel );
        _mapLoading.clear();
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
            if ( bHasSourceNode || cacheIt->second->getBasePath().empty() == false )
                cacheIt = _mapCache.erase( cacheIt );
            else
                ++cacheIt;
        }
    }
} // namespace sw

namespace sw
{
    shared_ptr<const PrefabAsset> AssetRefCache<PrefabAsset>::acquire( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().loadPrefab( path );
    }

    PrefabHandle AssetRefCache<PrefabAsset>::request( string_view path )
    {
        return engine::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().request( path );
    }
} // namespace sw
