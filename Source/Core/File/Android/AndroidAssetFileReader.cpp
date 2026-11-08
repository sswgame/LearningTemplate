#include "pch.h"

#include "Core/File/Android/AndroidAssetFileReader.h"

#include "Core/Log/Logger.h"

#include <android/asset_manager.h>

namespace sw
{
    AndroidAssetFileReader::AndroidAssetFileReader( AAssetManager* pAssetManager )
        : _pAssetManager{ pAssetManager }
    {
        if ( _pAssetManager == nullptr )
            SW_FATAL( "AndroidAssetFileReader: the activity has no asset manager - packs in the APK cannot be read" );
    }

    AsyncFileHandle AndroidAssetFileReader::open( string_view bundledName ) const
    {
        const string name( bundledName );
        AAsset*      pAsset = AAssetManager_open( _pAssetManager, name.c_str(), AASSET_MODE_RANDOM );
        if ( pAsset == nullptr )
            return AsyncFileHandle{};
        off64_t     start{ 0 };
        off64_t     length{ 0 };
        const int32 fd = AAsset_openFileDescriptor64( pAsset, &start, &length ); // 새 fd 다 — 핸들이 주인이 되어 닫는다
        AAsset_close( pAsset );
        if ( fd < 0 )
        {
            // 압축된 파일은 구간으로 읽을 수 없다 — 통째로 풀어 메모리에 올려 대신 읽지 않는다(팩 크기만큼 메모리를 먹는다).
            SW_LOG_ERROR_ID( "AndroidAssetFileReader.AssetCompressed", "APK asset '%#' is compressed - store it uncompressed (aapt2 -0) so it can be read in place", name );
            return AsyncFileHandle{};
        }
        return AsyncFileHandle::openNativeRange( static_cast<NativeFileHandle>( fd ), static_cast<uint64>( start ), static_cast<uint64>( length ), name );
    }

    bool AndroidAssetFileReader::collectFileNames( string_view bundledFolder, vector<string>& outListFileName ) const
    {
        const string folder( bundledFolder );
        AAssetDir*   pDir = AAssetManager_openDir( _pAssetManager, folder.c_str() );
        if ( pDir == nullptr )
            return false;
        // 파일만 돌려준다(하위 폴더는 없다) — 이름은 ASCII 오름차순이다(문서). 그래서 마운트 순서가 실행마다 같다.
        // 문서는 "AAssetManager_open 에 넘길 수 있는 이름" 이라고만 한다 — 폴더 앞머리가 붙어 오면 떼어 계약(폴더를 뺀 이름)에 맞춘다.
        const string prefix = folder + "/";
        for ( const utf8* pName = AAssetDir_getNextFileName( pDir ); pName != nullptr; pName = AAssetDir_getNextFileName( pDir ) )
        {
            const string_view name{ pName };
            outListFileName.emplace_back( name.compare( 0, prefix.size(), prefix ) == 0 ? name.substr( prefix.size() ) : name );
        }
        AAssetDir_close( pDir );
        return true;
    }
} // namespace sw
