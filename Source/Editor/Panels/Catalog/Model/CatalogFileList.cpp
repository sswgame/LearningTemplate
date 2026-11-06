#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogFileList.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

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
