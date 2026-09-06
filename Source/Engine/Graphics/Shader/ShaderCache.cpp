#include "pch.h"

#include "Engine/Graphics/Shader/ShaderCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Shader/ShaderBaker.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCache" );

    namespace
    {
        struct ShaderCacheInternal
        {
            static string getStemLower( string_view filePath )
            {
                const string fileName = FileUtil::getFileNamePart( filePath );
                const string stem     = FileUtil::removeExtension( fileName );
                return StringUtil::toLower( stem.c_str() );
            }

            static string makePrebakedRelativePath( string_view filePath, string_view rhiFolder,
                                                    string_view stageTag, string_view ext )
            {
                const string norm       = FileUtil::normalizeSeparators( filePath );
                const string stem       = getStemLower( norm );
                const string fileSuffix = stem + "_" + string( stageTag ) + string( ext );

                const size_t pos = norm.find( "shaders/" );
                if ( pos != string::npos )
                {
                    const string prefix = norm.substr( 0, pos + sizeof( "shaders/" ) - 1 );
                    return prefix + "bin/" + string( rhiFolder ) + "/" + fileSuffix;
                }

                return string( "shaders/bin/" ) + string( rhiFolder ) + "/" + fileSuffix;
            }

            static string makeLocalCachePath( string_view filePath, string_view rhiFolder,
                                              string_view stageTag, string_view ext )
            {
                const string norm       = FileUtil::normalizeSeparators( filePath );
                const string stem       = getStemLower( norm );
                const string fileSuffix = stem + "_" + string( stageTag ) + string( ext );
                return FileUtil::joinPath( FileUtil::joinPath( "Saved/ShaderCache", rhiFolder ), fileSuffix );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShaderCache::ShaderCache()
        : _mapCache{}
        , _mutexCache{}
    {
    }

    ShaderCache::~ShaderCache()
    {
        shutdown();
    }

    bool ShaderCache::initialize()
    {
        clearCache();
        return true;
    }

    void ShaderCache::shutdown()
    {
        clearCache();
    }

    ShaderCompileResult ShaderCache::getOrCompile( const ShaderCompileDesc& desc )
    {
        string absPath;
        string cacheKey;
        uint64 currentTimestamp{ 0 };

        absPath = ResourceUtil::getResourcePath( desc._filePath );

        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( desc._filePath ).append( '_' ).append( desc._entryPoint ).append( '_' ).append( static_cast<int32>( desc._stage ) ).append( '_' ).append( static_cast<int32>( desc._targetFormat ) );
        for ( const auto& def : desc._listDefine )
        {
            sb.append( '_' ).append( def._name ).append( '=' ).append( def._value );
        }
        cacheKey.assign( sb.c_str(), sb.size() );

        if ( absPath.empty() == false )
            currentTimestamp = ShaderBaker::computeEffectiveSourceTimestamp( absPath );

        // 0) 인메모리 캐시 조회
        {
            std::scoped_lock<mutex> lock{ _mutexCache };
            const auto              iter = _mapCache.find( cacheKey );
            if ( iter != _mapCache.end() )
            {
                if ( iter->second._lastTimestamp == currentTimestamp && currentTimestamp != 0 )
                    return iter->second._result;
            }
        }

        const string_view rhiFolder = ShaderBaker::getSubfolderForFormat( desc._targetFormat );
        const string_view stageTag  = ShaderBaker::getStageTag( desc._stage );
        const string_view ext       = ShaderBaker::getExtensionForFormat( desc._targetFormat );

        // 1순위 (로컬 라이브 수정 캐시: Saved/ShaderCache/)
        const string localCachePath = ShaderCacheInternal::makeLocalCachePath( desc._filePath, rhiFolder, stageTag, ext );
        if ( FileUtil::fileExists( localCachePath ) )
        {
            const uint64 localMtime = FileUtil::getFileTimestamp( localCachePath );
            if ( localMtime >= currentTimestamp || currentTimestamp == 0 )
            {
                vector<uint8> cacheBytes;
                if ( FileUtil::readFile( localCachePath, cacheBytes ) && cacheBytes.empty() == false )
                {
                    SW_LOG_TRACE( "Loaded live override shader from local cache: %#", localCachePath.c_str() );
                    ShaderCompileResult result{};
                    result._bytecode = std::move( cacheBytes );
                    result._bSuccess = true;

                    std::scoped_lock<mutex> lock{ _mutexCache };
                    ShaderCacheEntry        entry{};
                    entry._lastTimestamp = currentTimestamp;
                    entry._result        = result;
                    _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
                    return result;
                }
            }
        }

        // 2순위 (Git 사전 컴파일 정식 바이너리 패스트 패스: Resource/<domain>/shaders/bin/<rhi>/ 또는 .pack)
        //
        // **소스보다 오래된 바이너리는 쓰지 않는다.** 예전엔 .dxil 이 있기만 하면 무조건 이겼다.
        // 그래서 HLSL 을 고쳐도 화면은 그대로였고(리베이크 전까지), 엔진 셰이더에 대해서는
        // 라이브 컴파일 경로가 사실상 도달 불가였다 — 그 경로를 검증할 방법도 없었던 셈이다.
        const string prebakedRelPath = ShaderCacheInternal::makePrebakedRelativePath( desc._filePath, rhiFolder, stageTag, ext );
        bool         bPrebakedUsable = true;
        if ( currentTimestamp != 0 )
        {
            const string prebakedAbsPath = ResourceUtil::getResourcePath( prebakedRelPath );
            if ( prebakedAbsPath.empty() == false && FileUtil::fileExists( prebakedAbsPath ) )
            {
                const uint64 prebakedMtime = FileUtil::getFileTimestamp( prebakedAbsPath );
                if ( prebakedMtime != 0 && prebakedMtime < currentTimestamp )
                {
                    SW_LOG_TRACE( "Pre-baked shader is older than source — recompiling: %#", prebakedRelPath.c_str() );
                    bPrebakedUsable = false;
                }
            }
        }

        vector<uint8> prebakedBytes;
        if ( bPrebakedUsable && ResourceUtil::readBinaryResource( prebakedRelPath, prebakedBytes ) && prebakedBytes.empty() == false )
        {
            SW_LOG_TRACE( "Loaded pre-baked shader binary: %# (%zu bytes)", prebakedRelPath.c_str(), prebakedBytes.size() );
            ShaderCompileResult result{};
            result._bytecode = std::move( prebakedBytes );
            result._bSuccess = true;

            std::scoped_lock<mutex> lock{ _mutexCache };
            ShaderCacheEntry        entry{};
            entry._lastTimestamp = currentTimestamp;
            entry._result        = result;
            _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
            return result;
        }

        // 3순위 (런타임 DXC 컴파일 폴백 — Dev 모드 전용)
#if !defined( SW_SHIPPING )
        BLOCK( "캐시 미스: HLSL 컴파일 및 로컬 캐시 업데이트" )
        ShaderCompileResult compiledResult = ShaderCompiler::compileHLSL( desc );
        if ( compiledResult._bSuccess )
        {
            const string localDir = FileUtil::getDirectoryPart( localCachePath );
            if ( localDir.empty() == false )
                FileUtil::ensureDirectoryExists( localDir );
            FileUtil::writeFile( localCachePath, compiledResult._bytecode.data(), compiledResult._bytecode.size() );

            std::scoped_lock<mutex> lock{ _mutexCache };
            ShaderCacheEntry        entry{};
            entry._lastTimestamp = currentTimestamp;
            entry._result        = compiledResult;
            _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
        }
        return compiledResult;
#else
        SW_LOG_ERROR( "Precompiled shader binary not found in shipping pack: '%#' [%#] (RHI: %#)",
                      desc._filePath.c_str(), desc._entryPoint.c_str(), rhiFolder.data() );
        ShaderCompileResult failedResult{};
        failedResult._bSuccess     = false;
        failedResult._errorMessage = "Shader binary missing in shipping pack";
        return failedResult;
#endif
    }

    void ShaderCache::clearCache()
    {
        std::scoped_lock<mutex> lock{ _mutexCache };
        _mapCache.clear();
    }
} // namespace sw
