#include "pch.h"

#include "Engine/Object/Prefab/PrefabOverrides.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct PrefabOverridesInternal
        {
            using ComponentListWriteFn = Delegate<void( XmlNode, const XmlNode& )>;

            static constexpr const utf8* kComponentList = "_listComponent";
            static constexpr const utf8* kComponentName = "_componentName";
            static constexpr const utf8* kObjectName    = "_name";
            static constexpr const utf8* kSchemaVersion = "_schemaVersion";
            static constexpr const utf8* kObject        = "Object";
            static constexpr const utf8* kRemove        = "Remove";
            static constexpr const utf8* kOverride      = "Override";
            static constexpr const utf8* kAdd           = "Add";
            static constexpr const utf8* kKey           = "key";
            static constexpr const utf8* kAfter         = "after";
            static constexpr const utf8* kBaseName      = "PrefabBase";

            /** @brief 컴포넌트 원소 하나와 그 키(`이름표#n`)입니다. */
            struct KeyedComponent
            {
                XmlNode _node;
                string  _key;
                bool    _bUsed{ false };
            };

            /** @brief 원소 노드인지 봅니다(글 · 주석 노드는 이름이 비어 있다). */
            static bool isElement( const XmlNode& node )
            {
                return node.isValid() && StringUtil::isNullOrEmpty( node.getName() ) == false;
            }

            static bool isSameText( const utf8* pLeft, const utf8* pRight )
            {
                return StringUtil::equals( pLeft, pRight, false );
            }

            /** @brief 이름이 정확히 같은 첫 자식 원소입니다. 없으면 무효 노드입니다. */
            static XmlNode findChildElement( const XmlNode& parent, const utf8* pName )
            {
                if ( parent.isValid() == false )
                    return {};
                for ( XmlNode child = parent.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) && isSameText( child.getName(), pName ) )
                        return child;
                }
                return {};
            }

            /** @brief 첫 자식 원소입니다(`<Override>` · `<Add>` 가 싼 컴포넌트 원소). */
            static XmlNode findFirstElement( const XmlNode& parent )
            {
                for ( XmlNode child = parent.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) )
                        return child;
                }
                return {};
            }

            /**
             * @brief 컴포넌트 목록 원소의 컴포넌트마다 키를 붙여 모읍니다. 키는 `ComponentStableKey` 와 같은 규칙입니다 — 이름표(`_componentName`),
             *        없거나 비었으면 원소 이름(타입), 그 뒤에 같은 이름 가운데 몇 번째인지.
             */
            static void collectComponents( const XmlNode& listNode, vector<KeyedComponent>& outListComponent )
            {
                outListComponent.clear();
                if ( listNode.isValid() == false )
                    return;
                unordered_map<string, int32> mapCount;
                for ( XmlNode child = listNode.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    const utf8*  pLabel     = child.findAttribute( kComponentName, false );
                    const bool   bNoLabel   = StringUtil::isNullOrEmpty( pLabel ) || isSameText( pLabel, "None" );
                    const string baseName   = bNoLabel ? string{ child.getName() } : string{ pLabel };
                    int32&       occurrence = mapCount[baseName];
                    outListComponent.push_back( KeyedComponent{ child, baseName + "#" + to_string( occurrence ), false } );
                    ++occurrence;
                }
            }

            /** @brief 키 · 원소 이름(타입)이 같은 컴포넌트입니다. 없으면 nullptr 입니다. */
            static KeyedComponent* findComponent( vector<KeyedComponent>& listComponent, string_view key, const utf8* pTypeName )
            {
                for ( KeyedComponent& component : listComponent )
                {
                    if ( component._key == key && isSameText( component._node.getName(), pTypeName ) )
                        return &component;
                }
                return nullptr;
            }

            /** @brief 오브젝트 루트에서 덮어쓴 것으로 적지 않는 칸입니다 — 이름은 엔티티가, 판은 직렬화기가, 컴포넌트는 따로 견준다. */
            static bool isRootOnlyField( const utf8* pName )
            {
                return isSameText( pName, kSchemaVersion ) || isSameText( pName, kObjectName ) || isSameText( pName, kComponentList );
            }

            /** @brief 인스턴스 원소에서 기준 원소와 다른 속성 · 자식 원소를 모읍니다. */
            static void collectDifferences( const XmlNode& instance, const XmlNode& base, bool bRoot, vector<XmlAttribute>& outListAttribute,
                                            vector<XmlNode>& outListChild )
            {
                outListAttribute.clear();
                outListChild.clear();
                for ( XmlAttribute attribute = instance.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( bRoot && isRootOnlyField( attribute.getName() ) )
                        continue;
                    const utf8* pBaseValue = base.findAttribute( attribute.getName(), false );
                    if ( pBaseValue == nullptr || isSameText( pBaseValue, attribute.getValue() ) == false )
                        outListAttribute.push_back( attribute );
                }
                for ( XmlNode child = instance.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false || ( bRoot && isRootOnlyField( child.getName() ) ) )
                        continue;
                    const XmlNode baseChild = findChildElement( base, child.getName() );
                    if ( baseChild.isValid() == false || baseChild.toString() != child.toString() )
                        outListChild.push_back( child );
                }
            }

            /** @brief 모은 속성 · 자식만 든 원소 하나를 @p parent 아래에 씁니다. */
            static void appendPartial( XmlNode parent, const utf8* pName, const vector<XmlAttribute>& listAttribute, const vector<XmlNode>& listChild )
            {
                XmlNode partial = parent.appendChild( pName );
                for ( const XmlAttribute& attribute : listAttribute )
                {
                    partial.appendAttribute( attribute.getName(), attribute.getValue() );
                }
                for ( const XmlNode& child : listChild )
                {
                    partial.appendClone( child );
                }
            }

            /** @brief 기준 원소의 속성을 덮어쓴 값(있으면)으로 쓰고, 기준에 없는 덮어쓴 속성을 더합니다. @p pName 이 있으면 `_name` 을 그것으로. */
            static void appendMergedAttributes( XmlNode out, const XmlNode& base, const XmlNode& partial, const utf8* pName )
            {
                for ( XmlAttribute attribute = base.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const bool  bRenamed = pName != nullptr && isSameText( attribute.getName(), kObjectName );
                    const utf8* pValue   = partial.isValid() ? partial.findAttribute( attribute.getName(), false ) : nullptr;
                    if ( bRenamed )
                        pValue = pName;
                    out.appendAttribute( attribute.getName(), pValue != nullptr ? pValue : attribute.getValue() );
                }
                if ( partial.isValid() == false )
                    return;
                for ( XmlAttribute attribute = partial.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( base.findAttribute( attribute.getName(), false ) == nullptr && isSameText( attribute.getName(), kObjectName ) == false )
                        out.appendAttribute( attribute.getName(), attribute.getValue() );
                }
            }

            /** @brief 기준 원소의 자식 원소마다 덮어쓴 것이 있으면 그것을, 없으면 기준의 것을 씁니다. 컴포넌트 목록은 @p listWriter 가 씁니다. */
            static void appendMergedChildren( XmlNode out, const XmlNode& base, const XmlNode& partial, const ComponentListWriteFn* pListWriter )
            {
                bool bListWritten = false;
                for ( XmlNode child = base.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    if ( pListWriter != nullptr && isSameText( child.getName(), kComponentList ) )
                    {
                        ( *pListWriter )( out, child );
                        bListWritten = true;
                        continue;
                    }
                    const XmlNode replaced = findChildElement( partial, child.getName() );
                    out.appendClone( replaced.isValid() ? replaced : child );
                }
                if ( partial.isValid() )
                {
                    for ( XmlNode child = partial.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        if ( isElement( child ) && findChildElement( base, child.getName() ).isValid() == false )
                            out.appendClone( child );
                    }
                }
                if ( pListWriter != nullptr && bListWritten == false )
                    ( *pListWriter )( out, XmlNode{} );
            }

            /** @brief `makeInstanceState` 가 컴포넌트 목록을 쓰는 단계입니다 — 지운 것을 빼고, 덮어쓴 것을 얹고, 더한 것을 `after` 자리에 넣습니다. */
            struct ComponentListWriter
            {
                XmlNode     _overrideRoot;
                string_view _instanceName; ///< 경고에 적을 엔티티 이름(비면 "?")

                void write( XmlNode outRoot, const XmlNode& baseList ) const
                {
                    vector<KeyedComponent> listBase;
                    collectComponents( baseList, listBase );
                    XmlNode outList = outRoot.appendChild( kComponentList );

                    vector<string> listRemovedKey;
                    for ( XmlNode entry = _overrideRoot.findChild( kRemove, false ); entry.isValid(); entry = entry.findNextSibling( kRemove, false ) )
                    {
                        const utf8* pKey = entry.findAttribute( kKey, false );
                        if ( StringUtil::isNullOrEmpty( pKey ) == false )
                            listRemovedKey.push_back( pKey );
                    }

                    // 더한 컴포넌트는 앞에 있던 물려받은 컴포넌트(지운 것 포함) 뒤에 그 자리대로 넣는다 — 순서가 바뀌면 같은 타입의 `타입#n` 키가 바뀐다.
                    appendAddedAfter( outList, nullptr );
                    for ( KeyedComponent& component : listBase )
                    {
                        const bool bRemoved = std::find( listRemovedKey.begin(), listRemovedKey.end(), component._key ) != listRemovedKey.end();
                        if ( bRemoved )
                            component._bUsed = true;
                        else
                            appendBaseComponent( outList, component );
                        appendAddedAfter( outList, &component._key );
                    }
                    warnDroppedEntries( listBase, listRemovedKey );
                    appendAddedWithLostAnchor( outList, listBase );
                }

                /** @brief 프리팹 컴포넌트 하나를 씁니다 — 덮어쓴 것이 있으면 얹어서. */
                void appendBaseComponent( XmlNode outList, KeyedComponent& component ) const
                {
                    const XmlNode partial = findOverride( component );
                    if ( partial.isValid() == false )
                    {
                        outList.appendClone( component._node );
                        return;
                    }
                    component._bUsed = true;
                    XmlNode merged   = outList.appendChild( component._node.getName() );
                    appendMergedAttributes( merged, component._node, partial, nullptr );
                    appendMergedChildren( merged, component._node, partial, nullptr );
                }

                /** @brief `after` 가 @p pAnchorKey 인 더한 컴포넌트를 문서 순서대로 씁니다. @p pAnchorKey 가 nullptr 이면 `after` 가 없는 것(맨 앞)입니다. */
                void appendAddedAfter( XmlNode outList, const string* pAnchorKey ) const
                {
                    for ( XmlNode entry = _overrideRoot.findChild( kAdd, false ); entry.isValid(); entry = entry.findNextSibling( kAdd, false ) )
                    {
                        const utf8* pAfter    = entry.findAttribute( kAfter, false );
                        const bool  bNoAnchor = StringUtil::isNullOrEmpty( pAfter );
                        const bool  bMatches  = pAnchorKey == nullptr ? bNoAnchor : ( bNoAnchor == false && *pAnchorKey == pAfter );
                        if ( bMatches == false )
                            continue;
                        const XmlNode added = findFirstElement( entry );
                        if ( added.isValid() )
                            outList.appendClone( added );
                    }
                }

                /** @brief 앞의 물려받은 컴포넌트가 프리팹에서 사라진 더한 컴포넌트를 끝에 씁니다 — 버리지 않는다(다음 저장이 지금 자리로 다시 적는다). */
                void appendAddedWithLostAnchor( XmlNode outList, const vector<KeyedComponent>& listBase ) const
                {
                    for ( XmlNode entry = _overrideRoot.findChild( kAdd, false ); entry.isValid(); entry = entry.findNextSibling( kAdd, false ) )
                    {
                        const utf8* pAfter = entry.findAttribute( kAfter, false );
                        if ( StringUtil::isNullOrEmpty( pAfter ) )
                            continue;
                        bool bFound = false;
                        for ( const KeyedComponent& component : listBase )
                        {
                            bFound = bFound || component._key == pAfter;
                        }
                        const XmlNode added = findFirstElement( entry );
                        if ( bFound || added.isValid() == false )
                            continue;
                        SW_LOG_TRACE( "Added component follows '%#', which the prefab no longer has - appended at the end", pAfter );
                        outList.appendClone( added );
                    }
                }

                /** @brief 이 컴포넌트를 가리키는 `<Override>` 의 컴포넌트 원소입니다. 원소 이름(타입)이 다르면 무효 노드입니다. */
                XmlNode findOverride( const KeyedComponent& component ) const
                {
                    for ( XmlNode entry = _overrideRoot.findChild( kOverride, false ); entry.isValid(); entry = entry.findNextSibling( kOverride, false ) )
                    {
                        const utf8*   pKey    = entry.findAttribute( kKey, false );
                        const XmlNode partial = findFirstElement( entry );
                        if ( pKey != nullptr && component._key == pKey && partial.isValid() && isSameText( partial.getName(), component._node.getName() ) )
                            return partial;
                    }
                    return {};
                }

                /** @brief 프리팹에서 사라진(또는 타입이 바뀐) 컴포넌트를 가리키는 항목을 알립니다 — 버려지고 다음 저장에서 빠진다. */
                void warnDroppedEntries( const vector<KeyedComponent>& listBase, const vector<string>& listRemovedKey ) const
                {
                    for ( XmlNode entry = _overrideRoot.findChild( kOverride, false ); entry.isValid(); entry = entry.findNextSibling( kOverride, false ) )
                    {
                        const utf8* pKey     = entry.findAttribute( kKey, false );
                        bool        bMatched = false;
                        for ( const KeyedComponent& component : listBase )
                        {
                            bMatched = bMatched || ( pKey != nullptr && component._key == pKey && component._bUsed );
                        }
                        if ( bMatched == false )
                            SW_LOG_WARNING( "Prefab override for component '%#' of '%#' is dropped - the prefab no longer has that component", pKey != nullptr ? pKey : "",
                                            _instanceName.empty() ? string_view( "?" ) : _instanceName );
                    }
                    for ( const string& removedKey : listRemovedKey )
                    {
                        bool bMatched = false;
                        for ( const KeyedComponent& component : listBase )
                        {
                            bMatched = bMatched || component._key == removedKey;
                        }
                        if ( bMatched == false )
                            SW_LOG_TRACE( "Removed prefab component '%#' is already gone from the prefab", removedKey );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "PrefabOverrides" );

    bool PrefabOverrides::makeBaseState( const PrefabAsset& prefab, string& outStateXml )
    {
        outStateXml.clear();
        if ( prefab.isValid() == false )
            return false;

        // 원형은 씬 밖에서 짓는다(언리얼 CDO 처럼 월드에 들지 않는다). 매니저마다 엔진 · 모듈의 팩토리가 등록되므로 게임 컴포넌트도 지어진다.
        GameObjectManager scratch;
        const string&     prefabName = prefab.getName();
        GameObject*       pBase      = scratch.createGameObject( hashed_string( prefabName.empty() ? PrefabOverridesInternal::kBaseName : prefabName.c_str() ) );
        if ( pBase == nullptr )
            return false;
        if ( StringUtil::trim( prefab.getStateData() ).empty() == false && prefab.applyStateTo( pBase ) == false )
            return false;

        ObjectSaveOptions options{};
        options._bOmitExternalParent = true;
        outStateXml                  = ObjectStateSerializer::saveToXmlString( pBase, options );
        return outStateXml.empty() == false;
    }

    bool PrefabOverrides::computeOverrides( string_view instanceStateXml, string_view baseStateXml, string& outOverrideXml )
    {
        outOverrideXml.clear();
        XmlDocument instanceDoc;
        XmlDocument baseDoc;
        if ( instanceDoc.parse( instanceStateXml ) == false || baseDoc.parse( baseStateXml ) == false )
            return false;
        const XmlNode instanceRoot = instanceDoc.getRoot();
        const XmlNode baseRoot     = baseDoc.getRoot();
        if ( instanceRoot.isValid() == false || baseRoot.isValid() == false )
            return false;

        XmlDocument outDoc;
        XmlNode     outRoot = outDoc.appendRoot( kRootName );
        bool        bAny    = false;

        vector<XmlAttribute> listAttribute;
        vector<XmlNode>      listChild;
        PrefabOverridesInternal::collectDifferences( instanceRoot, baseRoot, true, listAttribute, listChild );
        if ( listAttribute.empty() == false || listChild.empty() == false )
        {
            PrefabOverridesInternal::appendPartial( outRoot, PrefabOverridesInternal::kObject, listAttribute, listChild );
            bAny = true;
        }

        vector<PrefabOverridesInternal::KeyedComponent> listInstance;
        vector<PrefabOverridesInternal::KeyedComponent> listBase;
        PrefabOverridesInternal::collectComponents( PrefabOverridesInternal::findChildElement( instanceRoot, PrefabOverridesInternal::kComponentList ), listInstance );
        PrefabOverridesInternal::collectComponents( PrefabOverridesInternal::findChildElement( baseRoot, PrefabOverridesInternal::kComponentList ), listBase );

        // 짝: 키와 타입(원소 이름)이 같은 것. 짝이 없는 프리팹 컴포넌트는 지운 것, 짝이 없는 인스턴스 컴포넌트는 더한 것이다.
        for ( PrefabOverridesInternal::KeyedComponent& instanceComponent : listInstance )
        {
            PrefabOverridesInternal::KeyedComponent* pBaseComponent =
                PrefabOverridesInternal::findComponent( listBase, instanceComponent._key, instanceComponent._node.getName() );
            if ( pBaseComponent != nullptr )
            {
                pBaseComponent->_bUsed   = true;
                instanceComponent._bUsed = true;
            }
        }
        for ( const PrefabOverridesInternal::KeyedComponent& baseComponent : listBase )
        {
            if ( baseComponent._bUsed )
                continue;
            outRoot.appendChild( PrefabOverridesInternal::kRemove ).appendAttribute( PrefabOverridesInternal::kKey, baseComponent._key );
            bAny = true;
        }
        for ( const PrefabOverridesInternal::KeyedComponent& instanceComponent : listInstance )
        {
            if ( instanceComponent._bUsed == false )
                continue;
            PrefabOverridesInternal::KeyedComponent* pBaseComponent =
                PrefabOverridesInternal::findComponent( listBase, instanceComponent._key, instanceComponent._node.getName() );
            PrefabOverridesInternal::collectDifferences( instanceComponent._node, pBaseComponent->_node, false, listAttribute, listChild );
            if ( listAttribute.empty() && listChild.empty() )
                continue;
            XmlNode entry = outRoot.appendChild( PrefabOverridesInternal::kOverride );
            entry.appendAttribute( PrefabOverridesInternal::kKey, instanceComponent._key );
            PrefabOverridesInternal::appendPartial( entry, instanceComponent._node.getName(), listAttribute, listChild );
            bAny = true;
        }
        // 더한 컴포넌트는 바로 앞의 물려받은 컴포넌트 키를 `after` 로 적는다(`makeInstanceState` 가 그 뒤에 넣는다). 앞에 물려받은 것이 없으면 적지 않는다(맨 앞).
        const string* pLastInheritedKey = nullptr;
        for ( const PrefabOverridesInternal::KeyedComponent& instanceComponent : listInstance )
        {
            if ( instanceComponent._bUsed )
            {
                pLastInheritedKey = &instanceComponent._key;
                continue;
            }
            XmlNode entry = outRoot.appendChild( PrefabOverridesInternal::kAdd );
            if ( pLastInheritedKey != nullptr )
                entry.appendAttribute( PrefabOverridesInternal::kAfter, *pLastInheritedKey );
            entry.appendClone( instanceComponent._node );
            bAny = true;
        }

        if ( bAny )
            outOverrideXml = outRoot.toString();
        return true;
    }

    bool PrefabOverrides::makeInstanceState( string_view baseStateXml, string_view overrideXml, string_view instanceName, string& outStateXml )
    {
        outStateXml.clear();
        XmlDocument baseDoc;
        if ( baseDoc.parse( baseStateXml ) == false )
            return false;
        const XmlNode baseRoot = baseDoc.getRoot();
        if ( baseRoot.isValid() == false )
            return false;

        XmlDocument overrideDoc;
        XmlNode     overrideRoot;
        if ( StringUtil::trim( overrideXml ).empty() == false )
        {
            if ( overrideDoc.parse( overrideXml ) == false )
                return false;
            overrideRoot = overrideDoc.getRoot( kRootName, false );
            if ( overrideRoot.isValid() == false )
            {
                SW_LOG_WARNING( "Prefab overrides have no <%#> root - the instance is built from the prefab alone", kRootName );
                return false;
            }
        }

        const XmlNode                                       objectOverride = PrefabOverridesInternal::findChildElement( overrideRoot, PrefabOverridesInternal::kObject );
        const PrefabOverridesInternal::ComponentListWriter  listWriter{ overrideRoot, instanceName };
        const PrefabOverridesInternal::ComponentListWriteFn writeList =
            SW_DELEGATE_METHOD( PrefabOverridesInternal::ComponentListWriteFn, &PrefabOverridesInternal::ComponentListWriter::write, &listWriter );
        const string name( instanceName );

        XmlDocument outDoc;
        XmlNode     outRoot = outDoc.appendRoot( baseRoot.getName() );
        PrefabOverridesInternal::appendMergedAttributes( outRoot, baseRoot, objectOverride, name.empty() ? nullptr : name.c_str() );
        PrefabOverridesInternal::appendMergedChildren( outRoot, baseRoot, objectOverride, &writeList );
        outStateXml = outRoot.toString();
        return outStateXml.empty() == false;
    }
} // namespace sw
