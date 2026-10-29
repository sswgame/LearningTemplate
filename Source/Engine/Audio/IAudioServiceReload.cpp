/**
 * @file IAudioServiceReload.cpp
 * @brief 오디오 데이터 파일(믹서 · 이벤트 라이브러리 · 소리 그래프)의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않는다. 부르는 곳은 개발 도구 · 에디터의 핫 리로드뿐이다).
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/Audio/IAudioService.h"

namespace sw
{
    SW_LOG_CALLER( "AudioService" );

    bool IAudioService::isDataFile( string_view assetPath ) const
    {
        if ( _bInitialized == false || assetPath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( assetPath ), true );
        hashed_string libraryName;
        hashed_string graphPath;
        const bool    bLibrary = _pEngine->findEventLibraryName( assetPath, libraryName );
        const bool    bGraph   = findLoadedGraphPath( assetPath, graphPath );
        return bMixer || bLibrary || bGraph;
    }

    bool IAudioService::findLoadedGraphPath( string_view assetPath, hashed_string& outGraphPath ) const
    {
        // 소리 그래프는 이벤트가 클립처럼 고르므로 클립 캐시가 읽어 둔 것만 다시 읽는다(아직 재생하지 않은 그래프는 처음 재생할 때 새로 읽힌다).
        return StringUtil::endsWith( assetPath, AudioGraphDef::kExtension, true ) && _pEngine->getClipStore().findReadyPath( assetPath, outGraphPath );
    }

    bool IAudioService::reloadDataFile( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        if ( _bInitialized == false || assetPath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( assetPath ), true );
        hashed_string graphPath;
        if ( findLoadedGraphPath( assetPath, graphPath ) )
        {
            const bool bReloaded = _pEngine->getClipStore().reloadClip( graphPath );
            if ( bReloaded )
                SW_LOG_INFO( "Audio graph reloaded (playing voices finish with the previous graph): %#", assetPath );
            return bReloaded;
        }
        hashed_string libraryName;
        if ( bMixer == false && _pEngine->findEventLibraryName( assetPath, libraryName ) == false )
            return false;
        if ( bMixer )
        {
            const bool bReloaded = loadMixer( AssetID{ _mixerPath } );
            if ( bReloaded )
                SW_LOG_INFO( "Mixer graph reloaded: %#", assetPath );
            return bReloaded;
        }
        AudioEventLibrary library;
        if ( library.loadFromAsset( assetID ) == false )
            return false;
        // 이름은 라이브러리를 처음 올린 이름 그대로여야 같은 라이브러리를 바꾼다.
        if ( _pEngine->loadEventLibrary( libraryName, library ) == false )
            return false;
        SW_LOG_INFO( "Event library reloaded: %#", assetPath );
        return true;
    }
} // namespace sw
