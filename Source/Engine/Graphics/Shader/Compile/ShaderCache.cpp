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
        // **경로에 유효 소스 해시를 한 칸 끼운다.** 예전엔 경로가 소스와 무관하고 "캐시 파일이 소스보다
        // 새것인가" 를 파일 시간으로 봤는데, 이 저장소는 산출물까지 커밋해서 `git pull` 이 시간 순서를
        // 임의로 뒤집는다(ShaderBaker::computeEffectiveSourceHash 주석). 경로가 다르면 낡은 것은 애초에
        // 찾히지 않으므로 시간 비교가 필요 없다 — 남는 폴더는 Saved/ 안이라 버려도 그만이다.
        //
        // **파일 이름이 아니라 폴더**에 넣는 이유는 이름이 베이커가 굽는 이름과 글자 단위로 같아야
        // 하기 때문이다(규칙이 둘이면 그중 하나가 정본을 이긴다 — `ShaderBakerTest` 가 지키는 계약).
        const string_view rhiFolder  = ShaderBaker::getSubfolderForFormat( desc._targetFormat );
        const string      absPath    = ResourceUtil::getResourcePath( desc._filePath );
        const uint64      sourceHash = absPath.empty() ? 0u : ShaderBaker::computeEffectiveSourceHash( absPath );

        StringBuilder<constant::kMaxBuffer64> sb;
        sb.appendFormat( "%#", Fmt( sourceHash, Format( 16, Format::Padding::Zero ).hex() ) );
        sb.append( desc._bDebugCodegen != SW_FALSE ? "-dbg" : "-opt" ); // 같은 소스라도 코드젠이 다르면 다른 파일이다

        const string rhiDir    = FileUtil::joinPath( "Saved/ShaderCache", rhiFolder );
        const string sourceDir = FileUtil::joinPath( rhiDir, string( sb.c_str(), sb.size() ) );
        return FileUtil::joinPath( sourceDir, ShaderCacheInternal::makeBinaryFileName( desc ) );
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
        uint64 currentSourceHash{ 0 };

        absPath = ResourceUtil::getResourcePath( desc._filePath );

        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( desc._filePath ).append( '_' ).append( desc._entryPoint ).append( '_' ).append( static_cast<int32>( desc._stage ) ).append( '_' ).append( static_cast<int32>( desc._targetFormat ) );
        sb.append( desc._bDebugCodegen != SW_FALSE ? "_dbg" : "_opt" );
        for ( const auto& def : desc._listDefine )
        {
            sb.append( '_' ).append( def._name ).append( '=' ).append( def._value );
        }
        cacheKey.assign( sb.c_str(), sb.size() );

        if ( absPath.empty() == false )
            currentSourceHash = ShaderBaker::computeEffectiveSourceHash( absPath );

        // 0) 인메모리 캐시 조회
        {
            std::scoped_lock<mutex> lock{ _mutexCache };
            const auto              iter = _mapCache.find( cacheKey );
            if ( iter != _mapCache.end() )
            {
                if ( iter->second._lastTimestamp == currentSourceHash && currentSourceHash != 0 )
                    return iter->second._result;
            }
        }

        // 1순위 (로컬 라이브 수정 캐시: Saved/ShaderCache/) — 파일 이름에 퍼뮤테이션 해시가 들어간다(헤더 주석 참고).
        const string localCachePath = makeLocalCachePath( desc );
        if ( FileUtil::fileExists( localCachePath ) )
        {
            // 경로에 이미 유효 소스 해시가 들어 있다 — 찾혔다는 것이 곧 "이 소스에서 나온 것" 이다.
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
                    entry._lastTimestamp = currentSourceHash;
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
        // **신선도는 `bake.stamp` 의 내용 해시로 본다.** 예전엔 구운 파일의 mtime 과 소스의 mtime 을
        // 비교했는데, 이 저장소는 구운 바이너리까지 커밋하므로 `git pull` 이 둘의 시간 순서를 임의로
        // 뒤집는다 — 소스가 바뀌었는데도 "바이너리가 더 새것" 이라 낡은 것을 그대로 썼다. 실제로
        // `forwardlit` 이 라이트 버퍼 이전 바이너리로 돌아, Vulkan 만 다른 그림을 내는 것을 백엔드
        // 버그로 오인했다. 검사가 개발 빌드 전용인 것은 그대로다 — 배포엔 다시 컴파일할 길이 없어
        // 거부하면 폴백이 아니라 빈 화면이고, 배포물의 신선도는 쿠킹이 같은 스탬프로 이미 막는다
        // (CookAssets.py --verify-shaders).
        if ( absPath.empty() == false )
        {
            const string prebakedAbsPath = ResourceUtil::getResourcePath( prebakedRelPath );
            if ( prebakedAbsPath.empty() == false && FileUtil::fileExists( prebakedAbsPath ) )
            {
                const string binDirAbs = FileUtil::getDirectoryPart( FileUtil::normalizeSeparators( prebakedAbsPath ) );
                if ( ShaderBaker::isBakedOutputCurrent( binDirAbs, absPath ) == false )
                {
                    SW_LOG_TRACE( "Pre-baked shader does not match the current source — recompiling: %#", prebakedRelPath.c_str() );
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
            entry._lastTimestamp = currentSourceHash;
            entry._desc          = desc;
            entry._result        = result;
            _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
            return result;
        }

        // 3순위 (런타임 DXC 컴파일 폴백 — Dev 모드 전용)
#if !defined( SW_SHIPPING )
        BLOCK( "캐시 미스: HLSL 컴파일 및 로컬 캐시 업데이트" )
        // 라이브 컴파일은 Debug 에서 디버그 코드젠으로 — RenderDoc 에서 셰이더를 한 줄씩 볼 수 있어야 한다.
        // 베이커는 이 경로를 타지 않으므로 구운 바이너리는 빌드 구성과 무관하게 늘 최적화된 것이다.
        ShaderCompileDesc liveDesc = desc;
    #if defined( SW_DEBUG )
        liveDesc._bDebugCodegen = SW_TRUE;
    #endif
        ShaderCompileResult compiledResult = ShaderCompiler::compileHlsl( liveDesc );
        if ( compiledResult._bSuccess )
        {
            const string localDir = FileUtil::getDirectoryPart( localCachePath );
            if ( localDir.empty() == false )
                FileUtil::ensureDirectoryExists( localDir );
            FileUtil::writeFile( localCachePath, compiledResult._bytecode.data(), compiledResult._bytecode.size() );

            std::scoped_lock<mutex> lock{ _mutexCache };
            ShaderCacheEntry        entry{};
            entry._lastTimestamp = currentSourceHash;
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
