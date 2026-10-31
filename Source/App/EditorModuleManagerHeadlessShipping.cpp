#include "pch.h"

#include "App/EditorModuleManager.h"

// 배포 빌드의 구현이다 — 에디터 모듈이 없어 헤드리스 임포트 · 로컬라이제이션 작업은 오류로 끝난다.
// 개발 빌드는 `EditorModuleManagerHeadless.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
namespace sw
{
    SW_LOG_CALLER( "EditorModuleManager" );

    bool EditorModuleManager::importAssetsWithEditorModule( [[maybe_unused]] EditorImportKind kind, [[maybe_unused]] bool bCheckOnly )
    {
        SW_LOG_ERROR( "Asset importing needs the editor module, which a Shipping build does not have - run it from a Dev build." );
        return false;
    }

    bool EditorModuleManager::runLocalizationWithEditorModule( [[maybe_unused]] EditorLocalizationTask task, [[maybe_unused]] string_view poPath,
                                                               [[maybe_unused]] string_view projectArgument )
    {
        SW_LOG_ERROR( "Localization tools need the editor module, which a Shipping build does not have - run them from a Dev build." );
        return false;
    }
} // namespace sw
