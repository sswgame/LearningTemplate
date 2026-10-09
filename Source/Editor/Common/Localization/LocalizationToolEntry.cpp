/**
 * @file LocalizationToolEntry.cpp
 * @brief 에디터 모듈의 헤드리스 로컬라이제이션 진입점(`runEditorLocalizationTask`)입니다. `App --gather-text` · `--check-text` ·
 *        `--import-po=<파일>` · `--export-po` 가 부릅니다.
 * @note 모듈 전용입니다. `EditorTest` 는 이 파일을 넣지 않고 `LocalizationTools` 를 바로 부릅니다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Localization/LocalizationTools.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "RuntimeAPI/ABI/EditorAPI.h"

namespace sw::editor
{
    SW_LOG_CALLER( "LocalizationToolEntry" );

    namespace
    {
        struct LocalizationToolEntryInternal
        {
            /** @brief 작업 하나를 돌립니다. 성공하면 true 입니다(모르는 작업은 false). */
            static bool runTask( EditorLocalizationTask task, string_view poPath, string_view projectArgument )
            {
                switch ( task )
                {
                    case EditorLocalizationTask::GatherText:
                        return LocalizationTools::runGatherCommand( false, projectArgument );
                    case EditorLocalizationTask::CheckText:
                        return LocalizationTools::runGatherCommand( true, projectArgument );
                    case EditorLocalizationTask::ImportPo:
                        return LocalizationTools::runImportCommand( poPath, projectArgument );
                    case EditorLocalizationTask::ExportPo:
                        return LocalizationTools::runExportCommand( projectArgument );
                }
                SW_LOG_ERROR( "Unknown localization task %#", static_cast<uint32>( task ) );
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

extern "C" SW_MODULE_API int32 runEditorLocalizationTask( uint32 task, const utf8* pPoPath, const utf8* pProjectArgument, const sw::ModuleService* pService )
{
    // 인스턴스가 없는 헤드리스 호출이라 서비스 표를 이 호출 동안만 묶는다(`importEditorAssets` 와 같은 길).
    if ( pService != nullptr )
        sw::editor::bindEditorService( *pService );
    const bool bSucceeded = sw::editor::LocalizationToolEntryInternal::runTask( static_cast<sw::EditorLocalizationTask>( task ), pPoPath != nullptr ? pPoPath : "",
                                                                                pProjectArgument != nullptr ? pProjectArgument : "" );
    if ( pService != nullptr )
        sw::editor::unbindEditorService();
    return bSucceeded ? 0 : 1;
}
