#include "pch.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/DataInheritanceSchemaRegistry.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct DataInheritanceInternal
        {
            /** @brief 부모의 조건 원소 다음에 자식의 조건 원소를 붙입니다 — 조건은 바꾸지 않고 쌓는다(합친 결과에서 위에서 아래로 얹는다). */
            static void appendWhenElements( const XMLNode& parent, const XMLNode& child, const XMLNode& out )
            {
                for ( const XMLNode& source : { parent, child } )
                {
                    if ( source.isValid() )
                        appendAllNamed( source, DataInheritance::kWhenElement, out );
                }
            }

            /**
             * @brief 차이에 적을 조건 원소입니다 — @p full 의 조건이 @p parent 의 조건으로 시작하면 그 뒤만 적습니다.
             * @return 부모의 조건을 지우거나 바꾼 결과라 쌓기로 적을 수 없으면 오류를 남기고 false 입니다.
             */
            [[nodiscard]] static bool writeWhenDelta( const XMLNode& full, const XMLNode& parent, const XMLNode& out )
            {
                XMLNode fullWhen   = full.isValid() ? full.findChild( DataInheritance::kWhenElement ) : XMLNode{};
                XMLNode parentWhen = parent.isValid() ? parent.findChild( DataInheritance::kWhenElement ) : XMLNode{};
                for ( ; parentWhen.isValid(); parentWhen = parentWhen.findNextSibling( DataInheritance::kWhenElement ) )
                {
                    if ( fullWhen.isValid() == false || fullWhen.toString() != parentWhen.toString() )
                    {
                        SW_LOG_ERROR_ID( "DataInheritance.WhenDropped", "A delta of <%#> cannot drop or change a %# its base has - conditions stack (write a later %# that sets the values back)",
                                         full.isValid() ? full.getName() : parent.getName(), DataInheritance::kWhenElement, DataInheritance::kWhenElement );
                        return false;
                    }
                    fullWhen = fullWhen.findNextSibling( DataInheritance::kWhenElement );
                }
                for ( ; fullWhen.isValid(); fullWhen = fullWhen.findNextSibling( DataInheritance::kWhenElement ) )
                {
                    (void)out.appendClone( fullWhen );
                }
                return true;
            }

            /** @brief 파일에만 있고 합친 결과에는 남지 않는 속성입니다. */
            static bool isMetaAttribute( const utf8* pName )
            {
                const string_view name = pName;
                return name == DataInheritance::kBaseAttribute || name == DataInheritance::kOpAttribute || name == DataInheritance::kBeforeAttribute || name == DataInheritance::kAfterAttribute;
            }

            /** @brief 이름이 같은 첫 자식 원소입니다. 부모가 무효이거나 없으면 무효 노드입니다. */
            static XMLNode findChildElement( const XMLNode& parent, const utf8* pName )
            {
                return parent.isValid() ? parent.findChild( pName ) : XMLNode{};
            }

            /** @brief 원소에 속성이나 자식이 있으면 true 입니다(차이가 있는가). */
            static bool hasContent( const XMLNode& node )
            {
                return node.getFirstAttribute().isValid() || node.findChild().isValid();
            }

            /** @brief 합치는 동안 쓰는 목록 원소 하나입니다. 부모 쪽 노드와 자식 쪽 노드 가운데 있는 것을 듭니다. */
            struct ListEntry
            {
                string  _key;
                XMLNode _parentNode;
                XMLNode _childNode;
            };

            /**
             * @brief 합치는 동안의 목록입니다 — 차례는 배열, 찾기는 열쇠 색인. 열쇠는 겹치지 않는다(넣는 쪽이 먼저 찾아 오류로 멈춘다).
             * @details 카탈로그 하나가 원소 수천 개이고 자식 파일의 원소마다 한 번 찾는다 — 훑어 찾으면 원소 수의 제곱이다. 자리를 옮기는 지움 · 자리 지정(드물다)만 색인을 다시 짓는다.
             */
            struct KeyedEntryList
            {
                vector<ListEntry>             _listEntry{};
                unordered_map<string, uint32> _mapKeyToIndex{};

                ListEntry* find( string_view key )
                {
                    const auto found = _mapKeyToIndex.find( key );
                    return found != _mapKeyToIndex.end() ? &_listEntry[found->second] : nullptr;
                }

                void append( ListEntry&& entry )
                {
                    (void)_mapKeyToIndex.try_emplace( entry._key, static_cast<uint32>( _listEntry.size() ) );
                    _listEntry.push_back( std::move( entry ) );
                }

                /** @brief 있는 열쇠의 원소를 뺍니다. */
                void erase( string_view key )
                {
                    _listEntry.erase( _listEntry.begin() + ( find( key ) - _listEntry.data() ) );
                    rebuildIndex();
                }

                /** @brief 있는 열쇠 @p key 의 원소를 있는 열쇠 @p anchor 의 앞 · 뒤로 옮깁니다. */
                void moveNextTo( string_view key, string_view anchor, bool bAfter )
                {
                    ListEntry*      pEntry      = find( key );
                    const ptrdiff_t entryIndex  = pEntry - _listEntry.data();
                    const ptrdiff_t anchorIndex = find( anchor ) - _listEntry.data();
                    ListEntry       moved       = *pEntry;
                    _listEntry.erase( _listEntry.begin() + entryIndex );
                    const ptrdiff_t shiftedAnchor = anchorIndex > entryIndex ? anchorIndex - 1 : anchorIndex;
                    _listEntry.insert( _listEntry.begin() + shiftedAnchor + ( bAfter ? 1 : 0 ), std::move( moved ) );
                    rebuildIndex();
                }

                void rebuildIndex()
                {
                    _mapKeyToIndex.clear();
                    for ( size_t index = 0; index < _listEntry.size(); ++index )
                    {
                        (void)_mapKeyToIndex.try_emplace( _listEntry[index]._key, static_cast<uint32>( index ) );
                    }
                }
            };

            /** @brief 부모 속성 위에 자식 속성을 덮어 씁니다. 파일에만 있는 속성은 옮기지 않습니다. */
            static void mergeAttributes( const XMLNode& parent, const XMLNode& child, const XMLNode& out )
            {
                for ( const XMLNode& source : { parent, child } )
                {
                    if ( source.isValid() == false )
                        continue;
                    for ( XMLAttribute attribute = source.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                    {
                        if ( isMetaAttribute( attribute.getName() ) == false )
                            out.setAttribute( attribute.getName(), attribute.getValue() );
                    }
                }
            }

            /** @brief @p parent 아래 이름이 @p pName 인 자식 원소를 모두 @p out 에 옮깁니다. */
            static void appendAllNamed( const XMLNode& parent, const utf8* pName, const XMLNode& out )
            {
                for ( XMLNode element = parent.findChild( pName ); element.isValid(); element = element.findNextSibling( pName ) )
                {
                    (void)out.appendClone( element );
                }
            }

            /** @brief @p parent 아래 이름이 @p pName 인 자식 원소를 이어 쓴 글입니다. 같은 이름의 형제 묶음을 맞댈 때 씁니다. */
            static string joinAllNamed( const XMLNode& parent, const utf8* pName )
            {
                string text;
                for ( XMLNode element = parent.findChild( pName ); element.isValid(); element = element.findNextSibling( pName ) )
                {
                    text += element.toString();
                }
                return text;
            }

            /** @brief 구조체 칸은 하나다 — 같은 이름을 또 적었으면 오류를 남기고 false 입니다(첫 원소만 합치면 뒤 원소가 말없이 사라진다). */
            [[nodiscard]] static bool isSingleField( const XMLNode& element, string_view sourceName )
            {
                if ( element.isValid() == false || element.findNextSibling( element.getName() ).isValid() == false )
                    return true;
                SW_LOG_ERROR_ID( "DataInheritance.FieldRepeated", "'%#': <%#> is written twice - its merge rule says it is a single field", sourceName, element.getName() );
                return false;
            }

            /**
             * @brief 칸 원소들을 합칩니다. 부모 순서를 지키고 자식에만 있는 칸은 뒤에 더합니다.
             * @details 규칙에 없는 이름은 같은 이름의 형제까지 묶음 하나다 — 자식이 하나라도 적으면 부모의 첫 자리에 자식의 묶음이 들어가고 부모의 묶음은 모두 빠진다. 자식이 적지 않은 이름은 부모 자리 그대로다.
             */
            [[nodiscard]] static bool mergeFieldElements( const XMLNode& parent, const XMLNode& child, const DataInheritanceSchema& schema, const XMLNode& out, string_view sourceName )
            {
                if ( parent.isValid() )
                {
                    vector<string> listReplacedName;
                    for ( XMLNode element = parent.findChild(); element.isValid(); element = element.findNextSibling() )
                    {
                        if ( element.isElement() == false || DataInheritance::isWhenElement( element ) )
                            continue;
                        const XMLNode                childElement = findChildElement( child, element.getName() );
                        const DataInheritanceSchema* pField       = schema.findField( element.getName() );
                        if ( childElement.isValid() == false )
                        {
                            (void)out.appendClone( element );
                        }
                        else if ( pField == nullptr )
                        {
                            const string name{ element.getName() };
                            if ( VectorUtil::contains( listReplacedName, name ) )
                                continue;
                            listReplacedName.push_back( name );
                            appendAllNamed( child, element.getName(), out );
                        }
                        else if ( isSingleField( element, sourceName ) == false || isSingleField( childElement, sourceName ) == false ||
                                  mergeElement( element, childElement, *pField, out.appendChild( element.getName() ), sourceName ) == false )
                        {
                            return false;
                        }
                    }
                }
                if ( child.isValid() )
                {
                    for ( XMLNode element = child.findChild(); element.isValid(); element = element.findNextSibling() )
                    {
                        if ( element.isElement() == false || DataInheritance::isWhenElement( element ) || findChildElement( parent, element.getName() ).isValid() )
                            continue;
                        const DataInheritanceSchema* pField = schema.findField( element.getName() );
                        if ( pField == nullptr )
                            (void)out.appendClone( element );
                        else if ( isSingleField( element, sourceName ) == false || mergeElement( XMLNode{}, element, *pField, out.appendChild( element.getName() ), sourceName ) == false )
                            return false;
                    }
                }
                appendWhenElements( parent, child, out );
                return true;
            }

            /** @brief 목록 원소를 차례로 모읍니다. 열쇠가 없거나 겹치는 원소는 오류입니다 — 겹치면 합치기는 첫 원소만 고치고, 차이 적기는 목록 밖 자리를 읽는다. */
            [[nodiscard]] static bool collectParentEntries( const XMLNode& parent, const DataInheritanceSchema& schema, KeyedEntryList& outListEntry, string_view sourceName )
            {
                if ( parent.isValid() == false )
                    return true;
                for ( XMLNode element = parent.findChild(); element.isValid(); element = element.findNextSibling() )
                {
                    if ( element.isElement() == false || DataInheritance::isWhenElement( element ) )
                        continue; // 조건 원소는 목록 원소가 아니다 — 목록 원소 자신에 얹는 차이다(`appendWhenElements`)
                    optional<string> key = schema.findElementKey( element );
                    if ( key.has_value() == false )
                    {
                        SW_LOG_ERROR_ID( "DataInheritance.ListKeyMissing", "'%#': an element of list '%#' in its base has no key '%#'", sourceName, schema._name, schema._keyAttribute );
                        return false;
                    }
                    if ( outListEntry.find( *key ) != nullptr )
                    {
                        SW_LOG_ERROR_ID( "DataInheritance.DuplicateKey", "'%#': element '%#' is written twice in list '%#'", sourceName, *key, schema._name );
                        return false;
                    }
                    outListEntry.append( ListEntry{ std::move( *key ), element, XMLNode{} } );
                }
                return true;
            }

            /** @brief 자식이 적은 자리(`_before` · `_after`)로 원소를 옮깁니다. 둘 다 적었거나 없는 열쇠를 가리키면 오류입니다. */
            [[nodiscard]] static bool moveEntry( KeyedEntryList& inoutListEntry, string_view key, const XMLNode& element, string_view sourceName )
            {
                const utf8* pBefore = element.findAttribute( DataInheritance::kBeforeAttribute );
                const utf8* pAfter  = element.findAttribute( DataInheritance::kAfterAttribute );
                if ( pBefore == nullptr && pAfter == nullptr )
                    return true;
                if ( pBefore != nullptr && pAfter != nullptr )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.BothAnchors", "'%#': element '%#' has both %# and %#", sourceName, key, DataInheritance::kBeforeAttribute, DataInheritance::kAfterAttribute );
                    return false;
                }
                const string_view anchor = pBefore != nullptr ? pBefore : pAfter;
                if ( anchor == key || inoutListEntry.find( anchor ) == nullptr )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.AnchorMissing", "'%#': element '%#' is placed next to '%#', which the list does not have", sourceName, key, anchor );
                    return false;
                }
                inoutListEntry.moveNextTo( key, anchor, pAfter != nullptr );
                return true;
            }

            /** @brief @p child 가 적은 정체 속성(`DataInheritanceSchema::_listIdentityAttribute`) 가운데 @p parent 와 값이 다른 첫 이름입니다. 없으면 nullptr 입니다. */
            [[nodiscard]] static const string* findIdentityChange( const XMLNode& parent, const XMLNode& child, const DataInheritanceSchema& schema )
            {
                for ( const string& attribute : schema._listIdentityAttribute )
                {
                    const utf8* pChildValue = child.findAttribute( attribute.c_str() );
                    if ( pChildValue != nullptr && string_view{ pChildValue } != parent.getAttributeText( attribute.c_str() ) )
                        return &attribute;
                }
                return nullptr;
            }

            /** @brief 자식의 목록 원소 하나를 얹습니다. */
            [[nodiscard]] static bool applyChildEntry( const XMLNode& element, const DataInheritanceSchema& schema, KeyedEntryList& inoutListEntry, string_view sourceName )
            {
                const optional<string> elementKey = schema.findElementKey( element );
                if ( elementKey.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.ListKeyMissing2", "'%#': an element of list '%#' has no key '%#'", sourceName, schema._name, schema._keyAttribute );
                    return false;
                }
                const string_view key      = *elementKey;
                const string_view op       = element.getAttributeText( DataInheritance::kOpAttribute );
                ListEntry*        pEntry   = inoutListEntry.find( key );
                const bool        bRemove  = op == DataInheritance::kOpRemove;
                const bool        bReplace = op == DataInheritance::kOpReplace;
                if ( op.empty() == false && bRemove == false && bReplace == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.UnknownOp", "'%#': element '%#' has %#='%#' - must be %# or %#", sourceName, key, DataInheritance::kOpAttribute, op, DataInheritance::kOpRemove, DataInheritance::kOpReplace );
                    return false;
                }
                if ( pEntry == nullptr && ( bRemove || bReplace ) )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.OpKeyMissing", "'%#': %# of '%#' in list '%#', which its base does not have", sourceName, op, key, schema._name );
                    return false;
                }
                if ( pEntry != nullptr && pEntry->_childNode.isValid() )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.DuplicateKey", "'%#': element '%#' is written twice in list '%#'", sourceName, key, schema._name );
                    return false;
                }
                if ( bRemove )
                {
                    inoutListEntry.erase( key );
                    return true;
                }
                const string* pChangedIdentity = pEntry != nullptr && bReplace == false ? findIdentityChange( pEntry->_parentNode, element, schema ) : nullptr;
                if ( pChangedIdentity != nullptr )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.KeyCollision", "'%#': element '%#' of list '%#' has another %# than its base - the key collides with the base's element (use a new key, or %#=\"%#\" to replace it)",
                                     sourceName, key, schema._name, *pChangedIdentity, DataInheritance::kOpAttribute, DataInheritance::kOpReplace );
                    return false;
                }
                if ( pEntry == nullptr )
                {
                    inoutListEntry.append( ListEntry{ string{ key }, XMLNode{}, element } );
                }
                else
                {
                    pEntry->_childNode = element;
                    if ( bReplace )
                        pEntry->_parentNode = XMLNode{};
                }
                return moveEntry( inoutListEntry, key, element, sourceName );
            }

            /** @brief 열쇠로 목록을 합칩니다. @p out 에 결과 원소를 씁니다. */
            [[nodiscard]] static bool mergeKeyedList( const XMLNode& parent, const XMLNode& child, const DataInheritanceSchema& schema, const XMLNode& out, string_view sourceName )
            {
                KeyedEntryList listEntry;
                if ( collectParentEntries( parent, schema, listEntry, sourceName ) == false )
                    return false;
                if ( child.isValid() )
                {
                    for ( XMLNode element = child.findChild(); element.isValid(); element = element.findNextSibling() )
                    {
                        if ( element.isElement() && DataInheritance::isWhenElement( element ) == false && applyChildEntry( element, schema, listEntry, sourceName ) == false )
                            return false;
                    }
                }
                appendWhenElements( parent, child, out );
                for ( const ListEntry& entry : listEntry._listEntry )
                {
                    if ( entry._childNode.isValid() == false )
                    {
                        (void)out.appendClone( entry._parentNode );
                        continue;
                    }
                    const XMLNode item = out.appendChild( entry._childNode.getName() );
                    mergeAttributes( entry._parentNode, entry._childNode, item );
                    schema.writeElementKey( item, entry._key ); // 열쇠가 글이면 속성 합치기가 옮기지 않는다
                    if ( schema._bSelfNested != SW_FALSE )
                    {
                        if ( entry._parentNode.isValid() == false )
                        {
                            for ( XMLNode element = entry._childNode.findChild(); element.isValid(); element = element.findNextSibling() )
                            {
                                (void)item.appendClone( element );
                            }
                        }
                        else if ( mergeKeyedList( entry._parentNode, entry._childNode, schema, item, sourceName ) == false )
                        {
                            return false;
                        }
                        continue;
                    }
                    if ( mergeFieldElements( entry._parentNode, entry._childNode, schema, item, sourceName ) == false )
                        return false;
                }
                return true;
            }

            /** @brief 원소 하나를 합칩니다(속성 + 목록 또는 칸 원소). @p parent 는 무효일 수 있습니다(부모에 없는 칸). */
            [[nodiscard]] static bool mergeElement( const XMLNode& parent, const XMLNode& child, const DataInheritanceSchema& schema, const XMLNode& out, string_view sourceName )
            {
                mergeAttributes( parent, child, out );
                return schema._keyAttribute.empty() ? mergeFieldElements( parent, child, schema, out, sourceName ) : mergeKeyedList( parent, child, schema, out, sourceName );
            }

            /**
             * @brief @p text 를 합쳐 @p outText 에 씁니다. @p inoutChain 은 지나온 부모 id 입니다.
             * @details 출처는 사슬의 위부터 얹는다 — 재귀에서 돌아오는 순서가 곧 그 순서다.
             */
            [[nodiscard]] static bool flattenChain( string_view text, string_view sourceName, const DataInheritanceSchema& schema, DataInheritanceSource& source, vector<string>& inoutChain, string& outText,
                                                    DataInheritanceProvenance* pOutProvenance )
            {
                XMLDocument childDoc;
                if ( childDoc.parse( text, sourceName ) == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.NotXML", "'%#' could not be parsed: %#", sourceName, childDoc.getLastError() );
                    return false;
                }
                const XMLNode childRoot = childDoc.getRoot();
                if ( childRoot.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.NoRoot", "'%#' has no root element", sourceName );
                    return false;
                }
                const string baseID = DataInheritance::resolveBaseText( childRoot.getAttributeText( DataInheritance::kBaseAttribute ) );
                if ( baseID.empty() )
                {
                    outText = string{ text };
                    if ( pOutProvenance != nullptr )
                        pOutProvenance->applyLayer( childRoot, schema, sourceName );
                    return true;
                }
                if ( VectorUtil::contains( inoutChain, baseID ) )
                {
                    DataInheritance::reportCycle( baseID, sourceName );
                    return false;
                }
                const optional<uint32> maxDepth = source.findMaxDepth();
                if ( maxDepth.has_value() == false )
                {
                    DataInheritance::reportNoDepthLimit( sourceName );
                    return false;
                }
                if ( inoutChain.size() >= *maxDepth )
                {
                    DataInheritance::reportTooDeep( baseID, sourceName, *maxDepth );
                    return false;
                }
                inoutChain.push_back( baseID );

                string baseText;
                if ( source.readText( baseID, baseText ) == false )
                {
                    DataInheritance::reportBaseNotFound( baseID, sourceName );
                    return false;
                }
                string flatBaseText;
                if ( flattenChain( baseText, baseID, schema, source, inoutChain, flatBaseText, pOutProvenance ) == false )
                    return false;
                XMLDocument baseDoc;
                if ( baseDoc.parse( flatBaseText, baseID ) == false )
                    return false;
                const XMLNode baseRoot = baseDoc.getRoot( childRoot.getName() );
                if ( baseRoot.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "DataInheritance.BaseFormatDiffers", "'%#': base '%#' is not a <%#> document", sourceName, baseID, childRoot.getName() );
                    return false;
                }

                XMLDocument   mergedDoc;
                const XMLNode mergedRoot = mergedDoc.appendRoot( childRoot.getName() );
                if ( mergeElement( baseRoot, childRoot, schema, mergedRoot, sourceName ) == false )
                    return false;
                outText = mergedDoc.saveToString();
                if ( pOutProvenance != nullptr )
                    pOutProvenance->applyLayer( childRoot, schema, sourceName );
                return true;
            }

            /** @brief 저장이 @p attributeName 을 지우지 않는 칸이면 true 입니다. */
            static bool isKeptAttribute( const DataInheritanceSchema& schema, string_view attributeName )
            {
                return VectorUtil::containsIf( schema._listKeptAttribute, [attributeName]( const string& kept )
                { return kept == attributeName; } );
            }

            /** @brief 다른 속성만 적습니다. @p parent 는 무효일 수 있습니다. */
            static void writeAttributeDelta( const XMLNode& full, const XMLNode& parent, const DataInheritanceSchema& schema, const XMLNode& out )
            {
                for ( XMLAttribute attribute = full.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const utf8* pInherited = parent.isValid() ? parent.findAttribute( attribute.getName() ) : nullptr;
                    const bool  bDiffers   = pInherited == nullptr || string_view( pInherited ) != string_view( attribute.getValue() );
                    if ( bDiffers || isKeptAttribute( schema, attribute.getName() ) )
                        out.setAttribute( attribute.getName(), attribute.getValue() );
                }
                if ( schema._bEmptyClears == SW_FALSE || parent.isValid() == false )
                    return;
                for ( XMLAttribute attribute = parent.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( full.findAttribute( attribute.getName() ) == nullptr )
                        out.setAttribute( attribute.getName(), string_view{} );
                }
            }

            /** @brief 다른 칸 원소만 적습니다. */
            [[nodiscard]] static bool writeFieldDelta( const XMLNode& full, const XMLNode& parent, const DataInheritanceSchema& schema, const XMLNode& out )
            {
                vector<string> listComparedName;
                for ( XMLNode element = full.findChild(); element.isValid(); element = element.findNextSibling() )
                {
                    if ( element.isElement() == false || DataInheritance::isWhenElement( element ) )
                        continue;
                    const XMLNode                inherited = findChildElement( parent, element.getName() );
                    const DataInheritanceSchema* pField    = schema.findField( element.getName() );
                    if ( inherited.isValid() == false )
                    {
                        (void)out.appendClone( element );
                        continue;
                    }
                    if ( pField == nullptr )
                    {
                        // 합치기가 같은 이름의 형제를 묶음으로 바꾸므로 묶음으로 맞대고 묶음으로 적는다.
                        const string name{ element.getName() };
                        if ( VectorUtil::contains( listComparedName, name ) )
                            continue;
                        listComparedName.push_back( name );
                        if ( joinAllNamed( parent, element.getName() ) != joinAllNamed( full, element.getName() ) )
                            appendAllNamed( full, element.getName(), out );
                        continue;
                    }
                    if ( writeElementDeltaIfAny( element, inherited, *pField, out ) == false )
                        return false;
                }
                if ( writeWhenDelta( full, parent, out ) == false )
                    return false;
                if ( parent.isValid() == false )
                    return true;
                for ( XMLNode element = parent.findChild(); element.isValid(); element = element.findNextSibling() )
                {
                    if ( element.isElement() == false || DataInheritance::isWhenElement( element ) || findChildElement( full, element.getName() ).isValid() )
                        continue;
                    const DataInheritanceSchema* pField = schema.findField( element.getName() );
                    if ( pField != nullptr && pField->_keyAttribute.empty() == false )
                    {
                        if ( writeElementDeltaIfAny( XMLNode{}, element, *pField, out ) == false )
                            return false;
                    }
                    else if ( schema._bEmptyClears == SW_TRUE )
                    {
                        (void)out.appendChild( element.getName() ); // 비어서 쓰지 않은 칸이다 — 부모 값을 지운다
                    }
                }
                return true;
            }

            /** @brief 차이가 있을 때만 원소를 적습니다. 지울 수 없는 노드라 임시 문서에 만들어 보고 옮깁니다. */
            [[nodiscard]] static bool writeElementDeltaIfAny( const XMLNode& full, const XMLNode& parent, const DataInheritanceSchema& schema, const XMLNode& out )
            {
                const XMLNode& named = full.isValid() ? full : parent;
                XMLDocument    scratch;
                const XMLNode  scratchRoot = scratch.appendRoot( named.getName() );
                if ( full.isValid() )
                    writeAttributeDelta( full, parent, schema, scratchRoot );
                const bool bWritten = schema._keyAttribute.empty() ? writeFieldDelta( full, parent, schema, scratchRoot ) : writeKeyedListDelta( full, parent, schema, scratchRoot );
                if ( bWritten && hasContent( scratchRoot ) )
                    (void)out.appendClone( scratchRoot );
                return bWritten;
            }

            /**
             * @brief 목록의 차이를 적습니다. 지운 원소, 그다음 @p full 순서대로 다른 원소 · 새 원소 · 자리가 다른 원소입니다.
             * @details 합치기와 같은 순서로 작업 목록을 움직이며 자리를 맞춘다 — 앞 칸이 같으면 그 칸은 더 움직이지 않으므로 옮김을 최소로 적는다.
             */
            [[nodiscard]] static bool writeKeyedListDelta( const XMLNode& full, const XMLNode& parent, const DataInheritanceSchema& schema, const XMLNode& out )
            {
                KeyedEntryList parentEntries;
                if ( collectParentEntries( parent, schema, parentEntries, "delta" ) == false )
                    return false;
                KeyedEntryList fullEntries;
                if ( collectParentEntries( full, schema, fullEntries, "delta" ) == false )
                    return false;
                const vector<ListEntry>& listFull = fullEntries._listEntry;
                if ( writeWhenDelta( full, parent, out ) == false )
                    return false;
                for ( size_t index = parentEntries._listEntry.size(); index > 0; --index )
                {
                    const ListEntry& entry = parentEntries._listEntry[index - 1];
                    if ( fullEntries.find( entry._key ) != nullptr )
                        continue;
                    const XMLNode removed = out.appendChild( entry._parentNode.getName() );
                    schema.writeElementKey( removed, entry._key );
                    removed.setAttribute( DataInheritance::kOpAttribute, DataInheritance::kOpRemove );
                }
                // 작업 목록은 열쇠의 차례만 든다 — 원소는 부모 쪽 색인에서 찾는다(자리를 옮겨도 색인을 다시 짓지 않는다).
                vector<string> listWorking;
                listWorking.reserve( listFull.size() );
                for ( const ListEntry& entry : parentEntries._listEntry )
                {
                    if ( fullEntries.find( entry._key ) != nullptr )
                        listWorking.push_back( entry._key );
                }
                for ( size_t index = 0; index < listFull.size(); ++index )
                {
                    const ListEntry& fullEntry = listFull[index];
                    const ListEntry* pEntry    = parentEntries.find( fullEntry._key );
                    const bool       bNew      = pEntry == nullptr;
                    if ( bNew )
                        listWorking.push_back( fullEntry._key );
                    const bool bInPlace = listWorking[index] == fullEntry._key;

                    XMLDocument   scratch;
                    const XMLNode item = scratch.appendRoot( fullEntry._parentNode.getName() );
                    // 정체가 바뀐 원소는 고침이 아니라 바꿈이다 — 차이로 적으면 읽기가 열쇠 겹침으로 거절한다(`_listIdentityAttribute`).
                    const bool bReplaced = bNew == false && findIdentityChange( pEntry->_parentNode, fullEntry._parentNode, schema ) != nullptr;
                    if ( bReplaced )
                        item.setAttribute( DataInheritance::kOpAttribute, DataInheritance::kOpReplace );
                    if ( bNew || bReplaced )
                    {
                        mergeAttributes( XMLNode{}, fullEntry._parentNode, item );
                        for ( XMLNode element = fullEntry._parentNode.findChild(); element.isValid(); element = element.findNextSibling() )
                        {
                            if ( element.isElement() )
                                (void)item.appendClone( element );
                        }
                    }
                    else
                    {
                        writeAttributeDelta( fullEntry._parentNode, pEntry->_parentNode, schema, item );
                        if ( writeFieldDelta( fullEntry._parentNode, pEntry->_parentNode, schema, item ) == false )
                            return false;
                    }
                    if ( bInPlace == false )
                    {
                        // 앞 칸이 있으면 그 뒤로, 맨 앞이면 지금 맨 앞 앞으로 옮긴다.
                        if ( index > 0 )
                            item.setAttribute( DataInheritance::kAfterAttribute, string_view( listFull[index - 1]._key ) );
                        else
                            item.setAttribute( DataInheritance::kBeforeAttribute, string_view( listWorking[0] ) );
                        // 앞 칸들은 이미 맞았다 — 남은 칸에서 찾아 이 자리로 돌려 놓는다(새 원소는 맨 끝에 있다).
                        const auto itCurrent = bNew ? listWorking.end() - 1 : std::find( listWorking.begin() + static_cast<ptrdiff_t>( index ), listWorking.end(), fullEntry._key );
                        std::rotate( listWorking.begin() + static_cast<ptrdiff_t>( index ), itCurrent, itCurrent + 1 );
                    }
                    if ( hasContent( item ) == false && bNew == false )
                        continue; // 새 원소는 열쇠(글)만으로도 차이다
                    schema.writeElementKey( item, fullEntry._key );
                    (void)out.appendClone( item );
                }
                return true;
            }
        };

        /** @brief `makeFromType` 의 재귀 단계입니다. @p inoutStack 은 지나온 형식이고, 자기 자신을 담는 칸은 통째로 바꾸는 칸으로 둡니다. */
        struct DataInheritanceSchemaBuilder
        {
            static const PropertyInfo* findKeyProperty( const TypeInfo& typeInfo )
            {
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop._metadata->_bInheritKey == SW_TRUE )
                        return &prop;
                }
                return nullptr;
            }

            static void fill( const TypeInfo& typeInfo, DataInheritanceSchema& out, vector<const TypeInfo*>& inoutStack )
            {
                inoutStack.push_back( &typeInfo );
                const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    // 열쇠 칸은 목록 원소를 쓸 때 따로 적는다 — 남기는 칸에 넣으면 바뀌지 않은 원소도 "내용이 있다" 로 보여 자식 파일에 모두 적힌다
                    if ( prop._metadata->_bRequired == SW_TRUE && prop._metadata->_bInheritKey != SW_TRUE )
                        out._listKeptAttribute.emplace_back( prop._name.c_str() );
                    const bool      bContainer = prop._bIsContainer == SW_TRUE;
                    const bool      bNested    = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                    const bool      bSequence  = prop._containerWrapper != nullptr && prop._containerWrapper->asSequence() != nullptr && bNested == false;
                    const TypeInfo* pFieldType = nullptr;
                    if ( bContainer == false )
                        pFieldType = SerializerUtil::findNestedObjectType( prop._typeName, prop.findValueBinding(), context );
                    else if ( bSequence )
                        pFieldType = SerializerUtil::findNestedObjectType( prop._elementTypeName, prop.findElementBinding(), context );
                    if ( pFieldType == nullptr || VectorUtil::contains( inoutStack, pFieldType ) )
                        continue;
                    const PropertyInfo* pKey = bContainer ? findKeyProperty( *pFieldType ) : nullptr;
                    if ( bContainer && pKey == nullptr )
                        continue; // 열쇠 없는 목록은 통째로 바뀐다
                    DataInheritanceSchema field{ prop._name.c_str() };
                    if ( pKey != nullptr )
                        field._keyAttribute = pKey->_name.c_str();
                    fill( *pFieldType, field, inoutStack );
                    out._listField.push_back( std::move( field ) );
                }
                inoutStack.pop_back();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DataInheritanceResourceSource::DataInheritanceResourceSource()
        : DataInheritanceSource{ engine::getSubsystem<ResourceComposition>().findInheritanceMaxDepth() }
    {
    }

    bool DataInheritanceResourceSource::readText( string_view assetID, string& outText )
    {
        return XMLReadUtil::readText( AssetID{ assetID }, outText );
    }

    DataInheritanceDiskSource::DataInheritanceDiskSource( const AbsolutePath& contentRoot, optional<uint32> maxDepth )
        : DataInheritanceSource{ maxDepth }
        , _contentRoot{ contentRoot }
    {
    }

    bool DataInheritanceDiskSource::readText( string_view assetID, string& outText )
    {
        const AbsolutePath diskPath = _contentRoot.join( assetID );
        return AssetIO::readTextFile( diskPath, outText ); // 없는 파일도 작업의 입력(해시 0)으로 남는다 — 부모가 생기면 다시 굽는다
    }

    DataInheritanceSchema::DataInheritanceSchema( string_view name )
        : _name{ name }
        , _keyAttribute{}
        , _listKeptAttribute{}
        , _listIdentityAttribute{}
        , _listField{}
        , _bEmptyClears{ SW_TRUE }
        , _bSelfNested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    optional<string> DataInheritanceSchema::findElementKey( const XMLNode& element ) const
    {
        if ( _keyAttribute == kTextKey )
        {
            const string_view text = StringUtil::trim( element.getText() );
            return text.empty() ? optional<string>{} : optional<string>{ string{ text } };
        }
        const utf8* pKey = element.findAttribute( _keyAttribute.c_str() );
        return pKey != nullptr ? optional<string>{ string{ pKey } } : optional<string>{};
    }

    void DataInheritanceSchema::writeElementKey( const XMLNode& element, string_view key ) const
    {
        if ( _keyAttribute == kTextKey )
            element.setValue( key );
        else
            element.setAttribute( _keyAttribute.c_str(), key );
    }

    DataInheritanceSchema DataInheritanceSchema::makeKeyedList( string_view name, string_view keyAttribute )
    {
        DataInheritanceSchema list{ name };
        list._keyAttribute = keyAttribute;
        return list;
    }

    const DataInheritanceSchema* DataInheritanceSchema::findField( string_view name ) const
    {
        for ( const DataInheritanceSchema& field : _listField )
        {
            if ( field._name == name )
                return &field;
        }
        return nullptr;
    }

    DataInheritanceSchema DataInheritanceSchema::makeFromType( const TypeInfo& typeInfo )
    {
        DataInheritanceSchema   schema{ typeInfo._name.c_str() };
        vector<const TypeInfo*> listStack;
        DataInheritanceSchemaBuilder::fill( typeInfo, schema, listStack );
        return schema;
    }

    const string* DataInheritanceProvenance::findOrigin( string_view fieldPath ) const
    {
        for ( const FieldOrigin& origin : _listOrigin )
        {
            if ( origin._fieldPath == fieldPath )
                return &origin._assetID;
        }
        return nullptr;
    }

    void DataInheritanceProvenance::setOrigin( const string& fieldPath, string_view assetID )
    {
        for ( FieldOrigin& origin : _listOrigin )
        {
            if ( origin._fieldPath == fieldPath )
            {
                origin._assetID = string{ assetID };
                return;
            }
        }
        _listOrigin.push_back( FieldOrigin{ fieldPath, string{ assetID } } );
    }

    void DataInheritanceProvenance::eraseUnder( const string& fieldPath )
    {
        StringBuilder<constant::buffer::kMax256> nestedPrefixBuilder;
        nestedPrefixBuilder.appendFormat( "%#.", fieldPath );
        const string_view nestedPrefix = nestedPrefixBuilder.view();
        (void)VectorUtil::removeIf( _listOrigin, [&]( const FieldOrigin& origin )
        { return origin._fieldPath == fieldPath || StringUtil::startsWith( origin._fieldPath, nestedPrefix ); } );
    }

    void DataInheritanceProvenance::applyElement( const XMLNode& element, const DataInheritanceSchema& schema, const string& prefix, string_view assetID )
    {
        for ( XMLAttribute attribute = element.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            if ( DataInheritanceInternal::isMetaAttribute( attribute.getName() ) == false )
            {
                StringBuilder<constant::buffer::kMax256> attributePath;
                attributePath.appendFormat( "%#%#", prefix, attribute.getName() );
                setOrigin( string( attributePath.view() ), assetID );
            }
        }
        for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( child.isElement() == false || DataInheritance::isWhenElement( child ) )
                continue; // 조건 원소는 칸이 아니다 — 어느 층이 그 칸을 적었는지는 펼친 뒤 조건 단계 앞의 값이다
            const DataInheritanceSchema*             pField = schema.findField( child.getName() );
            StringBuilder<constant::buffer::kMax256> pathBuilder;
            pathBuilder.appendFormat( "%#%#", prefix, child.getName() );
            const string path( pathBuilder.view() );
            if ( pField == nullptr )
            {
                setOrigin( path, assetID );
                continue;
            }
            if ( pField->_keyAttribute.empty() )
            {
                pathBuilder.append( '.' );
                applyElement( child, *pField, string( pathBuilder.view() ), assetID );
                continue;
            }
            for ( XMLNode item = child.findChild(); item.isValid(); item = item.findNextSibling() )
            {
                if ( item.isElement() == false || DataInheritance::isWhenElement( item ) )
                    continue;
                StringBuilder<constant::buffer::kMax256> itemPathBuilder;
                itemPathBuilder.appendFormat( "%#[%#]", path, pField->findElementKey( item ).value_or( string{} ) ); // 열쇠 없는 원소는 펼치기가 이미 거절했다
                const string_view op = item.getAttributeText( DataInheritance::kOpAttribute );
                if ( op == DataInheritance::kOpRemove || op == DataInheritance::kOpReplace )
                    eraseUnder( string( itemPathBuilder.view() ) );
                if ( op != DataInheritance::kOpRemove )
                {
                    itemPathBuilder.append( '.' );
                    applyElement( item, *pField, string( itemPathBuilder.view() ), assetID );
                }
            }
        }
    }

    void DataInheritanceProvenance::applyLayer( const XMLNode& layerRoot, const DataInheritanceSchema& schema, string_view assetID )
    {
        if ( layerRoot.isValid() )
            applyElement( layerRoot, schema, string{}, assetID );
    }

    bool DataInheritance::isWhenElement( const XMLNode& node )
    {
        return node.isElement() && string_view( node.getName() ) == kWhenElement;
    }

    bool DataInheritance::isLikelyXMLText( string_view text )
    {
        string_view body = StringUtil::trim( text );
        if ( StringUtil::startsWith( body, "\xEF\xBB\xBF" ) )
            body.remove_prefix( 3 );
        return body.empty() == false && body.front() == '<';
    }

    string DataInheritance::findBasePath( string_view text )
    {
        // 부모를 가리키지 않는 파일(거의 전부)은 글 검색 한 번으로 끝난다 — 런타임 읽기마다 지나는 자리다.
        // 첫 글자를 보는 것은 팩 쿠커가 모든 파일을 지나 보내기 때문이다 — 바이너리 · 셰이더에 우연히 든 글자로 XML 파서가 오류를 내지 않게.
        if ( text.find( kBaseAttribute ) == string_view::npos )
            return {};
        if ( isLikelyXMLText( text ) == false )
            return {};
        XMLDocument doc;
        if ( doc.parse( text ) == false )
            return {};
        return resolveBaseText( doc.getRoot().getAttributeText( kBaseAttribute ) );
    }

    string DataInheritance::resolveBaseText( string_view baseText )
    {
        if ( AssetRegistry::isReferenceText( baseText ) == false )
            return string{ baseText };
        const size_t              separator = baseText.find( DataCatalogInheritance::kFileSeparator );
        const optional<AssetGUID> guid      = AssetRegistry::parseReferenceText( baseText.substr( 0, separator ) );
        optional<string>          path      = guid.has_value() ? AssetRegistry::resolve( *guid ) : optional<string>{};
        if ( path.has_value() == false )
            return string{ baseText };
        if ( separator != string_view::npos )
            path->append( baseText.substr( separator ) );
        return std::move( *path );
    }

    string DataInheritance::makeBaseText( string_view baseID )
    {
        const size_t              separator = baseID.find( DataCatalogInheritance::kFileSeparator );
        const optional<AssetGUID> guid      = AssetRegistry::resolveGUID( baseID.substr( 0, separator ) );
        if ( guid.has_value() == false )
            return string{ baseID };
        string baseText = AssetRegistry::makeReferenceText( *guid );
        if ( separator != string_view::npos )
            baseText.append( baseID.substr( separator ) );
        return baseText;
    }

    bool DataInheritance::flatten( string_view text, string_view sourceName, const DataInheritanceSchema& schema, DataInheritanceSource& source, string& outText, DataInheritanceProvenance* pOutProvenance )
    {
        vector<string> listChain;
        return DataInheritanceInternal::flattenChain( text, sourceName, schema, source, listChain, outText, pOutProvenance );
    }

    bool DataInheritance::collectChain( string_view text, string_view sourceName, DataInheritanceSource& source, vector<DataInheritanceLayer>& outListLayer )
    {
        outListLayer.clear();
        outListLayer.push_back( DataInheritanceLayer{ string{ sourceName }, string{ text } } );
        const optional<uint32> maxDepth = source.findMaxDepth();
        if ( maxDepth.has_value() == false )
        {
            reportNoDepthLimit( sourceName );
            return false;
        }
        vector<string> listChain;
        while ( true )
        {
            const string baseID  = findBasePath( outListLayer.back()._text );
            const string childID = outListLayer.back()._id;
            if ( baseID.empty() )
                break;
            if ( VectorUtil::contains( listChain, baseID ) )
            {
                reportCycle( baseID, childID );
                return false;
            }
            if ( listChain.size() >= *maxDepth )
            {
                reportTooDeep( baseID, childID, *maxDepth );
                return false;
            }
            listChain.push_back( baseID );
            string baseText;
            if ( source.readText( baseID, baseText ) == false )
            {
                reportBaseNotFound( baseID, childID );
                return false;
            }
            outListLayer.push_back( DataInheritanceLayer{ baseID, std::move( baseText ) } );
        }
        std::reverse( outListLayer.begin(), outListLayer.end() );
        return true;
    }

    SW_DEFINE_LOCKED_REGISTRATION_LIST( DataInheritanceSchemaEntry::getEntries, const DataInheritanceSchemaEntry, "DataInheritanceSchemaRegistry", RegistrationOrder::Insertion, true )

    bool DataInheritance::makeDelta( string_view fullText, string_view parentFullText, const DataInheritanceSchema& schema, string_view baseID, string& outText )
    {
        XMLDocument fullDoc;
        XMLDocument parentDoc;
        if ( fullDoc.parse( fullText ) == false || parentDoc.parse( parentFullText ) == false )
        {
            SW_LOG_ERROR_ID( "DataInheritance.DeltaNotXML", "A delta of base '%#' could not be made - one side is not XML", baseID );
            return false;
        }
        const XMLNode fullRoot   = fullDoc.getRoot();
        const XMLNode parentRoot = parentDoc.getRoot();
        if ( fullRoot.isValid() == false || parentRoot.isValid() == false )
            return false;
        XMLDocument   deltaDoc;
        const XMLNode deltaRoot = deltaDoc.appendRoot( fullRoot.getName() );
        if ( baseID.empty() == false )
            deltaRoot.setAttribute( kBaseAttribute, string_view( makeBaseText( baseID ) ) );
        DataInheritanceInternal::writeAttributeDelta( fullRoot, parentRoot, schema, deltaRoot );
        const bool bWritten = schema._keyAttribute.empty() ? DataInheritanceInternal::writeFieldDelta( fullRoot, parentRoot, schema, deltaRoot )
                                                           : DataInheritanceInternal::writeKeyedListDelta( fullRoot, parentRoot, schema, deltaRoot );
        if ( bWritten == false )
            return false;
        outText = deltaDoc.saveToString();
        return true;
    }

    bool DataInheritance::collectChildren( const AbsolutePath& contentRoot, string_view baseID, const vector<string>& listExtension, vector<string>& outListChildID )
    {
        outListChildID.clear();
        const string normalizedBase = FileUtil::normalizePath( baseID ).str();
        for ( const string& extension : listExtension )
        {
            vector<AbsolutePath> listFile;
            if ( AssetPaths::collectContentFiles( contentRoot, extension, listFile ) == false )
                return false;
            for ( const AbsolutePath& filePath : listFile )
            {
                string text;
                if ( AssetIO::readTextFile( filePath, text ) == false )
                    return false;
                const string basePath = findBasePath( text );
                string       relativePath;
                if ( basePath.empty() == false && FileUtil::normalizePath( basePath ).str() == normalizedBase && FileUtil::tryMakePathUnderRoot( contentRoot.str(), filePath.str(), relativePath ) )
                    outListChildID.push_back( FileUtil::normalizePath( relativePath ).str() );
            }
        }
        VectorUtil::sort( outListChildID );
        return true;
    }

    void DataInheritance::reportCycle( string_view baseID, string_view sourceName )
    {
        SW_LOG_ERROR_ID( "DataInheritance.Cycle", "'%#': the base chain loops at '%#' - it is not loaded", sourceName, baseID );
    }

    void DataInheritance::reportTooDeep( string_view baseID, string_view sourceName, uint32 maxDepth )
    {
        SW_LOG_ERROR_ID( "DataInheritance.TooDeep", "'%#': the base chain is deeper than %# at '%#' - it is not loaded", sourceName, maxDepth, baseID );
    }

    void DataInheritance::reportNoDepthLimit( string_view sourceName )
    {
        SW_LOG_ERROR_ID( "DataInheritance.NoDepthLimit", "'%#': the read carries no inheritance depth limit (the engine copy is not set yet) - it is not loaded", sourceName );
    }

    void DataInheritance::reportBaseNotFound( string_view baseID, string_view sourceName )
    {
        SW_LOG_ERROR_ID( "DataInheritance.BaseNotFound", "'%#': base '%#' could not be read - it is not loaded", sourceName, baseID );
    }
} // namespace sw
