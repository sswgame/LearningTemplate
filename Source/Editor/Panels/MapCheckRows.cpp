#include "pch.h"

#include "Editor/Panels/MapCheckRows.h"

#include "Core/Common/StdHeaders.h"

#include "Editor/Common/Widgets/EditorListFilter.h"

namespace sw::editor
{
    namespace
    {
        struct MapCheckRowsInternal
        {
            /** @brief 오류가 먼저, 그다음 오브젝트 이름 · 타입 · 프로퍼티 · 메시지 순입니다. */
            static bool isRowBefore( const ValidationIssue& left, const ValidationIssue& right )
            {
                if ( left._severity != right._severity )
                    return left._severity == ValidationSeverity::Error;
                if ( left._sourceLabel != right._sourceLabel )
                    return left._sourceLabel < right._sourceLabel;
                if ( left._typeName.isEqual( right._typeName, NameCase::CaseSensitive ) == false )
                    return HashedStringLexicalLess{}( left._typeName, right._typeName );
                if ( left._propertyName.isEqual( right._propertyName, NameCase::CaseSensitive ) == false )
                    return HashedStringLexicalLess{}( left._propertyName, right._propertyName );
                return left._message < right._message;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MapCheckCounts MapCheckRows::countIssues( const vector<ValidationIssue>& listIssue )
    {
        MapCheckCounts counts{};
        for ( const ValidationIssue& issue : listIssue )
        {
            if ( issue._severity == ValidationSeverity::Error )
                ++counts._errorCount;
            else
                ++counts._warningCount;
        }
        return counts;
    }

    void MapCheckRows::populate( const vector<ValidationIssue>& listIssue, const MapCheckFilter& filter, vector<ValidationIssue>& outListRow, MapCheckCounts& outCounts )
    {
        outListRow.clear();
        outCounts = countIssues( listIssue );
        const EditorListFilter search{ filter._search };
        for ( const ValidationIssue& issue : listIssue )
        {
            const bool bShown = issue._severity == ValidationSeverity::Error ? filter._bShowErrors : filter._bShowWarnings;
            if ( bShown == false )
                continue;
            const bool bMatched = search.matches( issue._sourceLabel ) || search.matches( issue._typeName.c_str() ) || search.matches( issue._propertyName.c_str() ) ||
                                  search.matches( issue._message );
            if ( bMatched )
                outListRow.push_back( issue );
        }
        std::stable_sort( outListRow.begin(), outListRow.end(), &MapCheckRowsInternal::isRowBefore );
    }
} // namespace sw::editor
