#include "pch.h"

#include "Editor/Common/Widgets/EditorAssetFilter.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 필터 글의 확장자 구분자(쉼표 · 공백 · 탭 · 세미콜론)입니다. */
        constexpr const utf8* kFilterSeparators = ", \t;";
    } // namespace

    vector<string_view> EditorAssetFilter::splitExtensions( string_view filter )
    {
        return StringUtil::split( filter, kFilterSeparators, { ._bDropEmpty = true } );
    }

    bool EditorAssetFilter::matches( string_view path, string_view filter )
    {
        const vector<string_view> listExtension = splitExtensions( filter );
        for ( const string_view extension : listExtension )
        {
            if ( FileUtil::hasExtension( path, extension ) )
                return true;
        }
        return listExtension.empty();
    }
} // namespace sw::editor
