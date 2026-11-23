#include "pch.h"

#include "Editor/Panels/Catalog/Model/ReflectedXMLDocument.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        struct ReflectedXMLDocumentInternal
        {

            /** @brief 원소 글 @p elementText 의 줄마다 앞에 @p indent 를 둡니다(첫 줄은 이미 그 자리에 선다). */
            static string indentLines( string_view elementText, string_view indent )
            {
                string result;
                for ( const utf8 letter : StringUtil::trim( elementText ) )
                {
                    if ( letter == '\r' )
                        continue;
                    result += letter;
                    if ( letter == '\n' )
                        result += indent;
                }
                return result;
            }

            /** @brief @p begin 이 있는 줄의 들여쓰기입니다. */
            static string readIndent( string_view text, uint32 begin )
            {
                size_t lineStart = begin;
                while ( lineStart > 0 && text[lineStart - 1] != '\n' )
                    --lineStart;
                size_t end = lineStart;
                while ( end < begin && ( text[end] == ' ' || text[end] == '\t' ) )
                    ++end;
                return string( text.substr( lineStart, end - lineStart ) );
            }

            static const CatalogXMLElement* findList( const CatalogXMLText& parsed, string_view listName )
            {
                if ( parsed.isParsed() == false )
                    return nullptr;
                for ( const int32 childIndex : parsed.getRowIndices() )
                {
                    if ( string_view{ parsed.getElement( childIndex )._name } == listName )
                        return &parsed.getElement( childIndex );
                }
                return nullptr;
            }

            static string_view getAttribute( const CatalogXMLElement& element, string_view name )
            {
                const optional<uint32> attributeIndex = element.findAttribute( name );
                return attributeIndex.has_value() ? string_view{ element._listAttribute[*attributeIndex]._value } : string_view{};
            }

            /** @brief 목록 @p listName 에서 열쇠 속성 @p keyAttribute 가 @p key 인 원소입니다. 없으면 nullptr 입니다. */
            static const CatalogXMLElement* findByKey( const CatalogXMLText& parsed, string_view listName, string_view keyAttribute, string_view key )
            {
                const CatalogXMLElement* pList = findList( parsed, listName );
                if ( pList == nullptr )
                    return nullptr;
                for ( const int32 itemIndex : pList->_listChild )
                {
                    const CatalogXMLElement& item = parsed.getElement( itemIndex );
                    if ( getAttribute( item, keyAttribute ) == key )
                        return &item;
                }
                return nullptr;
            }

            static const CatalogXMLElement* findChild( const CatalogXMLText& parsed, const CatalogXMLElement& element, string_view name )
            {
                for ( const int32 childIndex : element._listChild )
                {
                    if ( string_view{ parsed.getElement( childIndex )._name } == name )
                        return &parsed.getElement( childIndex );
                }
                return nullptr;
            }

            /** @brief 원문에 목록 @p listName 이 없으면 빈 목록을 루트 끝에 더합니다. 다시 훑지 못하면 false 입니다. */
            [[nodiscard]] static bool ensureList( string& inoutText, CatalogXMLText& inoutParsed, string_view listName )
            {
                if ( findList( inoutParsed, listName ) != nullptr )
                    return true;
                const expected<string> added = CatalogXMLText::appendChildElement( inoutText, inoutParsed.getElement( inoutParsed.getRootIndex() ), inoutParsed.getElements(), listName, {} );
                if ( added.has_value() == false )
                    return false;
                inoutText = *added;
                return inoutParsed.parse( inoutText ).has_value();
            }

            /** @brief 원소 @p target 의 프로퍼티 @p pName 을 @p sourceRoot 의 속성 · 자식으로 쓴 원문입니다. @p sourceRoot 에 없으면 적힌 것을 뺍니다. 자리가 없거나 실패하면 @p text 그대로입니다. */
            static string writeProperty( string_view text, const CatalogXMLText& parsed, const CatalogXMLElement& target, const XMLNode& sourceRoot, const utf8* pName )
            {
                const CatalogXMLElement* pWrittenChild = findChild( parsed, target, pName );
                const bool               bWrittenValue = target.findAttribute( pName ).has_value();
                const utf8*              pValue        = sourceRoot.findAttribute( pName );
                const XMLNode            child         = sourceRoot.findChild( pName );
                expected<string>         edited{ string( text ) };
                if ( pValue != nullptr )
                {
                    edited = CatalogXMLText::setAttributeValue( text, target, pName, pValue );
                }
                else if ( child.isValid() && pWrittenChild != nullptr )
                {
                    edited = CatalogXMLText::replaceElement( text, *pWrittenChild, indentLines( child.toString(), readIndent( text, pWrittenChild->_begin ) ) );
                }
                else if ( child.isValid() )
                {
                    string indent = readIndent( text, target._begin );
                    indent += '\t';
                    edited = CatalogXMLText::appendChildText( text, target, parsed.getElements(), indentLines( child.toString(), indent ) );
                }
                else if ( bWrittenValue )
                {
                    edited = CatalogXMLText::removeAttribute( text, target, pName );
                }
                else if ( pWrittenChild != nullptr )
                {
                    edited = CatalogXMLText::removeElement( text, *pWrittenChild );
                }
                return edited.has_value() ? *edited : string( text );
            }

            /** @brief 목록의 어느 원소와도 겹치지 않는 열쇠(`<열쇠>_copy` …)입니다. */
            static string makeUniqueKey( const CatalogXMLText& parsed, string_view listName, string_view keyAttribute, string_view key )
            {
                string newKey{ key };
                do
                {
                    newKey += "_copy";
                } while ( findByKey( parsed, listName, keyAttribute, newKey ) != nullptr );
                return newKey;
            }

            /** @brief 글이 같은 속성이거나 같은 자식이면 true 입니다(둘 다 없으면 같다). */
            static bool isSameProperty( const XMLNode& lhs, const XMLNode& rhs, const utf8* pName )
            {
                const utf8* pLeft  = lhs.findAttribute( pName );
                const utf8* pRight = rhs.findAttribute( pName );
                if ( pLeft != nullptr || pRight != nullptr )
                    return pLeft != nullptr && pRight != nullptr && StringUtil::equals( pLeft, pRight );
                const XMLNode leftChild  = lhs.findChild( pName );
                const XMLNode rightChild = rhs.findChild( pName );
                if ( leftChild.isValid() == false || rightChild.isValid() == false )
                    return leftChild.isValid() == rightChild.isValid();
                return leftChild.toString() == rightChild.toString();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ReflectedXMLDocument::ReflectedXMLDocument()
        : _pType{ nullptr }
        , _pEditRecorder{ nullptr }
        , _instance{}
        , _text{}
        , _state{}
        , _parsed{}
        , _sourceID{}
        , _baseID{}
        , _parentText{}
        , _parentParsed{}
        , _flatParsed{}
        , _flatText{}
    {
    }

    expected<void> ReflectedXMLDocument::load( const TypeInfo& type, string_view text, string_view sourceID )
    {
        clear();
        _pType    = &type;
        _text     = string( text );
        _sourceID = string( sourceID );
        _state.reset( _text );
        if ( const expected<void> parsed = _parsed.parse( _text ); parsed.has_value() == false )
        {
            const Error error = parsed.error();
            clear();
            return unexpected( error );
        }
        _baseID = DataInheritance::findBasePath( _text );
        if ( isInherited() )
        {
            string                        parentSource;
            string                        parentFlat;
            DataInheritanceResourceSource source;
            const ReflectedInstance       parent = source.readText( _baseID, parentSource ) ? readFlattened( parentSource, _baseID, parentFlat ) : ReflectedInstance{};
            _parentText                          = parent.isValid() ? XMLSerializer::serialize( parent.get(), type ) : string{};
            if ( _parentParsed.parse( _parentText ).has_value() == false )
            {
                const string baseID = _baseID;
                clear();
                return unexpected( Error{ ErrorKind::NotFound, "the parent document could not be read - see the log", baseID } );
            }
        }
        _instance = readInstance( _text, _flatText );
        if ( _instance.isValid() && ( isInherited() == false || _flatParsed.parse( _flatText ).has_value() ) )
            return {};
        clear();
        return unexpected( Error{ ErrorKind::InvalidArgument, "the document could not be read as its type - see the log", type._name.view() } );
    }

    void ReflectedXMLDocument::clear()
    {
        _pType = nullptr;
        _instance.reset();
        _text.clear();
        _state.reset( string{} );
        _parsed = CatalogXMLText{};
        _sourceID.clear();
        _baseID.clear();
        _parentText.clear();
        _parentParsed = CatalogXMLText{};
        _flatParsed   = CatalogXMLText{};
        _flatText.clear();
    }

    ReflectedInstance ReflectedXMLDocument::readInstance( string_view text, string& outFlat ) const
    {
        if ( isInherited() )
            return readFlattened( text, _sourceID, outFlat );
        outFlat.clear();
        ReflectedInstance fresh = ReflectedInstance::create( *_pType );
        if ( fresh.isValid() && XMLSerializer::deserialize( fresh.get(), *_pType, text ) == false )
            fresh.reset();
        return fresh;
    }

    ReflectedInstance ReflectedXMLDocument::readFlattened( string_view text, string_view sourceID, string& outFlat ) const
    {
        DataInheritanceResourceSource source;
        ReflectedInstance             fresh = ReflectedInstance::create( *_pType );
        if ( fresh.isValid() && ( DataInheritance::flatten( text, sourceID, DataInheritanceSchema::makeFromType( *_pType ), source, outFlat ) == false ||
                                  XMLSerializer::deserialize( fresh.get(), *_pType, outFlat ) == false ) )
            fresh.reset();
        return fresh;
    }

    void ReflectedXMLDocument::applyText( string_view text )
    {
        if ( _pType == nullptr )
            return;
        _text = string( text );
        if ( _parsed.parse( _text ).has_value() == false )
        {
            _parsed = CatalogXMLText{};
            _instance.reset();
            return;
        }
        _instance = readInstance( _text, _flatText );
        if ( isInherited() && _flatParsed.parse( _flatText ).has_value() == false )
        {
            _flatParsed = CatalogXMLText{};
            _instance.reset();
        }
    }

    bool ReflectedXMLDocument::acceptText( string text, bool bReload )
    {
        CatalogXMLText parsed;
        if ( text == _text || parsed.parse( text ).has_value() == false )
            return false;
        if ( bReload || isInherited() ) // 물려받은 문서는 새 원문이 부모 위에 펼쳐지는지까지 본다
        {
            string            flatText;
            CatalogXMLText    flatParsed;
            ReflectedInstance fresh = readInstance( text, flatText );
            if ( fresh.isValid() == false || ( isInherited() && flatParsed.parse( flatText ).has_value() == false ) )
                return false;
            if ( bReload )
                _instance = std::move( fresh );
            _flatText   = std::move( flatText );
            _flatParsed = std::move( flatParsed );
        }
        _text   = std::move( text );
        _parsed = std::move( parsed );
        return true;
    }

    const CatalogXMLElement* ReflectedXMLDocument::findItem( string_view listName, uint32 index ) const
    {
        using Internal = ReflectedXMLDocumentInternal;
        if ( isInherited() )
        {
            const string key = findItemKey( listName, index );
            return key.empty() ? nullptr : Internal::findByKey( _parsed, listName, findKeyAttribute( listName ), key );
        }
        const CatalogXMLElement* pList = Internal::findList( _parsed, listName );
        return pList != nullptr && index < pList->_listChild.size() ? &_parsed.getElement( pList->_listChild[index] ) : nullptr;
    }

    string ReflectedXMLDocument::findKeyAttribute( string_view listName ) const
    {
        const DataInheritanceSchema  schema = DataInheritanceSchema::makeFromType( *_pType );
        const DataInheritanceSchema* pField = schema.findField( listName );
        return pField != nullptr ? pField->_keyAttribute : string{};
    }

    string ReflectedXMLDocument::findItemKey( string_view listName, uint32 index ) const
    {
        const CatalogXMLElement* pList        = ReflectedXMLDocumentInternal::findList( _flatParsed, listName );
        const string             keyAttribute = pList != nullptr ? findKeyAttribute( listName ) : string{};
        if ( keyAttribute.empty() || index >= pList->_listChild.size() )
            return {};
        return string( ReflectedXMLDocumentInternal::getAttribute( _flatParsed.getElement( pList->_listChild[index] ), keyAttribute ) );
    }

    const CatalogXMLElement* ReflectedXMLDocument::findParentItem( string_view listName, string_view key ) const
    {
        return key.empty() ? nullptr : ReflectedXMLDocumentInternal::findByKey( _parentParsed, listName, findKeyAttribute( listName ), key );
    }

    bool ReflectedXMLDocument::isInheritedItem( string_view listName, uint32 index ) const
    {
        return isInherited() && findParentItem( listName, findItemKey( listName, index ) ) != nullptr;
    }

    bool ReflectedXMLDocument::commitInheritedProperty( string_view listName, string_view key, const CatalogXMLElement& parentItem, string_view itemText, const TypeInfo& itemType,
                                                        const PropertyInfo& property )
    {
        using Internal            = ReflectedXMLDocumentInternal;
        const string keyAttribute = findKeyAttribute( listName );
        const utf8*  pName        = property._name.c_str();
        if ( keyAttribute == pName )
            return false; // 열쇠를 바꾸면 다른 원소다(그리드가 감춘다 — `isInheritedItem`)
        const string parentItemText = string( string_view{ _parentText }.substr( parentItem._begin, parentItem._end - parentItem._begin ) );
        string       deltaText;
        XMLDocument  delta;
        if ( ResourceComposer::makeCheckedDelta( itemText, parentItemText, DataInheritanceSchema::makeFromType( itemType ), {}, nullptr, deltaText ).has_value() == false || delta.parse( deltaText ) == false )
            return false;
        const XMLNode deltaRoot = delta.getRoot();

        string                   text    = _text;
        CatalogXMLText           parsed  = _parsed;
        const CatalogXMLElement* pTarget = Internal::findByKey( parsed, listName, keyAttribute, key );
        if ( pTarget == nullptr )
        {
            if ( deltaRoot.findAttribute( pName ) == nullptr && deltaRoot.findChild( pName ).isValid() == false )
                return false; // 부모와 같고 적혀 있지도 않다
            if ( Internal::ensureList( text, parsed, listName ) == false )
                return false;
            const expected<string> added = CatalogXMLText::appendChildElement( text, *Internal::findList( parsed, listName ), parsed.getElements(), parentItem._name,
                                                                               {
                                                                                   CatalogXMLNewAttribute{ keyAttribute, string( key ) }
            } );
            if ( added.has_value() == false )
                return false;
            text = *added;
            if ( parsed.parse( text ).has_value() == false )
                return false;
            pTarget = Internal::findByKey( parsed, listName, keyAttribute, key );
        }
        return pTarget != nullptr && acceptText( Internal::writeProperty( text, parsed, *pTarget, deltaRoot, pName ), false );
    }

    bool ReflectedXMLDocument::commitItemProperty( string_view listName, uint32 index, const void* pItem, const TypeInfo& itemType, const PropertyInfo& property )
    {
        using Internal = ReflectedXMLDocumentInternal;
        if ( isEditable() == false || pItem == nullptr )
            return false;
        const utf8*              pName       = property._name.c_str();
        const string             itemText    = XMLSerializer::serialize( pItem, itemType );
        const string             key         = isInherited() ? findItemKey( listName, index ) : string{};
        const CatalogXMLElement* pParentItem = findParentItem( listName, key );
        const CatalogXMLElement* pTarget     = findItem( listName, index );
        const bool               bReplaces   = pTarget != nullptr && Internal::getAttribute( *pTarget, DataInheritance::kOpAttribute ) == string_view{ DataInheritance::kOpReplace };
        if ( pParentItem != nullptr && bReplaces == false )
        {
            if ( commitInheritedProperty( listName, key, *pParentItem, itemText, itemType, property ) == false )
                return false;
        }
        else
        {
            const ReflectedInstance defaults = ReflectedInstance::create( itemType );
            XMLDocument             itemDocument;
            XMLDocument             defaultDocument;
            if ( pTarget == nullptr || defaults.isValid() == false || itemDocument.parse( itemText ) == false ||
                 defaultDocument.parse( XMLSerializer::serialize( defaults.get(), itemType ) ) == false )
                return false;
            // 적혀 있지 않고 기본값이면 적을 것이 없다.
            const bool bWritten = Internal::findChild( _parsed, *pTarget, pName ) != nullptr || pTarget->findAttribute( pName ).has_value();
            if ( bWritten == false && Internal::isSameProperty( itemDocument.getRoot(), defaultDocument.getRoot(), pName ) )
                return false;
            if ( acceptText( Internal::writeProperty( _text, _parsed, *pTarget, itemDocument.getRoot(), pName ), false ) == false ) // 인스턴스는 그리드가 이미 고쳤다
                return false;
        }
        StringBuilder<constant::buffer::kMax256> label;
        label.appendFormat( "Edit %#", pName );
        notifyEdited( label.view() );
        return true;
    }

    bool ReflectedXMLDocument::duplicateItem( string_view listName, uint32 index, string_view keyAttribute )
    {
        using Internal = ReflectedXMLDocumentInternal;
        if ( isEditable() == false )
            return false;
        string         text;
        CatalogXMLText parsed;
        string         key;
        uint32         copyIndex = index + 1; // 복사본은 다음 형제다
        if ( isInherited() )
        {
            // 펼친 원소를 통째로 이 파일의 새 원소로 적고 자리만 원본 뒤로 옮긴다 — 부모에 없는 열쇠라 합쳐지지 않는다.
            key                            = findItemKey( listName, index );
            const CatalogXMLElement* pItem = Internal::findByKey( _flatParsed, listName, keyAttribute, key );
            text                           = _text;
            parsed                         = _parsed;
            if ( key.empty() || pItem == nullptr || Internal::ensureList( text, parsed, listName ) == false )
                return false;
            const CatalogXMLElement& list = *Internal::findList( parsed, listName );
            copyIndex                     = static_cast<uint32>( list._listChild.size() ); // 이 파일의 목록 끝
            string indent                 = Internal::readIndent( text, list._begin );
            indent += '\t';
            text                   = CatalogXMLText::appendChildText( text, list, parsed.getElements(), Internal::indentLines( string_view{ _flatText }.substr( pItem->_begin, pItem->_end - pItem->_begin ), indent ) );
            const bool bCopyParsed = parsed.parse( text ).has_value();
            if ( bCopyParsed == false )
                return false;
            const CatalogXMLElement* pCopied = &parsed.getElement( Internal::findList( parsed, listName )->_listChild[copyIndex] );
            const expected<string>   placed  = CatalogXMLText::setAttributeValue( text, *pCopied, DataInheritance::kAfterAttribute, key );
            if ( placed.has_value() == false )
                return false;
            text = *placed;
        }
        else
        {
            const CatalogXMLElement* pTarget = findItem( listName, index );
            const expected<string>   copied  = pTarget != nullptr ? CatalogXMLText::duplicateElement( _text, *pTarget ) : expected<string>{ unexpected( Error{ ErrorKind::NotFound, "no such item" } ) };
            if ( copied.has_value() == false )
                return false;
            key  = string( Internal::getAttribute( *pTarget, keyAttribute ) );
            text = *copied;
        }
        // 복사본의 열쇠를 겹치지 않게 바꾼다 — 같은 열쇠 둘은 로더가 거절한다.
        if ( key.empty() || parsed.parse( text ).has_value() == false )
            return false;
        const CatalogXMLElement* pList = Internal::findList( parsed, listName );
        if ( pList == nullptr || copyIndex >= pList->_listChild.size() )
            return false;
        const string           newKey  = Internal::makeUniqueKey( isInherited() ? _flatParsed : parsed, listName, keyAttribute, key );
        const expected<string> renamed = CatalogXMLText::setAttributeValue( text, parsed.getElement( pList->_listChild[copyIndex] ), keyAttribute, newKey );
        if ( renamed.has_value() == false || acceptText( *renamed, true ) == false )
            return false;
        notifyEdited( "Duplicate Item" );
        return true;
    }

    bool ReflectedXMLDocument::removeItem( string_view listName, uint32 index )
    {
        using Internal = ReflectedXMLDocumentInternal;
        if ( isEditable() == false )
            return false;
        const CatalogXMLElement* pTarget     = findItem( listName, index );
        const string             key         = isInherited() ? findItemKey( listName, index ) : string{};
        const CatalogXMLElement* pParentItem = findParentItem( listName, key );
        string                   text        = _text;
        CatalogXMLText           parsed      = _parsed;
        if ( pTarget != nullptr )
        {
            const expected<string> removed = CatalogXMLText::removeElement( text, *pTarget );
            if ( removed.has_value() == false )
                return false;
            text = *removed;
            if ( parsed.parse( text ).has_value() == false )
                return false;
        }
        else if ( pParentItem == nullptr )
        {
            return false;
        }
        if ( pParentItem != nullptr ) // 부모의 원소는 지운다고 적는다
        {
            if ( Internal::ensureList( text, parsed, listName ) == false )
                return false;
            const expected<string> added = CatalogXMLText::appendChildElement( text, *Internal::findList( parsed, listName ), parsed.getElements(), pParentItem->_name,
                                                                               {
                                                                                   CatalogXMLNewAttribute{ findKeyAttribute( listName ),                        key},
                                                                                   CatalogXMLNewAttribute{DataInheritance::kOpAttribute, DataInheritance::kOpRemove}
            } );
            if ( added.has_value() == false )
                return false;
            text = *added;
        }
        if ( acceptText( std::move( text ), true ) == false )
            return false;
        notifyEdited( "Remove Item" );
        return true;
    }

    void ReflectedXMLDocument::notifyEdited( string_view label ) const
    {
        if ( _pEditRecorder != nullptr )
            _pEditRecorder->notifyEdited( label );
    }
} // namespace sw::editor
