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
            /** @brief 두 경로가 같은 파일인지 확인합니다. 구분자와 대소문자를 무시하고, 한쪽이 절대 경로면 뒤쪽을 맞춰 봅니다. */
            static bool isSamePath( string_view lhs, string_view rhs )
            {
                const string left  = FileUtil::normalizeSeparators( lhs );
                const string right = FileUtil::normalizeSeparators( rhs );
                if ( left.empty() || right.empty() )
                    return false;
                if ( StringUtil::equals( left, right, true ) )
                    return true;
                const string& longer  = left.size() > right.size() ? left : right;
                const string& shorter = left.size() > right.size() ? right : left;
                return StringUtil::endsWith( longer, "/" + shorter, true );
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
