/**
 * @file TextureBakeEntry.cpp
 * @brief 에디터 모듈의 헤드리스 텍스처 굽기 진입점(`bakeEditorTextures`)입니다. `App --bake-textures` · `--check-textures` 가 부릅니다.
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않고 `TextureBaker::bakeAllTextures` 를 바로 부릅니다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/TextureBaker.h"
#include "Editor/Common/Asset/TextureImportConfig.h"

#include "Engine/Resource/ResourceUtil.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    SW_LOG_CALLER( "TextureBakeEntry" );

    namespace
    {
        struct TextureBakeEntryInternal
        {
            static int32 bakeTextures( bool bCheckOnly )
            {
                const string        configPath = TextureBaker::makeDefaultImportConfigPath();
                TextureImportConfig config;
                if ( config.loadFromFile( configPath ) == false )
                {
                    SW_LOG_ERROR( "Cannot bake textures without the import config: %#", configPath.c_str() );
                    return -1;
                }

                const TextureBakeMode    mode    = bCheckOnly ? TextureBakeMode::CheckOnly : TextureBakeMode::BakeStale;
                const TextureBakeSummary summary = TextureBaker::bakeAllTextures( ResourceUtil::getRootFolderPath(), config, mode );
                for ( const string& problem : summary._listProblem )
                {
                    SW_LOG_ERROR( "%#", problem.c_str() );
                }

                SW_LOG_INFO( "Texture %#: %# sources, %# baked, %# problems.", bCheckOnly ? "check" : "bake", summary._sourceCount, summary._bakedCount,
                             summary._listProblem.size() );
                return static_cast<int32>( summary._listProblem.size() );
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 bakeEditorTextures( uint32 checkOnly )
{
    return sw::editor::TextureBakeEntryInternal::bakeTextures( checkOnly != 0 );
}
