/**
 * @file ConfigManagerReload.cpp
 * @brief 설정 파일의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않는다. 부르는 곳은 개발 도구 · 에디터의 핫 리로드뿐이다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"

namespace sw
{
    ReloadOutcome ConfigManager::reloadConfigFile( string_view changedPath )
    {
        for ( const auto& [key, source] : _mapSource )
        {
            (void)key;
            if ( source._pReload != nullptr && FileUtil::pathsEqualNormalized( source._resolvedPath, changedPath ) )
                return source._pReload( *this, source._resolvedPath ) ? ReloadOutcome::Reloaded : ReloadOutcome::Failed;
        }
        return ReloadOutcome::NotTracked;
    }
} // namespace sw
