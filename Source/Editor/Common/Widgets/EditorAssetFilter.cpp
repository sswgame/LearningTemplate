#include "pch.h"

#include "Editor/Common/Widgets/EditorAssetFilter.h"

#include "Core/File/FileUtil.h"

namespace sw::editor
{
    namespace
    {
        bool isFilterSeparator( utf8 ch )
        {
            return ch == ',' || ch == ' ' || ch == '\t' || ch == ';';
        }

        /** @brief @p filter 의 조각마다 @p func 를 부릅니다. func 가 true 를 돌려주면 멈추고 true 를 돌려줍니다. */
        template <typename Func>
        bool forEachExtension( string_view filter, Func&& func )
        {
            size_t index = 0;
            while ( index < filter.size() )
            {
                while ( index < filter.size() && isFilterSeparator( filter[index] ) )
                    ++index;
                const size_t start = index;
                while ( index < filter.size() && isFilterSeparator( filter[index] ) == false )
                    ++index;
                if ( index > start && func( filter.substr( start, index - start ) ) )
                    return true;
            }
            return false;
        }
    } // namespace

    vector<string_view> EditorAssetFilter::splitExtensions( string_view filter )
    {
        vector<string_view> listExtension;
        forEachExtension( filter, [&]( string_view extension ) {
            listExtension.push_back( extension );
            return false;
        } );
        return listExtension;
    }

    bool EditorAssetFilter::matches( string_view path, string_view filter )
    {
        bool bAnyExtension{ false };
        const bool bMatched = forEachExtension( filter, [&]( string_view extension ) {
            bAnyExtension = true;
            return FileUtil::hasExtension( path, extension );
        } );
        return bMatched || bAnyExtension == false;
    }
} // namespace sw::editor
