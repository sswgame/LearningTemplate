#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogInheritanceView.h"

#include "Core/Container/VectorUtil.h"

namespace sw::editor
{
    CatalogInheritanceView::CatalogInheritanceView()
        : _schema{ string_view{} }
        , _flatDoc{}
        , _listParentFile{}
        , _rootBase{}
        , _error{}
        , _pSource{ nullptr }
        , _bActive{ SW_FALSE }
    {
    }

    void CatalogInheritanceView::rebuild( string_view text, const CatalogXMLText& parsed )
    {
        _bActive = SW_FALSE;
        _error.clear();
        _rootBase.clear();
        _listParentFile.clear();
        _flatDoc.clear();
        if ( _pSource == nullptr || parsed.isParsed() == false )
            return;
        const CatalogXMLElement& root    = parsed.getElement( parsed.getRootIndex() );
        bool                     bListed = false;
        if ( DataCatalogInheritance::loadSchema( root._name, _schema, bListed ) == false || bListed == false )
            return;
        _bActive = SW_TRUE;
        string flatText;
        if ( DataCatalogInheritance::flatten( text, root._name, _schema, *_pSource, flatText ) == false || _flatDoc.parse( flatText ) == false )
        {
            _error = "The inheritance could not be flattened - see the log";
            return;
        }
        const int32 rootBaseIndex = root.findAttribute( DataInheritance::kBaseAttribute );
        if ( rootBaseIndex >= 0 )
        {
            _rootBase = root._listAttribute[static_cast<size_t>( rootBaseIndex )]._value;
            addParentFile( _rootBase );
        }
        for ( const int32 rowIndex : parsed.getRowIndices() )
        {
            const CatalogXMLElement& row       = parsed.getElement( rowIndex );
            const int32              baseIndex = row.findAttribute( DataInheritance::kBaseAttribute );
            if ( baseIndex < 0 )
                continue;
            const string_view base      = row._listAttribute[static_cast<size_t>( baseIndex )]._value;
            const size_t      separator = base.find( DataCatalogInheritance::kFileSeparator );
            if ( separator != string_view::npos )
                addParentFile( base.substr( 0, separator ) );
        }
    }

    void CatalogInheritanceView::addParentFile( string_view assetID )
    {
        if ( VectorUtil::containsIf( _listParentFile, [assetID]( const ParentFile& file )
        { return string_view( file._assetID ) == assetID; } ) )
            return;
        string     text;
        string     flatText;
        ParentFile file;
        if ( _pSource->readText( assetID, text ) == false || DataCatalogInheritance::flatten( text, assetID, _schema, *_pSource, flatText ) == false || file._doc.parse( flatText, assetID ) == false )
        {
            _error = "A base file could not be read: ";
            _error += assetID;
            return;
        }
        file._assetID = string{ assetID };
        _listParentFile.push_back( std::move( file ) );
    }

    const string* CatalogInheritanceView::findKeyAttribute( string_view kind ) const
    {
        const DataInheritanceSchema* pKind = _bActive == SW_TRUE ? _schema.findField( kind ) : nullptr;
        return pKind != nullptr && pKind->_keyAttribute.empty() == false ? &pKind->_keyAttribute : nullptr;
    }

    XMLNode CatalogInheritanceView::findParentEntry( const CatalogXMLElement& row ) const
    {
        const string* pKey = findKeyAttribute( row._name );
        if ( pKey == nullptr )
            return {};
        const int32 baseIndex = row.findAttribute( DataInheritance::kBaseAttribute );
        string_view filePart;
        string_view entryPart;
        if ( baseIndex >= 0 )
        {
            const string_view base      = row._listAttribute[static_cast<size_t>( baseIndex )]._value;
            const size_t      separator = base.find( DataCatalogInheritance::kFileSeparator );
            filePart                    = separator != string_view::npos ? base.substr( 0, separator ) : string_view{};
            entryPart                   = separator != string_view::npos ? base.substr( separator + 1 ) : base;
        }
        else
        {
            // 항목이 `_base` 를 적지 않았으면 파일 상속의 같은 id 가 부모다.
            const int32 keyIndex = row.findAttribute( *pKey );
            if ( _rootBase.empty() || keyIndex < 0 )
                return {};
            filePart  = _rootBase;
            entryPart = row._listAttribute[static_cast<size_t>( keyIndex )]._value;
        }
        XMLNode root;
        if ( filePart.empty() )
        {
            root = _flatDoc.getRoot();
        }
        else
        {
            const ParentFile* pFile = VectorUtil::findIf( _listParentFile, [filePart]( const ParentFile& file )
            { return string_view( file._assetID ) == filePart; } );
            root                    = pFile != nullptr ? pFile->_doc.getRoot() : XMLNode{};
        }
        if ( root.isValid() == false )
            return {};
        for ( XMLNode entry = root.findChild( row._name.c_str() ); entry.isValid(); entry = entry.findNextSibling( row._name.c_str() ) )
        {
            if ( entry.getAttributeText( pKey->c_str() ) == entryPart )
                return entry;
        }
        return {};
    }

    const utf8* CatalogInheritanceView::findInherited( const CatalogXMLElement& row, string_view column ) const
    {
        const XMLNode parent = findParentEntry( row );
        if ( parent.isValid() == false )
            return nullptr;
        const string columnName{ column };
        return parent.findAttribute( columnName.c_str() );
    }

    void CatalogInheritanceView::collectInheritedColumns( const CatalogXMLElement& row, vector<string>& inoutListColumn ) const
    {
        const XMLNode parent = findParentEntry( row );
        if ( parent.isValid() == false )
            return;
        for ( XMLAttribute attribute = parent.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const string name{ attribute.getName() };
            if ( VectorUtil::contains( inoutListColumn, name ) == false )
                inoutListColumn.push_back( name );
        }
    }
} // namespace sw::editor
