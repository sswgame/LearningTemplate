#include "pch.h"

#include "Editor/Common/Config/EditorThemeConfig.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorTheme" );
} // namespace sw::editor

namespace sw::editor
{
    bool EditorTheme::importChangedTokenFile( [[maybe_unused]] string_view relativePath ) // 배포본은 로그가 없다
    {
        EditorTheme* const pTheme = findSubsystem<EditorTheme>(); // 필수 서비스: 없으면 버그
        if ( pTheme != nullptr )
            pTheme->markTokensChanged();
        else
            SW_LOG_ERROR( "No EditorTheme - the changed theme tokens are not applied" );
        SW_LOG_INFO( "Theme tokens changed: %#", string{ relativePath }.c_str() );
        return true;
    }
} // namespace sw::editor
