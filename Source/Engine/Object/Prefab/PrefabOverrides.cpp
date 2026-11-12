#include "pch.h"

#include "Engine/Object/Prefab/PrefabOverrides.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Serializer/SchemaMigrate.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct PrefabOverridesInternal
        {
            using ComponentListWriteFn = Delegate<void( XMLNode, const XMLNode& )>;

            static constexpr const utf8* kComponentList = "_listComponent";
            static constexpr const utf8* kComponentName = "_componentName";
            static constexpr const utf8* kObjectName    = "_name";
            static constexpr const utf8* kObject        = "Object";
            static constexpr const utf8* kRemove        = "Remove";
            static constexpr const utf8* kOverride      = "Override";
            static constexpr const utf8* kAdd           = "Add";
            static constexpr const utf8* kKey           = "key";
            static constexpr const utf8* kAfter         = "after";
            static constexpr const utf8* kPart          = "Part";
            static constexpr const utf8* kPath          = "path";
            static constexpr const utf8* kBaseName      = "PrefabBase";

            /** @brief 컴포넌트 원소 하나와 그 키(`이름#n`)입니다. */
            struct KeyedComponent
            {
                XMLNode _node;
                string  _key;
                bool    _bUsed{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /** @brief 원소 노드인지 봅니다(글 · 주석 노드는 이름이 비어 있다). */
            static bool isElement( const XMLNode& node )
            {
                return node.isValid() && StringUtil::isNullOrEmpty( node.getName() ) == false;
            }

            /** @brief 이름이 정확히 같은 첫 자식 원소입니다. 없으면 무효 노드입니다. */
            static XMLNode findChildElement( const XMLNode& parent, const utf8* pName )
            {
                if ( parent.isValid() == false )
                    return {};
                for ( XMLNode child = parent.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) && StringUtil::equals( child.getName(), pName, false ) )
                        return child;
                }
                return {};
            }

            /** @brief 첫 자식 원소입니다(`<Override>` · `<Add>` 가 싼 컴포넌트 원소). */
            static XMLNode findFirstElement( const XMLNode& parent )
            {
                for ( XMLNode child = parent.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) )
                        return child;
                }
                return {};
            }

            /**
             * @brief 컴포넌트 목록 원소의 컴포넌트마다 키를 붙여 모읍니다. 키는 `ComponentStableKey` 와 같은 규칙입니다 — 이름(`_componentName`),
             *        없거나 비었으면 원소 이름(타입), 그 뒤에 같은 이름 가운데 몇 번째인지.
             */
            static void collectComponents( const XMLNode& listNode, vector<KeyedComponent>& outListComponent )
            {
                outListComponent.clear();
                if ( listNode.isValid() == false )
                    return;
                unordered_map<string, int32> mapCount;
                for ( XMLNode child = listNode.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    const utf8*                              pLabel     = child.findAttribute( kComponentName );
                    const bool                               bNoLabel   = StringUtil::isNullOrEmpty( pLabel ) || StringUtil::equals( pLabel, "None" );
                    const string                             baseName   = bNoLabel ? string{ child.getName() } : string{ pLabel };
                    int32&                                   occurrence = mapCount[baseName];
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "%##%#", baseName, occurrence );
                    outListComponent.push_back( KeyedComponent{ child, string( textBuilder.view() ), false } );
                    ++occurrence;
                }
            }

            /** @brief 키 · 원소 이름(타입)이 같은 컴포넌트입니다. 없으면 nullptr 입니다. */
            static KeyedComponent* findComponent( vector<KeyedComponent>& listComponent, string_view key, const utf8* pTypeName )
            {
                return VectorUtil::findIf( listComponent, [&]( KeyedComponent& component )
                { return component._key == key && StringUtil::equals( component._node.getName(), pTypeName, false ); } );
            }

            /** @brief 오브젝트 루트에서 덮어쓴 것으로 적지 않는 칸입니다 — 이름은 엔티티가, 판은 직렬화기가, 컴포넌트는 따로 비교한다. */
            static bool isRootOnlyField( const utf8* pName )
            {
                return StringUtil::equals( pName, sw::kSchemaVersionKey, false ) || StringUtil::equals( pName, kObjectName, false ) || StringUtil::equals( pName, kComponentList, false );
            }

            /** @brief 인스턴스 원소에서 기준 원소와 다른 속성 · 자식 원소를 모읍니다. */
            static void collectDifferences( const XMLNode& instance, const XMLNode& base, bool bRoot, vector<XMLAttribute>& outListAttribute,
                                            vector<XMLNode>& outListChild )
            {
                outListAttribute.clear();
                outListChild.clear();
                for ( XMLAttribute attribute = instance.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( bRoot && isRootOnlyField( attribute.getName() ) )
                        continue;
                    const utf8* pBaseValue = base.findAttribute( attribute.getName() );
                    if ( pBaseValue == nullptr || StringUtil::equals( pBaseValue, attribute.getValue() ) == false )
                        outListAttribute.push_back( attribute );
                }
                for ( XMLNode child = instance.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false || ( bRoot && isRootOnlyField( child.getName() ) ) )
                        continue;
                    const XMLNode baseChild = findChildElement( base, child.getName() );
                    if ( baseChild.isValid() == false || baseChild.toString() != child.toString() )
                        outListChild.push_back( child );
                }
            }

            /** @brief 모은 속성 · 자식만 든 원소 하나를 @p parent 아래에 씁니다. */
            static void appendPartial( XMLNode parent, const utf8* pName, const vector<XMLAttribute>& listAttribute, const vector<XMLNode>& listChild )
            {
                XMLNode partial = parent.appendChild( pName );
                for ( const XMLAttribute& attribute : listAttribute )
                {
                    partial.appendAttribute( attribute.getName(), attribute.getValue() );
                }
                for ( const XMLNode& child : listChild )
                {
                    partial.appendClone( child );
                }
            }

            /** @brief 기준 원소의 속성을 덮어쓴 값(있으면)으로 쓰고, 기준에 없는 덮어쓴 속성을 더합니다. @p pName 이 있으면 `_name` 을 그것으로. */
            static void appendMergedAttributes( XMLNode out, const XMLNode& base, const XMLNode& partial, const utf8* pName )
            {
                for ( XMLAttribute attribute = base.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const bool  bRenamed = pName != nullptr && StringUtil::equals( attribute.getName(), kObjectName );
                    const utf8* pValue   = partial.isValid() ? partial.findAttribute( attribute.getName() ) : nullptr;
                    if ( bRenamed )
                        pValue = pName;
                    out.appendAttribute( attribute.getName(), pValue != nullptr ? pValue : attribute.getValue() );
                }
                if ( partial.isValid() == false )
                    return;
                for ( XMLAttribute attribute = partial.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( base.findAttribute( attribute.getName() ) == nullptr && StringUtil::equals( attribute.getName(), kObjectName ) == false )
                        out.appendAttribute( attribute.getName(), attribute.getValue() );
                }
            }

            /** @brief 기준 원소의 자식 원소마다 덮어쓴 것이 있으면 그것을, 없으면 기준의 것을 씁니다. 컴포넌트 목록은 @p listWriter 가 씁니다. */
            static void appendMergedChildren( XMLNode out, const XMLNode& base, const XMLNode& partial, const ComponentListWriteFn* pListWriter )
            {
                bool bListWritten = false;
                for ( XMLNode child = base.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    if ( pListWriter != nullptr && StringUtil::equals( child.getName(), kComponentList, false ) )
                    {
                        ( *pListWriter )( out, child );
                        bListWritten = true;
                        continue;
                    }
                    const XMLNode replaced = findChildElement( partial, child.getName() );
                    out.appendClone( replaced.isValid() ? replaced : child );
                }
                if ( partial.isValid() )
                {
                    for ( XMLNode child = partial.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        if ( isElement( child ) && findChildElement( base, child.getName() ).isValid() == false )
                            out.appendClone( child );
                    }
                }
                if ( pListWriter != nullptr && bListWritten == false )
                    ( *pListWriter )( out, XMLNode{} );
            }

            /** @brief `makeInstanceState` 가 컴포넌트 목록을 쓰는 단계입니다 — 지운 것을 빼고, 덮어쓴 것을 얹고, 더한 것을 `after` 자리에 넣습니다. */
            struct ComponentListWriter
            {
                XMLNode     _overrideRoot;
                string_view _instanceName; ///< 경고에 적을 엔티티 이름(비면 "?")

                void write( XMLNode outRoot, const XMLNode& baseList ) const
                {
                    vector<KeyedComponent> listBase;
                    collectComponents( baseList, listBase );
                    XMLNode outList = outRoot.appendChild( kComponentList );

                    vector<string> listRemovedKey;
                    for ( XMLNode entry = _overrideRoot.findChild( kRemove ); entry.isValid(); entry = entry.findNextSibling( kRemove ) )
                    {
                        const utf8* pKey = entry.findAttribute( kKey );
                        if ( StringUtil::isNullOrEmpty( pKey ) == false )
                            listRemovedKey.push_back( pKey );
                    }

                    // 더한 컴포넌트는 앞에 있던 물려받은 컴포넌트(지운 것 포함) 뒤에 그 자리대로 넣는다 — 순서가 바뀌면 같은 타입의 `타입#n` 키가 바뀐다.
                    appendAddedAfter( outList, nullptr );
                    for ( KeyedComponent& component : listBase )
                    {
                        const bool bRemoved = VectorUtil::contains( listRemovedKey, component._key );
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
                void appendBaseComponent( XMLNode outList, KeyedComponent& component ) const
                {
                    const XMLNode partial = findOverride( component );
                    if ( partial.isValid() == false )
                    {
                        outList.appendClone( component._node );
                        return;
                    }
                    component._bUsed = true;
                    XMLNode merged   = outList.appendChild( component._node.getName() );
                    appendMergedAttributes( merged, component._node, partial, nullptr );
                    appendMergedChildren( merged, component._node, partial, nullptr );
                }

                /** @brief `after` 가 @p pAnchorKey 인 더한 컴포넌트를 문서 순서대로 씁니다. @p pAnchorKey 가 nullptr 이면 `after` 가 없는 것(맨 앞)입니다. */
                void appendAddedAfter( XMLNode outList, const string* pAnchorKey ) const
                {
                    for ( XMLNode entry = _overrideRoot.findChild( kAdd ); entry.isValid(); entry = entry.findNextSibling( kAdd ) )
                    {
                        const utf8* pAfter    = entry.findAttribute( kAfter );
                        const bool  bNoAnchor = StringUtil::isNullOrEmpty( pAfter );
                        const bool  bMatches  = pAnchorKey == nullptr ? bNoAnchor : ( bNoAnchor == false && *pAnchorKey == pAfter );
                        if ( bMatches == false )
                            continue;
                        const XMLNode added = findFirstElement( entry );
                        if ( added.isValid() )
                            outList.appendClone( added );
                    }
                }

                /** @brief 앞의 물려받은 컴포넌트가 프리팹에서 사라진 더한 컴포넌트를 끝에 씁니다 — 버리지 않는다(다음 저장이 지금 자리로 다시 적는다). */
                void appendAddedWithLostAnchor( XMLNode outList, const vector<KeyedComponent>& listBase ) const
                {
                    for ( XMLNode entry = _overrideRoot.findChild( kAdd ); entry.isValid(); entry = entry.findNextSibling( kAdd ) )
                    {
                        const utf8* pAfter = entry.findAttribute( kAfter );
                        if ( StringUtil::isNullOrEmpty( pAfter ) )
                            continue;
                        bool bFound = false;
                        for ( const KeyedComponent& component : listBase )
                        {
                            bFound = bFound || component._key == pAfter;
                        }
                        const XMLNode added = findFirstElement( entry );
                        if ( bFound || added.isValid() == false )
                            continue;
                        SW_LOG_TRACE_ID( "PrefabOverrides.AddedComponentFollowsWhich", "Added component follows '%#', which the prefab no longer has - appended at the end", pAfter );
                        outList.appendClone( added );
                    }
                }

                /** @brief 이 컴포넌트를 가리키는 `<Override>` 의 컴포넌트 원소입니다. 원소 이름(타입)이 다르면 무효 노드입니다. */
                XMLNode findOverride( const KeyedComponent& component ) const
                {
                    for ( XMLNode entry = _overrideRoot.findChild( kOverride ); entry.isValid(); entry = entry.findNextSibling( kOverride ) )
                    {
                        const utf8*   pKey                    = entry.findAttribute( kKey );
                        const XMLNode partial                 = findFirstElement( entry );
                        const bool    bMatchingComponentEntry = pKey != nullptr && component._key == pKey && partial.isValid() && StringUtil::equals( partial.getName(), component._node.getName(), false );
                        if ( bMatchingComponentEntry )
                            return partial;
                    }
                    return {};
                }

                /** @brief 프리팹에서 사라진(또는 타입이 바뀐) 컴포넌트를 가리키는 항목을 알립니다 — 버려지고 다음 저장에서 빠진다. */
                void warnDroppedEntries( const vector<KeyedComponent>& listBase, const vector<string>& listRemovedKey ) const
                {
                    for ( XMLNode entry = _overrideRoot.findChild( kOverride ); entry.isValid(); entry = entry.findNextSibling( kOverride ) )
                    {
                        const utf8* pKey     = entry.findAttribute( kKey );
                        bool        bMatched = false;
                        for ( const KeyedComponent& component : listBase )
                        {
                            bMatched = bMatched || ( pKey != nullptr && component._key == pKey && component._bUsed );
                        }
                        if ( bMatched == false )
                            SW_LOG_WARNING_ID( "Prefab.OverrideComponentDropped", "Prefab override for component '%#' of '%#' is dropped - the prefab no longer has that component", pKey != nullptr ? pKey : "",
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
                            SW_LOG_TRACE_ID( "PrefabOverrides.RemovedPrefabComponentAlready", "Removed prefab component '%#' is already gone from the prefab", removedKey );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PrefabPartOverride::joinPath( string_view parentPath, string_view key )
    {
        string path{ parentPath };
        if ( path.empty() == false )
            path += "/";
        path += key;
        return path;
    }

    bool PrefabOverrides::makeBaseState( const PrefabAsset& prefab, string& outStateXML )
    {
        outStateXML.clear();
        if ( prefab.isValid() == false )
            return false;

        // 원형은 씬 밖에서 짓는다(언리얼 CDO 처럼 월드에 들지 않는다). 매니저마다 엔진 · 모듈의 팩토리가 등록되므로 게임 컴포넌트도 지어진다.
        // 비동기 씬 로드(워커)도 부른다 — 씬 밖 매니저(TransientTag)라 컴포넌트 훅을 부르지 않는다(감사 A-15).
        GameObjectManager scratch( GameObjectManager::TransientTag{} );
        const string&     prefabName = prefab.getName();
        GameObject*       pBase      = scratch.createGameObject( hashed_string( prefabName.empty() ? PrefabOverridesInternal::kBaseName : prefabName.c_str() ) );
        if ( pBase == nullptr )
            return false;
        if ( StringUtil::trim( prefab.getStateData() ).empty() == false && prefab.applyStateTo( pBase ) == false )
            return false;

        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = SW_TRUE;
        outStateXML                  = ObjectStateSerializer::saveToXMLString( pBase, options );
        return outStateXML.empty() == false;
    }

    expected<void> PrefabOverrides::computeOverrides( string_view instanceStateXML, string_view baseStateXML, string& outOverrideXML )
    {
        outOverrideXML.clear();
        XMLDocument instanceDoc;
        XMLDocument baseDoc;
        if ( instanceDoc.parse( instanceStateXML ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the instance state is not XML", instanceDoc.getLastError() } );
        if ( baseDoc.parse( baseStateXML ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the prefab base state is not XML", baseDoc.getLastError() } );
        const XMLNode instanceRoot = instanceDoc.getRoot();
        const XMLNode baseRoot     = baseDoc.getRoot();
        if ( instanceRoot.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the instance state has no root element" } );
        if ( baseRoot.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the prefab base state has no root element" } );

        XMLDocument outDoc;
        XMLNode     outRoot = outDoc.appendRoot( kRootName );
        bool        bAny    = false;

        vector<XMLAttribute> listAttribute;
        vector<XMLNode>      listChild;
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
            XMLNode entry = outRoot.appendChild( PrefabOverridesInternal::kOverride );
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
            XMLNode entry = outRoot.appendChild( PrefabOverridesInternal::kAdd );
            if ( pLastInheritedKey != nullptr )
                entry.appendAttribute( PrefabOverridesInternal::kAfter, *pLastInheritedKey );
            entry.appendClone( instanceComponent._node );
            bAny = true;
        }

        if ( bAny )
            outOverrideXML = outRoot.toString();
        return {};
    }

    bool PrefabOverrides::makeInstanceState( string_view baseStateXML, string_view overrideXML, string_view instanceName, string& outStateXML )
    {
        outStateXML.clear();
        XMLDocument baseDoc;
        if ( baseDoc.parse( baseStateXML ) == false )
            return false;
        const XMLNode baseRoot = baseDoc.getRoot();
        if ( baseRoot.isValid() == false )
            return false;

        XMLDocument overrideDoc;
        XMLNode     overrideRoot;
        if ( StringUtil::trim( overrideXML ).empty() == false )
        {
            if ( overrideDoc.parse( overrideXML ) == false )
                return false;
            overrideRoot = overrideDoc.getRoot( kRootName );
            if ( overrideRoot.isValid() == false )
            {
                SW_LOG_WARNING_ID( "PrefabOverrides.PrefabOverridesNoRoot", "Prefab overrides have no <%#> root - the instance is built from the prefab alone", kRootName );
                return false;
            }
        }

        const XMLNode                                       objectOverride = PrefabOverridesInternal::findChildElement( overrideRoot, PrefabOverridesInternal::kObject );
        const PrefabOverridesInternal::ComponentListWriter  listWriter{ overrideRoot, instanceName };
        const PrefabOverridesInternal::ComponentListWriteFn writeList =
            SW_DELEGATE_METHOD( PrefabOverridesInternal::ComponentListWriteFn, &PrefabOverridesInternal::ComponentListWriter::write, &listWriter );
        const string name( instanceName );

        XMLDocument outDoc;
        XMLNode     outRoot = outDoc.appendRoot( baseRoot.getName() );
        PrefabOverridesInternal::appendMergedAttributes( outRoot, baseRoot, objectOverride, name.empty() ? nullptr : name.c_str() );
        PrefabOverridesInternal::appendMergedChildren( outRoot, baseRoot, objectOverride, &writeList );
        outStateXML = outRoot.toString();
        return outStateXML.empty() == false;
    }

    bool PrefabOverrides::checkOverrideTargets( string_view baseStateXML, string_view overrideXML, string_view sourceName )
    {
        if ( StringUtil::trim( overrideXML ).empty() )
            return true;
        XMLDocument baseDoc;
        XMLDocument overrideDoc;
        if ( baseDoc.parse( baseStateXML ) == false || overrideDoc.parse( overrideXML ) == false || overrideDoc.getRoot( kRootName ).isValid() == false )
        {
            SW_LOG_ERROR_ID( "PrefabOverrides.VariantNotParsed", "'%#': its <%#> or its base state could not be parsed", sourceName, kRootName );
            return false;
        }
        vector<PrefabOverridesInternal::KeyedComponent> listBase;
        PrefabOverridesInternal::collectComponents( PrefabOverridesInternal::findChildElement( baseDoc.getRoot(), PrefabOverridesInternal::kComponentList ), listBase );
        const auto hasKey = [&listBase]( const utf8* pKey )
        {
            return pKey != nullptr && VectorUtil::containsIf( listBase, [pKey]( const PrefabOverridesInternal::KeyedComponent& component )
            { return component._key == pKey; } );
        };
        const XMLNode overrideRoot = overrideDoc.getRoot( kRootName );
        bool          bAllFound    = true;
        const auto    check        = [&]( const utf8* pElement, const utf8* pAttribute, bool bOptional )
        {
            for ( XMLNode entry = overrideRoot.findChild( pElement ); entry.isValid(); entry = entry.findNextSibling( pElement ) )
            {
                const utf8* pKey = entry.findAttribute( pAttribute );
                if ( ( bOptional && pKey == nullptr ) || hasKey( pKey ) )
                    continue;
                SW_LOG_ERROR_ID( "PrefabOverrides.VariantTargetMissing", "'%#': <%# %#=\"%#\"> names a component its base does not have", sourceName, pElement, pAttribute, pKey != nullptr ? pKey : "" );
                bAllFound = false;
            }
        };
        check( PrefabOverridesInternal::kOverride, PrefabOverridesInternal::kKey, false );
        check( PrefabOverridesInternal::kRemove, PrefabOverridesInternal::kKey, false );
        check( PrefabOverridesInternal::kAdd, PrefabOverridesInternal::kAfter, true );
        return bAllFound;
    }

    bool PrefabOverrides::writesField( string_view overrideXML, string_view componentKey, string_view fieldName )
    {
        XMLDocument doc;
        if ( StringUtil::trim( overrideXML ).empty() || doc.parse( overrideXML ) == false )
            return false;
        const XMLNode root = doc.getRoot( kRootName );
        const string  fieldText{ fieldName };
        const auto    hasField = [&fieldText]( const XMLNode& element )
        {
            return element.isValid() && ( element.hasAttribute( fieldText.c_str() ) || element.findChild( fieldText.c_str() ).isValid() );
        };
        if ( componentKey.empty() )
            return hasField( PrefabOverridesInternal::findChildElement( root, PrefabOverridesInternal::kObject ) );
        for ( XMLNode entry = root.findChild( PrefabOverridesInternal::kOverride ); entry.isValid(); entry = entry.findNextSibling( PrefabOverridesInternal::kOverride ) )
        {
            if ( entry.getAttributeText( PrefabOverridesInternal::kKey ) == componentKey && hasField( PrefabOverridesInternal::findFirstElement( entry ) ) )
                return true;
        }
        for ( XMLNode entry = root.findChild( PrefabOverridesInternal::kAdd ); entry.isValid(); entry = entry.findNextSibling( PrefabOverridesInternal::kAdd ) )
        {
            const XMLNode     added = PrefabOverridesInternal::findFirstElement( entry );
            const string_view label = added.isValid() && added.hasAttribute( PrefabOverridesInternal::kComponentName ) ? added.getAttributeText( PrefabOverridesInternal::kComponentName ) : string_view( added.isValid() ? added.getName() : "" );
            if ( added.isValid() && StringUtil::startsWith( componentKey, label ) && hasField( added ) )
                return true;
        }
        return false;
    }

    bool PrefabOverrides::omitDefaultValues( string_view stateXML, string& outStateXML )
    {
        outStateXML.clear();
        XMLDocument stateDoc;
        if ( stateDoc.parse( stateXML ) == false || stateDoc.getRoot().isValid() == false )
            return false;
        const XMLNode stateRoot = stateDoc.getRoot();

        // 기본값은 씬 밖에서 새로 만든 오브젝트 · 컴포넌트를 같은 직렬화기로 쓴 글이다(언리얼 CDO 자리).
        GameObjectManager scratch;
        ObjectSaveOptions options{ SerializePurpose::Authoring };
        options._bOmitExternalParent = SW_TRUE;
        options._bSkipValidation     = SW_TRUE; // 견본은 형식의 초기값이다 — 빈 필수 칸 경고가 거짓으로 남지 않게
        GameObject* pDefaultObject   = scratch.createGameObject( hashed_string( PrefabOverridesInternal::kBaseName ) );
        if ( pDefaultObject == nullptr )
            return false;
        XMLDocument defaultDoc;
        if ( defaultDoc.parse( ObjectStateSerializer::saveToXMLString( pDefaultObject, options ) ) == false || defaultDoc.getRoot().isValid() == false )
            return false;

        XMLDocument   outDoc;
        const XMLNode outRoot = outDoc.appendRoot( stateRoot.getName() );
        // 판 · 이름은 늘 쓴다(읽기가 찾는다). 컴포넌트 목록은 아래에서 원소마다 줄인다.
        for ( const utf8* pRootField : { sw::kSchemaVersionKey, PrefabOverridesInternal::kObjectName } )
        {
            const utf8* pValue = stateRoot.findAttribute( pRootField );
            if ( pValue != nullptr )
                outRoot.appendAttribute( pRootField, pValue );
        }
        vector<XMLAttribute> listAttribute;
        vector<XMLNode>      listChild;
        PrefabOverridesInternal::collectDifferences( stateRoot, defaultDoc.getRoot(), true, listAttribute, listChild );
        for ( const XMLAttribute& attribute : listAttribute )
        {
            outRoot.appendAttribute( attribute.getName(), attribute.getValue() );
        }
        for ( const XMLNode& child : listChild )
        {
            (void)outRoot.appendClone( child );
        }

        const XMLNode outList = outRoot.appendChild( PrefabOverridesInternal::kComponentList );
        const XMLNode list    = PrefabOverridesInternal::findChildElement( stateRoot, PrefabOverridesInternal::kComponentList );
        for ( XMLNode component = list.isValid() ? list.findChild() : XMLNode{}; component.isValid(); component = component.findNextSibling() )
        {
            if ( PrefabOverridesInternal::isElement( component ) == false )
                continue;
            const hashed_string typeName( component.getName() );
            GameObject*         pHolder    = scratch.createGameObject( hashed_string( PrefabOverridesInternal::kBaseName ) );
            Component*          pComponent = pHolder != nullptr ? scratch.addComponentByName( pHolder, typeName, false ) : nullptr;
            XMLDocument         holderDoc;
            const XMLNode       defaultElement = pComponent != nullptr && holderDoc.parse( ObjectStateSerializer::saveToXMLString( pHolder, options ) )
                                                   ? PrefabOverridesInternal::findChildElement( PrefabOverridesInternal::findChildElement( holderDoc.getRoot(), PrefabOverridesInternal::kComponentList ), component.getName() )
                                                   : XMLNode{};
            if ( defaultElement.isValid() == false )
            {
                (void)outList.appendClone( component ); // 만들지 못한 타입 — 기본값을 모른다
                continue;
            }
            PrefabOverridesInternal::collectDifferences( component, defaultElement, false, listAttribute, listChild );
            const XMLNode   element   = outList.appendChild( component.getName() );
            const TypeInfo* pTypeInfo = engine::getSubsystem<TypeRegistry>().findType( typeName );
            if ( pTypeInfo != nullptr )
            {
                for ( const string& required : DataInheritanceSchema::makeFromType( *pTypeInfo )._listKeptAttribute )
                {
                    const utf8* pValue = component.findAttribute( required.c_str() );
                    if ( pValue != nullptr && defaultElement.findAttribute( required.c_str() ) != nullptr && StringUtil::equals( pValue, defaultElement.findAttribute( required.c_str() ) ) )
                        element.appendAttribute( required.c_str(), pValue );
                }
            }
            for ( const XMLAttribute& attribute : listAttribute )
            {
                element.appendAttribute( attribute.getName(), attribute.getValue() );
            }
            for ( const XMLNode& child : listChild )
            {
                (void)element.appendClone( child );
            }
        }
        outStateXML = outRoot.toString();
        return outStateXML.empty() == false;
    }

    bool PrefabOverrides::appendPartOverrides( string& inoutOverrideXML, const vector<PrefabPartOverride>& listPartOverride )
    {
        if ( listPartOverride.empty() )
            return true;
        XMLDocument outDoc;
        XMLNode     outRoot;
        if ( StringUtil::trim( inoutOverrideXML ).empty() )
        {
            outRoot = outDoc.appendRoot( kRootName );
        }
        else
        {
            if ( outDoc.parse( inoutOverrideXML ) == false )
                return false;
            outRoot = outDoc.getRoot( kRootName );
        }
        if ( outRoot.isValid() == false )
            return false;

        for ( const PrefabPartOverride& part : listPartOverride )
        {
            XMLDocument partDoc;
            if ( part._partPath.empty() || partDoc.parse( part._overrideXML ) == false )
                return false;
            const XMLNode partRoot = partDoc.getRoot( kRootName );
            if ( partRoot.isValid() == false )
                return false;
            XMLNode entry = outRoot.appendChild( PrefabOverridesInternal::kPart );
            entry.appendAttribute( PrefabOverridesInternal::kPath, part._partPath );
            entry.appendClone( partRoot );
        }
        inoutOverrideXML = outRoot.toString();
        return true;
    }

    bool PrefabOverrides::collectPartOverrides( string_view overrideXML, vector<PrefabPartOverride>& outListPartOverride )
    {
        outListPartOverride.clear();
        if ( StringUtil::trim( overrideXML ).empty() )
            return true;
        XMLDocument doc;
        if ( doc.parse( overrideXML ) == false )
        {
            SW_LOG_ERROR_ID( "PrefabOverrides.PrefabOverridesCouldNot", "Prefab overrides could not be parsed - nested part overrides are not read" );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "PrefabOverrides.PrefabOverridesNoRoot2", "Prefab overrides have no <%#> root - nested part overrides are not read", kRootName );
            return false;
        }
        for ( XMLNode entry = root.findChild( PrefabOverridesInternal::kPart ); entry.isValid();
              entry         = entry.findNextSibling( PrefabOverridesInternal::kPart ) )
        {
            const utf8*   pPath = entry.findAttribute( PrefabOverridesInternal::kPath );
            const XMLNode body  = PrefabOverridesInternal::findChildElement( entry, kRootName );
            if ( StringUtil::isNullOrEmpty( pPath ) || body.isValid() == false )
            {
                SW_LOG_ERROR_ID( "PrefabOverrides.NestedPartOverrideNo", "A nested part override has no path or no <%#> body - nested part overrides are not read", kRootName );
                outListPartOverride.clear();
                return false;
            }
            outListPartOverride.push_back( PrefabPartOverride{ string{ pPath }, body.toString() } );
        }
        return true;
    }
} // namespace sw
