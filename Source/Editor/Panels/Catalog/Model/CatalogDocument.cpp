#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "Core/Container/VectorUtil.h"

namespace sw::editor
{
    bool CatalogDocument::load( string_view text )
    {
        _text      = string( text );
        _savedText = _text;
        _listUndo.clear();
        _listRedo.clear();
        rebuildFromText();
        return _parsed.isParsed();
    }

    void CatalogDocument::markSaved()
    {
        _savedText = _text;
    }

    void CatalogDocument::revert()
    {
        _text = _savedText;
        _listUndo.clear();
        _listRedo.clear();
        rebuildFromText();
    }

    string CatalogDocument::getRootName() const
    {
        if ( _parsed.isParsed() == false )
            return string();
        return _parsed.getElement( _parsed.getRootIndex() )._name;
    }

    const string* CatalogDocument::findCellValue( int32 elementIndex, string_view column ) const
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
            return nullptr;
        const CatalogXMLElement& element        = _parsed.getElement( elementIndex );
        const int32              attributeIndex = element.findAttribute( column );
        return attributeIndex >= 0 ? &element._listAttribute[static_cast<size_t>( attributeIndex )]._value : nullptr;
    }

    void CatalogDocument::rebuildFromText()
    {
        _listGroup.clear();
        _error.clear();
        if ( _parsed.parse( _text, _error ) == false )
            return;
        for ( const int32 rowIndex : _parsed.getRowIndices() )
        {
            const CatalogXMLElement& row    = _parsed.getElement( rowIndex );
            Group*                   pGroup = nullptr;
            for ( Group& group : _listGroup )
            {
                if ( group._name == row._name )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                _listGroup.emplace_back();
                pGroup        = &_listGroup.back();
                pGroup->_name = row._name;
            }
            pGroup->_listRow.push_back( rowIndex );
            for ( const CatalogXMLAttribute& attribute : row._listAttribute )
            {
                if ( VectorUtil::contains( pGroup->_listColumn, attribute._name ) == false )
                    pGroup->_listColumn.push_back( attribute._name );
            }
        }
    }

    void CatalogDocument::pushUndo()
    {
        _listUndo.push_back( _text );
        if ( _listUndo.size() > kMaxUndoCount )
            _listUndo.erase( _listUndo.begin() );
        _listRedo.clear();
    }

    bool CatalogDocument::applyEdit( const string& editedText )
    {
        const string before = _text;
        _text               = editedText;
        string         error;
        CatalogXMLText probe;
        if ( probe.parse( _text, error ) == false )
        {
            _text  = before; // 원문 편집이 문서를 깨뜨리면 없던 일로 한다
            _error = error;
            return false;
        }
        _listUndo.push_back( before );
        if ( _listUndo.size() > kMaxUndoCount )
            _listUndo.erase( _listUndo.begin() );
        _listRedo.clear();
        rebuildFromText();
        return true;
    }

    bool CatalogDocument::setCell( int32 elementIndex, string_view column, string_view value )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        string edited;
        string error;
        if ( CatalogXMLText::setAttributeValue( _text, _parsed.getElement( elementIndex ), column, value, edited, error ) == false )
        {
            _error = error;
            return false;
        }
        return applyEdit( edited );
    }

    bool CatalogDocument::unsetCell( int32 elementIndex, string_view column )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        string edited;
        string error;
        if ( CatalogXMLText::removeAttribute( _text, _parsed.getElement( elementIndex ), column, edited, error ) == false )
        {
            _error = error;
            return false;
        }
        return applyEdit( edited );
    }

    bool CatalogDocument::addRow( string_view groupName )
    {
        if ( _parsed.isParsed() == false )
        {
            _error = "the document is not parsed";
            return false;
        }
        vector<CatalogXMLNewAttribute> listAttribute;
        for ( const Group& group : _listGroup )
        {
            if ( string_view( group._name ) != groupName )
                continue;
            for ( const string& column : group._listColumn )
            {
                CatalogXMLNewAttribute attribute;
                attribute._name = column;
                if ( column == "id" || column == "name" )
                    attribute._value = "new";
                listAttribute.push_back( std::move( attribute ) );
            }
        }
        if ( listAttribute.empty() )
        {
            CatalogXMLNewAttribute attribute;
            attribute._name  = "id";
            attribute._value = "new";
            listAttribute.push_back( std::move( attribute ) );
        }
        string edited;
        string error;
        if ( CatalogXMLText::appendChildElement( _text, _parsed.getElement( _parsed.getRootIndex() ), _parsed.getElements(), groupName, listAttribute, edited, error ) == false )
        {
            _error = error;
            return false;
        }
        return applyEdit( edited );
    }

    bool CatalogDocument::addChild( int32 parentIndex, string_view name, const vector<CatalogXMLNewAttribute>& listAttribute )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || parentIndex < 0 || static_cast<size_t>( parentIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        string edited;
        string error;
        if ( CatalogXMLText::appendChildElement( _text, _parsed.getElement( parentIndex ), _parsed.getElements(), name, listAttribute, edited, error ) == false )
        {
            _error = error;
            return false;
        }
        return applyEdit( edited );
    }

    bool CatalogDocument::removeRow( int32 elementIndex )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        string edited;
        string error;
        if ( CatalogXMLText::removeElement( _text, _parsed.getElement( elementIndex ), edited, error ) == false )
        {
            _error = error;
            return false;
        }
        return applyEdit( edited );
    }

    void CatalogDocument::replaceText( string_view text, bool bPushUndo )
    {
        if ( bPushUndo )
            pushUndo();
        _text = string( text );
        rebuildFromText();
    }

    void CatalogDocument::undo()
    {
        if ( _listUndo.empty() )
            return;
        _listRedo.push_back( _text );
        _text = _listUndo.back();
        _listUndo.pop_back();
        rebuildFromText();
    }

    void CatalogDocument::redo()
    {
        if ( _listRedo.empty() )
            return;
        _listUndo.push_back( _text );
        _text = _listRedo.back();
        _listRedo.pop_back();
        rebuildFromText();
    }
} // namespace sw::editor
