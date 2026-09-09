#include "pch.h"

#include "Editor/Common/Widgets/EditorListFilter.h"

#include "Core/String/StringUtil.h"

namespace sw::editor
{
    EditorListFilter::EditorListFilter( const utf8* pFilter )
        : EditorListFilter{ pFilter == nullptr ? string_view{} : string_view{ pFilter } }
    {
    }

    EditorListFilter::EditorListFilter( string_view filter )
        : _filter{ StringUtil::trim( filter ) }
    {
    }

    bool EditorListFilter::matches( string_view field ) const
    {
        if ( _filter.empty() )
            return true;
        if ( field.size() < _filter.size() )
            return false;

        // 길이를 먼저 맞춰 잘라 비교하므로 끝을 넘어갈 수 없다. `stristr` 은 널 종단 문자열만
        // 받는데, 여기 들어오는 필드는 종단자가 없는 조각일 수 있다.
        const size_t lastStart = field.size() - _filter.size();
        for ( size_t start = 0; start <= lastStart; ++start )
        {
            if ( StringUtil::equals( field.substr( start, _filter.size() ), _filter, true ) )
                return true;
        }
        return false;
    }

    bool EditorListFilter::matchesAny( std::initializer_list<string_view> listField ) const
    {
        if ( _filter.empty() )
            return true;

        for ( const string_view field : listField )
        {
            if ( matches( field ) )
                return true;
        }
        return false;
    }
} // namespace sw::editor
