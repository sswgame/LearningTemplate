#include "pch.h"

#include "Engine/Localization/XliffFile.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct XliffFileInternal
        {
            static constexpr const utf8* kVersion           = "1.2";
            static constexpr const utf8* kNamespace         = "urn:oasis:names:tc:xliff:document:1.2";
            static constexpr const utf8* kDeclaration       = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            static constexpr const utf8* kStateReview       = "needs-review-translation";
            static constexpr const utf8* kStateTranslated   = "translated";
            static constexpr string_view kStateReviewPrefix = "needs-";

            /** @brief 요소 텍스트입니다(없으면 빈 글). */
            static string readChildText( const XMLNode& parent, const utf8* pName )
            {
                const utf8* pText = parent.findChildText( pName );
                return pText != nullptr ? string( pText ) : string();
            }

            static void appendNote( const XMLNode& unit, const utf8* pFrom, string_view text )
            {
                XMLNode note = unit.appendChild( "note" );
                note.setAttribute( "from", pFrom );
                note.setValue( text );
            }

            /** @brief `<note from="…">` 하나를 항목에 넣습니다. 모르는 `from` 은 버립니다. */
            static void applyNote( string_view from, string_view text, PortableObjectEntry& inoutEntry )
            {
                if ( from == "developer" )
                    inoutEntry._listExtractedComment.push_back( string( text ) );
                else if ( from == "reference" )
                    inoutEntry._listReference.push_back( string( text ) );
                else if ( from == "previous-source" )
                    inoutEntry._previousSource = string( text );
                else if ( from == "translator" )
                    inoutEntry._translatorComment = string( text );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool XliffFile::parse( string_view text, string* pOutError )
    {
        _sourceLanguage.clear();
        _language.clear();
        _projectName.clear();
        _listEntry.clear();

        XMLDocument document;
        if ( document.parse( text, "xliff" ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = document.getLastError();
            return false;
        }
        const XMLNode root = document.getRoot( "xliff" );
        if ( root.isValid() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = "root element must be <xliff>";
            return false;
        }
        if ( root.getAttributeText( "version" ) != XliffFileInternal::kVersion )
        {
            if ( pOutError != nullptr )
                *pOutError = "only XLIFF version 1.2 is supported";
            return false;
        }
        const XMLNode file = root.findChild( "file" );
        const XMLNode body = file.isValid() ? file.findChild( "body" ) : XMLNode();
        if ( body.isValid() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = "<xliff> needs <file><body>";
            return false;
        }
        _sourceLanguage = string( file.getAttributeText( "source-language" ) );
        _language       = string( file.getAttributeText( "target-language" ) );
        _projectName    = string( file.getAttributeText( "product-name" ) );

        for ( XMLNode unit = body.findChild( "trans-unit" ); unit.isValid(); unit = unit.findNextSibling( "trans-unit" ) )
        {
            PortableObjectEntry entry;
            entry._context = string( unit.getAttributeText( "id" ) );
            if ( entry._context.empty() )
            {
                if ( pOutError != nullptr )
                    *pOutError = "a <trans-unit> has no id";
                return false;
            }
            entry._source        = XliffFileInternal::readChildText( unit, "source" );
            const XMLNode target = unit.findChild( "target" );
            if ( target.isValid() )
            {
                entry._translation = string( target.getText() );
                entry._bFuzzy      = target.getAttributeText( "state" ).substr( 0, XliffFileInternal::kStateReviewPrefix.size() ) == XliffFileInternal::kStateReviewPrefix;
            }
            for ( XMLNode note = unit.findChild( "note" ); note.isValid(); note = note.findNextSibling( "note" ) )
            {
                XliffFileInternal::applyNote( note.getAttributeText( "from" ), note.getText(), entry );
            }
            _listEntry.push_back( std::move( entry ) );
        }
        return true;
    }

    string XliffFile::toText() const
    {
        XMLDocument document;
        XMLNode     root = document.appendRoot( "xliff" );
        root.setAttribute( "version", XliffFileInternal::kVersion );
        root.setAttribute( "xmlns", XliffFileInternal::kNamespace );
        XMLNode file = root.appendChild( "file" );
        file.setAttribute( "original", string_view( _projectName ) );
        file.setAttribute( "product-name", string_view( _projectName ) );
        file.setAttribute( "source-language", string_view( _sourceLanguage ) );
        file.setAttribute( "target-language", string_view( _language ) );
        file.setAttribute( "datatype", "x-icu-message" );
        XMLNode body = file.appendChild( "body" );

        for ( const PortableObjectEntry& entry : _listEntry )
        {
            XMLNode unit = body.appendChild( "trans-unit" );
            unit.setAttribute( "id", string_view( entry._context ) );
            unit.setAttribute( "resname", string_view( entry._context ) );
            XMLNode source = unit.appendChild( "source" );
            source.setValue( string_view( entry._source ) );
            if ( entry._translation.empty() == false )
            {
                XMLNode target = unit.appendChild( "target" );
                target.setAttribute( "state", entry._bFuzzy ? XliffFileInternal::kStateReview : XliffFileInternal::kStateTranslated );
                target.setValue( string_view( entry._translation ) );
            }
            for ( const string& comment : entry._listExtractedComment )
            {
                XliffFileInternal::appendNote( unit, "developer", string_view( comment ) );
            }
            for ( const string& reference : entry._listReference )
            {
                XliffFileInternal::appendNote( unit, "reference", string_view( reference ) );
            }
            if ( entry._previousSource.empty() == false )
                XliffFileInternal::appendNote( unit, "previous-source", string_view( entry._previousSource ) );
            if ( entry._translatorComment.empty() == false )
                XliffFileInternal::appendNote( unit, "translator", string_view( entry._translatorComment ) );
        }
        return string( XliffFileInternal::kDeclaration ) + document.saveToString();
    }
} // namespace sw
