/**
 * @file LocalizationManagerReload.cpp
 * @brief 현지화 프로젝트 파일의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않는다. 부르는 곳은 개발 도구 · 에디터의 핫 리로드뿐이다).
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    SW_LOG_CALLER( "LocalizationManager" );

    namespace
    {
        struct LocalizationManagerInternal
        {
            /** @brief 두 리소스 id 가 같은 파일인지 확인합니다(구분자 · 대소문자 무시). */
            static bool isSamePath( string_view lhs, string_view rhs )
            {
                // 둘 다 리소스 id 다(파일 감시 · 데이터 표 편집 · 이 매니저) - 정규화한 id 가 같은지만 본다. 예전에는 꼬리 글자로도 같다고 봐(`a/x` 와 `b/a/x`)
                // 다른 폴더의 같은 이름 파일 저장이 엉뚱한 프로젝트를 다시 읽게 했다.
                return lhs.empty() == false && FileUtil::normalizePath( lhs ) == FileUtil::normalizePath( rhs );
            }
        };
    } // namespace

    ReloadOutcome LocalizationManager::reloadChangedFile( string_view changedPath )
    {
        string            projectPath;
        LocalizationScope scope{ LocalizationScope::Game };
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            for ( const MountedProject& project : _listProject )
            {
                for ( const string& filePath : project._listFilePath )
                {
                    if ( projectPath.empty() && LocalizationManagerInternal::isSamePath( filePath, changedPath ) )
                    {
                        projectPath = project._projectPath;
                        scope       = project._scope;
                    }
                }
            }
        }
        if ( projectPath.empty() )
            return ReloadOutcome::NotTracked;

        // 다시 읽다 실패하면 기존 내용을 유지한다. 저장 도중의 반쯤 쓴 파일이 화면의 텍스트를 모두 지우면 안 된다.
        MountedProject reloaded;
        reloaded._scope = scope;
        if ( readProject( AssetID{ projectPath }, reloaded ) == false )
        {
            SW_LOG_ERROR( "Localization project '%#' is not reloaded - keeping the previous text", projectPath.c_str() );
            return ReloadOutcome::Failed;
        }
        string language;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            for ( MountedProject& project : _listProject )
            {
                if ( project._projectPath == projectPath )
                    project = std::move( reloaded );
            }
            rebuildTablesLocked();
            rebuildLookupChainLocked();
            language = _currentLanguage;
        }
        SW_LOG_INFO( "Reloaded localization project '%#' (changed: %#).", projectPath.c_str(), changedPath );
        notifyLanguageChanged( language, language );
        return ReloadOutcome::Reloaded;
    }

    bool LocalizationManager::isProjectFile( string_view path ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        for ( const MountedProject& project : _listProject )
        {
            for ( const string& filePath : project._listFilePath )
            {
                if ( LocalizationManagerInternal::isSamePath( filePath, path ) )
                    return true;
            }
        }
        return false;
    }
} // namespace sw
