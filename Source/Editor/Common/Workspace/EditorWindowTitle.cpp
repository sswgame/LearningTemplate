#include "pch.h"

#include "Editor/Common/Workspace/EditorWindowTitle.h"

#include "Core/Container/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorWindowTitleInternal
        {
            /** @brief 제목 조각 사이의 구분입니다(UTF-8 em dash). */
            static constexpr string_view kSeparator = " \xE2\x80\x94 ";
            /** @brief 씬이 없을 때 씬 자리에 쓰는 글입니다. */
            static constexpr string_view kNoScene = "(no scene)";
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    string EditorWindowTitleUtil::makeTitle( string_view gameName, string_view scenePath, bool bDirty, const utf8* pPlayState )
    {
        // 경로의 마지막 조각(`/` · `\` 뒤)에서 씬 접미사를 뗀다.
        string_view  sceneName = scenePath;
        const size_t lastSlash = sceneName.find_last_of( "/\\" );
        if ( lastSlash != string_view::npos )
            sceneName = sceneName.substr( lastSlash + 1 );
        if ( StringUtil::endsWith( sceneName, kSceneSuffix, true ) )
            sceneName = sceneName.substr( 0, sceneName.size() - string_view{ kSceneSuffix }.size() );

        string title;
        title.append( gameName );
        title.append( EditorWindowTitleInternal::kSeparator );
        if ( sceneName.empty() )
            title.append( EditorWindowTitleInternal::kNoScene );
        else
            title.append( sceneName );
        if ( bDirty )
            title.append( "*" );
        title.append( EditorWindowTitleInternal::kSeparator );
        title.append( kEditorName );
        if ( StringUtil::isNullOrEmpty( pPlayState ) == false )
        {
            title.append( " (" );
            title.append( pPlayState );
            title.append( ")" );
        }
        return title;
    }

    bool EditorWindowTitleUtil::isDirtyTitle( string_view title )
    {
        // `*` 는 씬 이름 바로 뒤, 에디터 이름 앞의 구분 앞에만 붙는다.
        string marker{ "*" };
        marker.append( EditorWindowTitleInternal::kSeparator );
        marker.append( kEditorName );
        return title.find( string_view{ marker } ) != string_view::npos;
    }
} // namespace sw::editor
