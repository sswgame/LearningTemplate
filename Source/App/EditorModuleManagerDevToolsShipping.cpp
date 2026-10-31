/**
 * @file App/EditorModuleManagerDevToolsShipping.cpp
 * @brief 배포 구성의 에디터 모듈 등록 — 에디터 모듈 · 확장 모듈 · 리로드 그래프 · 개발 도구가 없습니다.
 * @details 개발은 `EditorModuleManagerDevTools.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
 */
#include "pch.h"

#include "App/EditorModuleManager.h"

namespace sw
{
    SW_LOG_CALLER( "EditorModuleManager" );

    bool EditorModuleManager::registerEditorModule( [[maybe_unused]] LiveReloadManager* pLiveReloadManager )
    {
        // 에디터를 켜 달라는 실행을 에디터 없이 조용히 시작하지 않는다(개발 구성의 "매니페스트가 에디터를 껐다" 와 같은 판정).
        if ( _bEnableEditor == SW_TRUE )
        {
            SW_LOG_ERROR( "-EnableEditor needs the editor module, which a Shipping build does not have - run it from a Dev build." );
            return false;
        }
        return true;
    }

    void EditorModuleManager::bindDevToolServices( [[maybe_unused]] SubsystemScope& editorServices ) const
    {
    }
} // namespace sw
