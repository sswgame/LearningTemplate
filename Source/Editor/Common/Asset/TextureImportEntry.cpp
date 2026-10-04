/**
 * @file TextureImportEntry.cpp
 * @brief 에디터 모듈의 헤드리스 텍스처 임포트 진입점(`importEditorTextures`)입니다. `App --import-textures` · `--check-textures` 가 부릅니다.
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않고 `TextureImporter::importAllTextures` 를 바로 부릅니다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Asset/TextureImporter.h"

#include "Engine/Resource/ResourceUtil.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    SW_LOG_CALLER( "TextureImportEntry" );

    namespace
    {
        struct TextureImportEntryInternal
        {
            static int32 importTextures( bool bCheckOnly )
            {
                const string        configPath = TextureImporter::makeDefaultImportConfigPath();
                TextureImportConfig config;
                if ( config.loadFromFile( configPath ) == false )
                {
                    SW_LOG_ERROR( "Cannot import textures without the import config: %#", configPath.c_str() );
                    return -1;
                }

                const TextureImportMode    mode    = bCheckOnly ? TextureImportMode::CheckOnly : TextureImportMode::ImportStale;
                const TextureImportSummary summary = TextureImporter::importAllTextures( ResourceUtil::getRootFolderPath(), config, mode );
                for ( const string& problem : summary._listProblem )
                {
                    SW_LOG_ERROR( "%#", problem.c_str() );
                }

                SW_LOG_INFO( "Texture %#: %# sources, %# imported, %# problems.", bCheckOnly ? "check" : "import", summary._sourceCount, summary._importedCount,
                             summary._listProblem.size() );
                return static_cast<int32>( summary._listProblem.size() );
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 importEditorTextures( uint32 checkOnly )
{
    return sw::editor::TextureImportEntryInternal::importTextures( checkOnly != 0 );
}
