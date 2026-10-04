/**
 * @file AssetImportEntry.cpp
 * @brief 에디터 모듈의 헤드리스 에셋 임포트 진입점(`importEditorAssets`)입니다. `App --import-textures` · `--check-textures` ·
 *        `--import-models` · `--check-models` 가 부릅니다.
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않고 `TextureImporter::importAllTextures` · `ModelImporter::importAllModels` 를 바로 부릅니다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Asset/TextureImporter.h"

#include "Engine/Resource/ResourceUtil.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    SW_LOG_CALLER( "AssetImportEntry" );

    namespace
    {
        struct AssetImportEntryInternal
        {
            /** @brief 종류 하나를 대조하거나 임포트합니다. 반환은 남은 문제 수이고, 설정을 읽지 못하거나 모르는 종류면 음수입니다. */
            static int32 importAssets( EditorImportKind kind, bool bCheckOnly )
            {
                const AssetImportMode mode = bCheckOnly ? AssetImportMode::CheckOnly : AssetImportMode::ImportStale;
                AssetImportSummary    summary;
                const utf8*           pKindLabel = nullptr;
                switch ( kind )
                {
                    case EditorImportKind::Texture:
                    {
                        const string        configPath = TextureImporter::makeDefaultImportConfigPath();
                        TextureImportConfig config;
                        if ( config.loadFromFile( configPath ) == false )
                        {
                            SW_LOG_ERROR( "Cannot import textures without the import config: %#", configPath.c_str() );
                            return -1;
                        }
                        summary    = TextureImporter::importAllTextures( ResourceUtil::getRootFolderPath(), config, mode );
                        pKindLabel = "Texture";
                        break;
                    }
                    case EditorImportKind::Model:
                    {
                        const string      configPath = ModelImportConfig::makeDefaultConfigPath();
                        ModelImportConfig config;
                        if ( config.loadFromFile( configPath ) == false )
                        {
                            SW_LOG_ERROR( "Cannot import models without the import config: %#", configPath.c_str() );
                            return -1;
                        }
                        summary    = ModelImporter::importAllModels( ResourceUtil::getRootFolderPath(), config, mode );
                        pKindLabel = "Model";
                        break;
                    }
                }
                if ( pKindLabel == nullptr )
                {
                    SW_LOG_ERROR( "Unknown asset import kind %#", static_cast<uint32>( kind ) );
                    return -1;
                }

                for ( const string& problem : summary._listProblem )
                {
                    SW_LOG_ERROR( "%#", problem.c_str() );
                }
                SW_LOG_INFO( "%# %#: %# sources, %# imported, %# problems.", pKindLabel, bCheckOnly ? "check" : "import", summary._sourceCount,
                             summary._importedCount, summary._listProblem.size() );
                return static_cast<int32>( summary._listProblem.size() );
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 importEditorAssets( uint32 kind, uint32 checkOnly )
{
    return sw::editor::AssetImportEntryInternal::importAssets( static_cast<sw::EditorImportKind>( kind ), checkOnly != 0 );
}
