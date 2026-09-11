#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCache" );

    namespace
    {
        struct ShaderCacheInternal
        {
            /** @brief 베이커가 쓰는 파일 이름 그대로 — 스템·스테이지·진입점·퍼뮤테이션 해시. 규칙은 한 곳(ShaderBaker)뿐이다. */
            static string makeBinaryFileName( const ShaderCompileDesc& desc )
            {
                const string fileName = FileUtil::getFileNamePart( desc._filePath );
                const string stem     = StringUtil::toLower( FileUtil::removeExtension( fileName ).c_str() );
                return ShaderBaker::computeBinaryFileName( stem, desc._stage, desc._entryPoint,
                                                           ShaderBaker::computePermutationHash( desc._listDefine ),
                                                           ShaderBaker::getExtensionForFormat( desc._targetFormat ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string ShaderCache::makePrebakedRelativePath( const ShaderCompileDesc& desc )
    {
        const string      norm      = FileUtil::normalizeSeparators( desc._filePath );
        const string_view rhiFolder = ShaderBaker::getSubfolderForFormat( desc._targetFormat );
        const string      fileName  = ShaderCacheInternal::makeBinaryFileName( desc );

        const size_t pos = norm.find( "shaders/" );
        if ( pos != string::npos )
        {
            const string prefix = norm.substr( 0, pos + sizeof( "shaders/" ) - 1 );
            return prefix + "bin/" + string( rhiFolder ) + "/" + fileName;
        }
        return string( "shaders/bin/" ) + string( rhiFolder ) + "/" + fileName;
    }

    string ShaderCache::makeLocalCachePath( const ShaderCompileDesc& desc )
    {
        const string_view rhiFolder = ShaderBaker::getSubfolderForFormat( desc._targetFormat );
        return FileUtil::joinPath( FileUtil::joinPath( "Saved/ShaderCache", rhiFolder ), ShaderCacheInternal::makeBinaryFileName( desc ) );
    }

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

        // 1순위 (로컬 라이브 수정 캐시: Saved/ShaderCache/) — 파일 이름에 퍼뮤테이션 해시가 들어간다(헤더 주석 참고).
        const string localCachePath = makeLocalCachePath( desc );
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
                    entry._desc          = desc;
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
        const string prebakedRelPath = makePrebakedRelativePath( desc );
        bool         bPrebakedUsable = true;
#if !defined( SW_SHIPPING )
        // **파일 시간 비교는 개발 빌드에서만 한다.** 배포 빌드에는 다시 컴파일할 길이 없어서,
        // 시간 하나가 어긋나면 폴백이 아니라 그대로 실패(화면이 빈다)다. 그런데 파일 시간은
        // 내용과 무관하게 흔들린다 — `git clone` 은 모든 파일을 체크아웃 시각으로 덮어쓰므로
        // 소스가 바이너리보다 나중에 쓰이는 순간 멀쩡한 팩이 통째로 거부된다.
        // 배포물의 신선도는 쿠킹 단계가 bake.stamp 의 **내용 해시**로 이미 보증한다
        // (CookAssets.py --verify-shaders). ShaderReflectionLibrary::tryGet 도 같은 이유로
        // 이 검사를 개발 빌드에만 걸고 있다.
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
#endif

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
            entry._desc          = desc;
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
            entry._desc          = desc;
            entry._result        = compiledResult;
            _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
        }
        return compiledResult;
#else
        // 빠진 것은 파일이 아니라 (경로 · 스테이지 · 진입점 · 퍼뮤테이션) 조합이다 — 찾던 이름(RHI 폴더 포함) 그대로 적는다.
        SW_LOG_ERROR( "Precompiled shader binary not found in shipping pack: '%#' [%#] — %#",
                      desc._filePath.c_str(), desc._entryPoint.c_str(), prebakedRelPath.c_str() );
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

    void ShaderCache::collectCompiledDescs( vector<ShaderCompileDesc>& outListDesc ) const
    {
        std::scoped_lock<mutex> lock{ _mutexCache };
        outListDesc.clear();
        outListDesc.reserve( _mapCache.size() );
        for ( const pair<const string, ShaderCacheEntry>& entry : _mapCache )
        {
            // 파일 경로가 없는 항목은 다시 컴파일할 수 없다.
            if ( entry.second._desc._filePath.empty() == false )
                outListDesc.push_back( entry.second._desc );
        }
    }
} // namespace sw
