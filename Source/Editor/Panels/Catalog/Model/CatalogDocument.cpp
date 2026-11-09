#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogDocumentInternal
        {
            /** @brief 파일이 원소를 짚는 id 칸이면 true 입니다. 로더가 겹침을 거절하는 칸입니다. */
            static bool isIDColumn( string_view column ) { return column == "id" || column == "_id"; }

            /** @brief 사람이 지어 주는 이름 칸이면 true 입니다(id 포함). 새 행 · 복사한 행은 이 칸을 표 안에서 겹치지 않게 채웁니다. */
            static bool isNameColumn( string_view column ) { return isIDColumn( column ) || column == "name" || column == "_name"; }

            static const CatalogDocument::Group* findGroup( const vector<CatalogDocument::Group>& listGroup, string_view name )
            {
                for ( const CatalogDocument::Group& group : listGroup )
                {
                    if ( string_view( group._name ) == name )
                        return &group;
                }
                return nullptr;
            }

            /** @brief @p base 가 표의 @p column 칸에 없으면 그대로, 있으면 `<base>_2`, `<base>_3` … 입니다. */
            static string makeUniqueValue( const CatalogDocument& document, const CatalogDocument::Group& group, string_view column, string_view base )
            {
                const auto isTaken = [&document, &group, column]( string_view candidate )
                {
                    for ( const int32 rowIndex : group._listRow )
                    {
                        const string* pValue = document.findCellValue( rowIndex, column );
                        if ( pValue != nullptr && string_view( *pValue ) == candidate )
                            return true;
                    }
                    return false;
                };
                string candidate{ base };
                for ( uint64 suffix = 2; isTaken( candidate ); ++suffix )
                {
                    candidate = string{ base };
                    candidate += "_";
                    candidate += to_string( suffix );
                }
                return candidate;
            }

            /** @brief 표에서 두 행이 같이 쓰는 첫 id 입니다. 없으면 빈 글입니다. */
            static string findDuplicateID( const CatalogDocument& document, const CatalogDocument::Group& group )
            {
                for ( const string& column : group._listColumn )
                {
                    if ( isIDColumn( column ) == false )
                        continue;
                    vector<string> listValue;
                    for ( const int32 rowIndex : group._listRow )
                    {
                        const string* pValue = document.findCellValue( rowIndex, column );
                        if ( pValue != nullptr && pValue->empty() == false )
                            listValue.push_back( *pValue );
                    }
                    const string* pDuplicate = VectorUtil::findDuplicate( listValue );
                    if ( pDuplicate != nullptr )
                        return *pDuplicate;
                }
                return string();
            }
        };
    } // namespace

    CatalogDocument::CatalogDocument()
        : _text{}
        , _state{}
        , _error{}
        , _parsed{}
        , _listGroup{}
        , _bTextEditOpen{ SW_FALSE }
        , _inheritance{}
    {
    }

    bool CatalogDocument::load( string_view text )
    {
        _text = string( text );
        _state.reset( _text );
        _bTextEditOpen = SW_FALSE;
        rebuildFromText();
        return _parsed.isParsed();
    }

    void CatalogDocument::markSaved()
    {
        _state.markSaved( _text );
        _bTextEditOpen = SW_FALSE;
    }

    void CatalogDocument::revert()
    {
        if ( isDirty() == false )
            return;
        pushUndo();
        _text = _state.getSaved();
        rebuildFromText();
    }

    void CatalogDocument::replaceSavedText( string_view text )
    {
        pushUndo();
        _text = string( text );
        _state.markSaved( _text );
        rebuildFromText();
    }

    uint32 CatalogDocument::countRows() const
    {
        uint32 rowCount = 0;
        for ( const Group& group : _listGroup )
        {
            rowCount += static_cast<uint32>( group._listRow.size() );
        }
        return rowCount;
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
        const optional<uint32>   attributeIndex = element.findAttribute( column );
        return attributeIndex.has_value() ? &element._listAttribute[*attributeIndex]._value : nullptr;
    }

    const utf8* CatalogDocument::findInheritedValue( int32 elementIndex, string_view column ) const
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex || _parsed.getElement( elementIndex )._parent != _parsed.getRootIndex() )
            return nullptr;
        return _inheritance.findInherited( _parsed.getElement( elementIndex ), column );
    }

    void CatalogDocument::collectChildSections( int32 elementIndex, vector<CatalogChildSection>& outListSection ) const
    {
        outListSection.clear();
        if ( canCreateVariant( elementIndex ) )
            _inheritance.collectChildSections( _parsed, elementIndex, outListSection );
    }

    bool CatalogDocument::canCreateVariant( int32 elementIndex ) const
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex || _parsed.getElement( elementIndex )._parent != _parsed.getRootIndex() )
            return false;
        const CatalogXMLElement& row  = _parsed.getElement( elementIndex );
        const string*            pKey = _inheritance.findKeyAttribute( row._name );
        return pKey != nullptr && row.findAttribute( *pKey ).has_value();
    }

    void CatalogDocument::collectVisibleRows( const Group& group, string_view filter, vector<int32>& outListRow ) const
    {
        outListRow.clear();
        const EditorListFilter listFilter{ filter };
        if ( listFilter.isActive() == false )
        {
            outListRow = group._listRow;
            return;
        }
        for ( const int32 rowIndex : group._listRow )
        {
            for ( const CatalogXMLAttribute& attribute : _parsed.getElement( rowIndex )._listAttribute )
            {
                if ( listFilter.matches( attribute._value ) )
                {
                    outListRow.push_back( rowIndex );
                    break;
                }
            }
        }
    }

    void CatalogDocument::setError( const Error& error )
    {
        _error = error.detail().empty() ? string( error.message() ) : string( error.detail() ) + ": " + string( error.message() );
    }

    void CatalogDocument::rebuildFromText()
    {
        _listGroup.clear();
        _error.clear();
        const expected<void> parsed = _parsed.parse( _text );
        if ( parsed.has_value() == false )
            setError( parsed.error() );
        _inheritance.rebuild( _text, _parsed );
        if ( parsed.has_value() == false )
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
            _inheritance.collectInheritedColumns( row, pGroup->_listColumn ); // 물려받은 칸도 보이고 고칠 수 있어야 한다
        }
        for ( Group& group : _listGroup )
        {
            group._duplicateID = CatalogDocumentInternal::findDuplicateID( *this, group ); // 그리기마다 세면 행 수의 제곱이다 — 원문이 바뀔 때만 센다
        }
    }

    void CatalogDocument::pushUndo()
    {
        _state.record( _text );
        _bTextEditOpen = SW_FALSE;
    }

    bool CatalogDocument::applyEdit( const expected<string>& edited )
    {
        if ( edited.has_value() )
            return replaceTextChecked( string_view{ *edited } );
        setError( edited.error() );
        return false;
    }

    bool CatalogDocument::replaceTextChecked( string_view text )
    {
        CatalogXMLText       probe;
        const expected<void> parsed = probe.parse( text );
        if ( parsed.has_value() == false )
        {
            setError( parsed.error() ); // 원문 편집이 문서를 깨뜨리면 없던 일로 한다
            return false;
        }
        pushUndo();
        _text = string( text );
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
        // 부모와 같은 값은 적지 않는다 — 부모를 고치면 따라가야 할 칸이 굳는다. 열쇠와 `_base` 는 항목 자신의 칸이라 물려받지 않는다.
        const CatalogXMLElement& target     = _parsed.getElement( elementIndex );
        const string*            pKey       = _inheritance.findKeyAttribute( target._name );
        const bool               bOwnColumn = column == DataInheritance::kBaseAttribute || ( pKey != nullptr && column == string_view( *pKey ) );
        const utf8*              pInherited = bOwnColumn ? nullptr : findInheritedValue( elementIndex, column );
        if ( pInherited != nullptr && value == string_view( pInherited ) )
            return target.findAttribute( column ).has_value() ? unsetCell( elementIndex, column ) : true;
        return applyEdit( CatalogXMLText::setAttributeValue( _text, _parsed.getElement( elementIndex ), column, value ) );
    }

    bool CatalogDocument::unsetCell( int32 elementIndex, string_view column )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        return applyEdit( CatalogXMLText::removeAttribute( _text, _parsed.getElement( elementIndex ), column ) );
    }

    bool CatalogDocument::addRow( string_view groupName )
    {
        using Internal = CatalogDocumentInternal;
        if ( _parsed.isParsed() == false )
        {
            _error = "the document is not parsed";
            return false;
        }
        vector<CatalogXMLNewAttribute> listAttribute;
        const Group*                   pGroup = Internal::findGroup( _listGroup, groupName );
        if ( pGroup != nullptr )
        {
            for ( const string& column : pGroup->_listColumn )
            {
                const string* pLastValue = findCellValue( pGroup->_listRow.back(), column );
                if ( Internal::isNameColumn( column ) )
                    listAttribute.push_back( CatalogXMLNewAttribute{ column, Internal::makeUniqueValue( *this, *pGroup, column, "new" ) } );
                else if ( pLastValue != nullptr )
                    listAttribute.push_back( CatalogXMLNewAttribute{ column, *pLastValue } );
            }
        }
        if ( listAttribute.empty() )
            listAttribute.push_back( CatalogXMLNewAttribute{ "id", "new" } );
        return applyEdit( CatalogXMLText::appendChildElement( _text, _parsed.getElement( _parsed.getRootIndex() ), _parsed.getElements(), groupName, listAttribute ) );
    }

    bool CatalogDocument::addChild( int32 parentIndex, string_view name, const vector<CatalogXMLNewAttribute>& listAttribute )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || parentIndex < 0 || static_cast<size_t>( parentIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        return applyEdit( CatalogXMLText::appendChildElement( _text, _parsed.getElement( parentIndex ), _parsed.getElements(), name, listAttribute ) );
    }

    bool CatalogDocument::removeRow( int32 elementIndex )
    {
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        return applyEdit( CatalogXMLText::removeElement( _text, _parsed.getElement( elementIndex ) ) );
    }

    bool CatalogDocument::duplicateRow( int32 elementIndex )
    {
        using Internal                  = CatalogDocumentInternal;
        const bool bInvalidElementIndex = _parsed.isParsed() == false || elementIndex < 0 || static_cast<size_t>( elementIndex ) >= _parsed.getElements().size();
        if ( bInvalidElementIndex )
        {
            _error = "the document is not parsed";
            return false;
        }
        const CatalogXMLElement& original = _parsed.getElement( elementIndex );
        const Group*             pGroup   = Internal::findGroup( _listGroup, original._name );
        if ( pGroup == nullptr || original._parent != _parsed.getRootIndex() )
        {
            _error = "only a table row can be duplicated";
            return false;
        }
        expected<string> duplicated = CatalogXMLText::duplicateElement( _text, original );
        if ( duplicated.has_value() == false )
        {
            setError( duplicated.error() );
            return false;
        }
        string edited = std::move( *duplicated );
        // 복사본의 이름 칸을 하나씩 바꾼다. 바꿀 때마다 위치가 밀리므로 다시 훑는다. 복사본은 원본의 다음 형제다(원본의 인덱스는 그대로다).
        CatalogXMLText draft;
        for ( const CatalogXMLAttribute& attribute : original._listAttribute )
        {
            if ( Internal::isNameColumn( attribute._name ) == false )
                continue;
            const expected<void> draftParsed = draft.parse( edited );
            if ( draftParsed.has_value() == false )
            {
                setError( draftParsed.error() );
                return false;
            }
            const vector<int32>& listRow   = draft.getRowIndices();
            int32                copyIndex = -1;
            for ( size_t rowPosition = 0; rowPosition + 1 < listRow.size(); ++rowPosition )
            {
                if ( listRow[rowPosition] == elementIndex )
                    copyIndex = listRow[rowPosition + 1];
            }
            const string     copiedValue = Internal::makeUniqueValue( *this, *pGroup, attribute._name, attribute._value + "_copy" );
            expected<string> editedAgain = CatalogXMLText::setAttributeValue( edited, draft.getElement( copyIndex ), attribute._name, copiedValue );
            if ( editedAgain.has_value() == false )
            {
                setError( editedAgain.error() );
                return false;
            }
            edited = std::move( *editedAgain );
        }
        return replaceTextChecked( edited );
    }

    bool CatalogDocument::createVariant( int32 elementIndex )
    {
        if ( canCreateVariant( elementIndex ) == false )
        {
            _error = "only a row of a catalog listed in the inheritance rules can have a variant";
            return false;
        }
        const CatalogXMLElement& row    = _parsed.getElement( elementIndex );
        const string&            key    = *_inheritance.findKeyAttribute( row._name );
        const string&            id     = row._listAttribute[row.findAttribute( key ).value()]._value; // 변형을 만들 수 있는 행은 열쇠 속성이 있다(canCreateVariant)
        const Group*             pGroup = CatalogDocumentInternal::findGroup( _listGroup, row._name );
        if ( pGroup == nullptr )
        {
            _error = "only a table row can have a variant";
            return false;
        }
        const vector<CatalogXMLNewAttribute> listAttribute{
            CatalogXMLNewAttribute{ key, CatalogDocumentInternal::makeUniqueValue( *this, *pGroup, key, id + "_variant" ) },
            CatalogXMLNewAttribute{ DataInheritance::kBaseAttribute, id }
        };
        return addChild( _parsed.getRootIndex(), row._name, listAttribute );
    }

    void CatalogDocument::replaceText( string_view text )
    {
        if ( string_view( _text ) == text )
            return;
        if ( _bTextEditOpen == SW_FALSE )
        {
            pushUndo();
            _bTextEditOpen = SW_TRUE;
        }
        _text = string( text );
        rebuildFromText();
    }

    void CatalogDocument::undo()
    {
        if ( _state.undo( _text ) == false )
            return;
        _bTextEditOpen = SW_FALSE;
        rebuildFromText();
    }

    void CatalogDocument::redo()
    {
        if ( _state.redo( _text ) == false )
            return;
        _bTextEditOpen = SW_FALSE;
        rebuildFromText();
    }
} // namespace sw::editor
