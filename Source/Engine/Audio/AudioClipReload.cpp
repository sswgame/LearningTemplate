/**
 * @file AudioClipReload.cpp
 * @brief 소리 클립(그래프) 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않는다. 부르는 곳은 개발 도구 · 에디터의 핫 리로드뿐이다).
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioClip.h"

namespace sw
{
    SW_LOG_CALLER( "AudioClipCache" );

    bool AudioClipCache::reloadClip( const hashed_string& path )
    {
        shared_ptr<const AudioClipData> pClip = decodeClip( path );
        if ( pClip == nullptr )
        {
            SW_LOG_ERROR( "Reload failed, keeping the previous sound: %#", path.c_str() );
            return false;
        }
        storeResult( path, std::move( pClip ) );
        return true;
    }
} // namespace sw
