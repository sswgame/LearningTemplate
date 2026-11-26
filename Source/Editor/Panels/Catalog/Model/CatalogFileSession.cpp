#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogFileSession.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

namespace sw::editor
{
    CatalogFileSession::CatalogFileSession()
        : _document{}
        , _listWarning{}
        , _assetID{}
        , _kindLabel{}
        , _previousText{}
        , _status{}
        , _validationState{ ValidationState::None }
    {
    }

    void CatalogFileSession::open( string_view assetID, string_view kindLabel, string_view text, DataInheritanceSource* pInheritanceSource )
    {
        _assetID   = string( assetID );
        _kindLabel = string( kindLabel );
        _document.setInheritanceSource( pInheritanceSource );
        (void)_document.load( text );
        _previousText = string( text );
        _status.clear();
        clearValidation();
    }

    void CatalogFileSession::onSaved( string replacedText )
    {
        _previousText = std::move( replacedText );
        _document.markSaved();
    }

    void CatalogFileSession::onRestored()
    {
        _document.replaceSavedText( _previousText ); // 되돌린 글이 디스크와 문서의 새 기준이다
    }

    void CatalogFileSession::clearValidation()
    {
        _listWarning.clear();
        _validationState = ValidationState::None;
    }

    void CatalogFileSession::onValidated( bool bLoaded, vector<string>&& listWarning )
    {
        _listWarning     = std::move( listWarning );
        _validationState = bLoaded && _listWarning.empty() ? ValidationState::Passed : ValidationState::Failed;
        _status          = _document.isDirty() ? "Validated the saved file - unsaved edits are not included" : string();
    }
} // namespace sw::editor

namespace sw::editor
{
    bool CatalogFileRow::isBefore( const CatalogFileRow& lhs, const CatalogFileRow& rhs )
    {
        const int32 byKind = StringUtil::compare( lhs._kindLabel, rhs._kindLabel, true );
        return byKind != 0 ? byKind < 0 : StringUtil::compare( lhs._assetID, rhs._assetID ) < 0;
    }

    CatalogFileList::CatalogFileList()
        : _listRow{}
    {
    }

    void CatalogFileList::setRows( vector<CatalogFileRow>&& listRow )
    {
        _listRow = std::move( listRow );
        VectorUtil::sort( _listRow, &CatalogFileRow::isBefore );
    }

    uint32 CatalogFileList::collectGroups( string_view filter, vector<CatalogFileGroup>& outListGroup ) const
    {
        outListGroup.clear();
        const EditorListFilter listFilter{ filter };
        uint32                 visibleCount = 0;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _listRow.size() ); ++rowIndex )
        {
            const CatalogFileRow& row = _listRow[rowIndex];
            if ( listFilter.matchesAny( { string_view( row._assetID ), string_view( row._kindLabel ) } ) == false )
                continue;
            if ( outListGroup.empty() || outListGroup.back()._kindLabel != row._kindLabel )
            {
                outListGroup.emplace_back();
                outListGroup.back()._kindLabel = row._kindLabel;
            }
            outListGroup.back()._listRow.push_back( rowIndex );
            ++visibleCount;
        }
        return visibleCount;
    }
} // namespace sw::editor
