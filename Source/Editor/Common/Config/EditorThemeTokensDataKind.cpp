#include "pch.h"

#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorThemeTokens.h"

#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw::editor
{
    /**
     * @brief 에디터 테마 토큰 파일(`editor/theme/<이름>.tokens.json`)의 데이터 종류입니다.
     * @details 스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다. 에디터만 읽는 데이터라 에디터 팩에 두고 에디터 쪽에서 등록합니다.
     */
    SW_ASSET_TYPE( EditorTheme, AssetTypeDef::makeDataFile( "editortheme", AssetPathRule{ "editor/theme/", {}, EditorThemeTokens::kExtension }, AssetTypeLoader::with<EditorThemeConfig, &EditorThemeTokens::loadThemeResource>( MemoryTag::Editor ) ) );
} // namespace sw::editor
