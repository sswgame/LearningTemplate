#include "pch.h"

#include "Editor/Common/Config/EditorThemeConfig.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorTheme" );
} // namespace sw::editor

namespace sw::editor
{
    bool EditorTheme::importChangedTokenFile( const AssetPaths& /*assetPaths*/, [[maybe_unused]] string_view relativePath ) // 배포본은 로그가 없다
    {
        editor::getSubsystem<EditorTheme>().markTokensChanged();
        SW_LOG_INFO( "Theme tokens changed: %#", string{ relativePath }.c_str() );
        return true;
    }
} // namespace sw::editor
