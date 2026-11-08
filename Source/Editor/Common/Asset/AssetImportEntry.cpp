/**
 * @file AssetImportEntry.cpp
 * @brief 에디터 모듈의 헤드리스 에셋 임포트 진입점(`importEditorAssets`)입니다. `App --import-textures` · `--check-textures` ·
 *        `--import-models` · `--check-models` · `--import-heightfields` · `--check-heightfields` 가 부릅니다.
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않고 `TextureImporter::importAllTextures` · `ModelImporter::importAllModels` 를 바로 부릅니다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/Model/ModelImportConfig.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Asset/Texture/HeightfieldImporter.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "ModuleAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    namespace
    {
        struct AssetImportEntryInternal
        {
            /** @brief 종류 하나를 대조하거나 임포트합니다. 반환은 남은 문제 수이고, 설정을 읽지 못하거나 모르는 종류면 음수입니다. */
            static int32 importAssets( EditorImportKind kind, bool bCheckOnly )
            {
                const AssetImportMode mode = bCheckOnly ? AssetImportMode::CheckOnly : AssetImportMode::ImportStale;
                // 경로 상태는 이 호출이 묶은 호스트 서비스에서 한 번 찾아 넘긴다 — 임포트는 워커에서 돌아 서비스를 찾지 않는다.
                const AssetPaths&  assetPaths = editor::getSubsystem<AssetPaths>();
                AssetImportSummary summary;
                const utf8*        pKindLabel = nullptr;
                switch ( kind )
                {
                    case EditorImportKind::Texture:
                    {
#if defined( SW_DEBUG )
                        if ( bCheckOnly == false )
                            SW_LOG_WARNING_ID( "AssetImportEntry.TextureImportDebugBuild", "Texture import in a Debug build is slow (unoptimized BC7/BC6H compressor, minutes per large texture) - run --import-textures with the Release App" );
#endif
                        const string        configPath = TextureImporter::makeDefaultImportConfigPath();
                        TextureImportConfig config;
                        if ( config.loadFromFile( configPath ) != ConfigReadResult::Loaded )
                        {
                            SW_LOG_ERROR_ID( "AssetImportEntry.CannotImportTexturesWithout", "Cannot import textures without the import config: %#", configPath.c_str() );
                            return -1;
                        }
                        summary    = TextureImporter::importAllTextures( assetPaths.getRootFolderPath(), config, mode );
                        pKindLabel = "Texture";
                        break;
                    }
                    case EditorImportKind::Model:
                    {
                        const string      configPath = ModelImportConfig::makeDefaultConfigPath();
                        ModelImportConfig config;
                        if ( config.loadFromFile( configPath ) != ConfigReadResult::Loaded )
                        {
                            SW_LOG_ERROR_ID( "AssetImportEntry.CannotImportModelsWithout", "Cannot import models without the import config: %#", configPath.c_str() );
                            return -1;
                        }
                        summary    = ModelImporter::importAllModels( assetPaths, assetPaths.getRootFolderPath(), config, mode );
                        pKindLabel = "Model";
                        break;
                    }
                    case EditorImportKind::Heightfield:
                    {
                        summary    = HeightfieldImporter::importAllHeightfields( assetPaths.getRootFolderPath(), mode );
                        pKindLabel = "Heightfield";
                        break;
                    }
                    case EditorImportKind::TextureVariant:
                    {
                        const string        configPath = TextureImporter::makeDefaultImportConfigPath();
                        TextureImportConfig config;
                        if ( config.loadFromFile( configPath ) != ConfigReadResult::Loaded )
                        {
                            SW_LOG_ERROR_ID( "AssetImportEntry.CannotCookVariantsWithout", "Cannot cook texture variants without the import config: %#", configPath.c_str() );
                            return -1;
                        }
                        // 모든 배포 대상의 변형을 한 번에 굽는다 — 팩은 대상마다 같고 실행이 장치에 맞는 변형을 고른다(`TextureFormatFamily::resolveCookFamilies`).
                        const TextureFormatSettings& settings = editor::getSubsystem<EngineConfig>()._textureFormats;
                        vector<TextureFormatFamily>  listFamily;
                        if ( TextureFormatFamily::resolveCookFamilies( settings, editor::getSubsystem<TypeRegistry>(), listFamily ) == false )
                            return -1; // 까닭은 표를 푸는 쪽이 남겼다
                        for ( const TextureFormatFamily& family : listFamily )
                        {
                            AssetImportSummary familySummary = TextureImporter::cookTextureVariants( assetPaths.getRootFolderPath(), config, family, mode );
                            summary._sourceCount += familySummary._sourceCount;
                            summary._importedCount += familySummary._importedCount;
                            summary._listProblem.insert( summary._listProblem.end(), familySummary._listProblem.begin(), familySummary._listProblem.end() );
                        }
                        pKindLabel = "Texture variant";
                        break;
                    }
                }
                if ( pKindLabel == nullptr )
                {
                    SW_LOG_ERROR_ID( "AssetImportEntry.UnknownAssetImportKind", "Unknown asset import kind %#", static_cast<uint32>( kind ) );
                    return -1;
                }

                for ( const string& problem : summary._listProblem )
                {
                    SW_LOG_ERROR_ID( "AssetImportEntry.ImportProblem", "%#", problem.c_str() );
                }
                SW_LOG_INFO_ID( "AssetImportEntry.SourcesImportedProblems", "%# %#: %# sources, %# imported, %# problems.", pKindLabel, bCheckOnly ? "check" : "import", summary._sourceCount,
                                summary._importedCount, summary._listProblem.size() );
                return static_cast<int32>( summary._listProblem.size() );
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 importEditorAssets( uint32 kind, uint32 checkOnly, const sw::ISubsystemLocator* pService )
{
    // 인스턴스가 없는 헤드리스 호출이라 서비스 범위를 이 호출 동안만 묶는다(작업 시스템 · 로거 같은 엔진 서비스).
    if ( pService != nullptr )
        sw::editor::bindEditorService( *pService );
    const int32 result = sw::editor::AssetImportEntryInternal::importAssets( static_cast<sw::EditorImportKind>( kind ), checkOnly != 0 );
    if ( pService != nullptr )
        sw::editor::unbindEditorService();
    return result;
}
