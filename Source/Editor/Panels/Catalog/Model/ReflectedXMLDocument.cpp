#include "pch.h"

#include "Editor/Panels/Catalog/Model/ReflectedXMLDocument.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        struct ReflectedXMLDocumentInternal
        {
            /** @brief 물려받는 문서 · 항목이 부모를 적는 속성입니다(데이터 상속). */
            static constexpr string_view kBaseAttribute = "_base";

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

            static bool hasBaseAttribute( const CatalogXMLText& parsed )
            {
                for ( const CatalogXMLElement& element : parsed.getElements() )
                {
                    if ( element.findAttribute( kBaseAttribute ).has_value() )
                        return true;
                }
                return false;
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
        , _bInherited{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    expected<void> ReflectedXMLDocument::load( const TypeInfo& type, string_view text )
    {
        clear();
        _pType = &type;
        _text  = string( text );
        _state.reset( _text );
        if ( const expected<void> parsed = _parsed.parse( _text ); parsed.has_value() == false )
        {
            const Error error = parsed.error();
            clear();
            return unexpected( error );
        }
        _bInherited = ReflectedXMLDocumentInternal::hasBaseAttribute( _parsed ) ? SW_TRUE : SW_FALSE;
        if ( isInherited() )
            return {};
        _instance = readInstance( _text );
        if ( _instance.isValid() )
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
        _parsed     = CatalogXMLText{};
        _bInherited = SW_FALSE;
    }

    ReflectedInstance ReflectedXMLDocument::readInstance( string_view text ) const
    {
        ReflectedInstance fresh = ReflectedInstance::create( *_pType );
        if ( fresh.isValid() && XMLSerializer::deserialize( fresh.get(), *_pType, text ) == false )
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
        if ( isInherited() == false )
            _instance = readInstance( _text );
    }

    bool ReflectedXMLDocument::acceptText( string text, bool bReload )
    {
        CatalogXMLText parsed;
        if ( text == _text || parsed.parse( text ).has_value() == false )
            return false;
        if ( bReload )
        {
            ReflectedInstance fresh = readInstance( text );
            if ( fresh.isValid() == false )
                return false;
            _instance = std::move( fresh );
        }
        _text   = std::move( text );
        _parsed = std::move( parsed );
        return true;
    }

    const CatalogXMLElement* ReflectedXMLDocument::findItem( string_view listName, uint32 index ) const
    {
        if ( _parsed.isParsed() == false )
            return nullptr;
        for ( const int32 childIndex : _parsed.getRowIndices() )
        {
            const CatalogXMLElement& list = _parsed.getElement( childIndex );
            if ( string_view{ list._name } == listName )
                return index < list._listChild.size() ? &_parsed.getElement( list._listChild[index] ) : nullptr;
        }
        return nullptr;
    }

    bool ReflectedXMLDocument::commitItemProperty( string_view listName, uint32 index, const void* pItem, const TypeInfo& itemType, const PropertyInfo& property )
    {
        using Internal                    = ReflectedXMLDocumentInternal;
        const CatalogXMLElement* pTarget  = isEditable() ? findItem( listName, index ) : nullptr;
        const ReflectedInstance  defaults = ReflectedInstance::create( itemType );
        XMLDocument              itemDocument;
        XMLDocument              defaultDocument;
        if ( pTarget == nullptr || pItem == nullptr || defaults.isValid() == false || itemDocument.parse( XMLSerializer::serialize( pItem, itemType ) ) == false ||
             defaultDocument.parse( XMLSerializer::serialize( defaults.get(), itemType ) ) == false )
            return false;
        const utf8*   pName    = property._name.c_str();
        const XMLNode itemRoot = itemDocument.getRoot();
        const utf8*   pValue   = itemRoot.findAttribute( pName );

        // 적혀 있는 자리(속성 · 자식)를 찾는다. 적혀 있지 않고 기본값이면 적을 것이 없다.
        const CatalogXMLElement* pWrittenChild = nullptr;
        for ( const int32 childIndex : pTarget->_listChild )
        {
            const CatalogXMLElement& child = _parsed.getElement( childIndex );
            if ( string_view{ child._name } == property._name.view() )
                pWrittenChild = &child;
        }
        const bool bWritten = pWrittenChild != nullptr || pTarget->findAttribute( property._name.view() ).has_value();
        if ( bWritten == false && Internal::isSameProperty( itemRoot, defaultDocument.getRoot(), pName ) )
            return false;

        string edited;
        if ( pValue != nullptr )
        {
            const expected<string> result = CatalogXMLText::setAttributeValue( _text, *pTarget, property._name.view(), pValue );
            if ( result.has_value() == false )
                return false;
            edited = *result;
        }
        else
        {
            const XMLNode child = itemRoot.findChild( pName );
            if ( child.isValid() == false )
                return false;
            if ( pWrittenChild != nullptr )
            {
                const string childText = Internal::indentLines( child.toString(), Internal::readIndent( _text, pWrittenChild->_begin ) );
                edited                 = CatalogXMLText::replaceElement( _text, *pWrittenChild, childText );
            }
            else
            {
                string indent = Internal::readIndent( _text, pTarget->_begin );
                indent += '\t';
                edited = CatalogXMLText::appendChildText( _text, *pTarget, _parsed.getElements(), Internal::indentLines( child.toString(), indent ) );
            }
        }
        if ( acceptText( std::move( edited ), false ) == false ) // 인스턴스는 그리드가 이미 고쳤다
            return false;
        StringBuilder<constant::buffer::kMax256> label;
        label.appendFormat( "Edit %#", pName );
        notifyEdited( label.view() );
        return true;
    }

    bool ReflectedXMLDocument::duplicateItem( string_view listName, uint32 index, string_view keyAttribute )
    {
        const CatalogXMLElement* pTarget = isEditable() ? findItem( listName, index ) : nullptr;
        if ( pTarget == nullptr )
            return false;
        const optional<uint32> keyIndex = pTarget->findAttribute( keyAttribute );
        const string           key      = keyIndex.has_value() ? pTarget->_listAttribute[*keyIndex]._value : string{};
        const expected<string> copied   = CatalogXMLText::duplicateElement( _text, *pTarget );
        CatalogXMLText         parsed;
        if ( key.empty() || copied.has_value() == false || parsed.parse( *copied ).has_value() == false )
            return false;
        // 복사본(다음 형제)의 열쇠를 겹치지 않게 바꾼다 — 같은 열쇠 둘은 로더가 거절한다.
        for ( const int32 childIndex : parsed.getRowIndices() )
        {
            const CatalogXMLElement& list = parsed.getElement( childIndex );
            if ( string_view{ list._name } != listName || index + 1 >= list._listChild.size() )
                continue;
            string newKey = key;
            bool   bTaken = true;
            while ( bTaken )
            {
                newKey += "_copy";
                bTaken = false;
                for ( const int32 itemIndex : list._listChild )
                {
                    const CatalogXMLElement& item     = parsed.getElement( itemIndex );
                    const optional<uint32>   otherKey = item.findAttribute( keyAttribute );
                    bTaken                            = bTaken || ( otherKey.has_value() && item._listAttribute[*otherKey]._value == newKey );
                }
            }
            const expected<string> renamed = CatalogXMLText::setAttributeValue( *copied, parsed.getElement( list._listChild[index + 1] ), keyAttribute, newKey );
            if ( renamed.has_value() == false || acceptText( *renamed, true ) == false )
                return false;
            notifyEdited( "Duplicate Item" );
            return true;
        }
        return false;
    }

    bool ReflectedXMLDocument::removeItem( string_view listName, uint32 index )
    {
        const CatalogXMLElement* pTarget = isEditable() ? findItem( listName, index ) : nullptr;
        if ( pTarget == nullptr )
            return false;
        const expected<string> removed = CatalogXMLText::removeElement( _text, *pTarget );
        if ( removed.has_value() == false || acceptText( *removed, true ) == false )
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
