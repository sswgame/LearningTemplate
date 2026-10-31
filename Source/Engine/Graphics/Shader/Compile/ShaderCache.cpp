#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCache" );

    /**
     * @brief `-gv_shaderDebugCodegen=1`: 실행 중 컴파일(소스 정책)을 디버그 코드젠으로 합니다 — RenderDoc 에서 셰이더를 한 줄씩 볼 때 켠다. 기본은 끔이다.
     * @details 기본(최적화 코드젠)은 쿠킹과 같은 바이트라, 그림이 셰이더를 어디서 얻었느냐(캐시 · 컴파일 · 쿠킹본)나 빌드 구성에 달리지 않는다 — 골든 비교가 그것에 기댄다.
     *          예전에는 Debug 구성이면 늘 디버그 코드젠이라 캐시가 빗나간 Debug 실행만 다른 그림을 냈다. 언리얼도 기본은 최적화이고 `r.Shaders.Optimize=0` 으로 켠다.
     *          엔진 기동 때 컴파일한 셰이더도 이 값을 따르므로 명령줄로 준다(실행 중에 바꾸면 이미 컴파일한 것은 그대로다).
     *          디버깅 스위치라 배포 구성에는 등록하지 않는다(`SW_TEST_GLOBAL_VARIABLE`).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_shaderDebugCodegen, false, "실행 중 셰이더 컴파일을 디버그 코드젠으로(RenderDoc 에서 한 줄씩) — 기본은 쿠킹과 같은 최적화 코드젠" );

    namespace
    {
        /** @brief 쿠커가 쓰는 파일 이름 그대로입니다(스템, 스테이지, 진입점, 퍼뮤테이션 해시). 규칙은 한 곳(ShaderCooker)뿐입니다. */
        string makeBinaryFileName( const ShaderCompileDesc& desc )
        {
            return ShaderCooker::computeBinaryFileName( desc._source.getStemLower(), desc._stage, desc._entryPoint,
                                                        ShaderCooker::computePermutationHash( desc._listDefine ),
                                                        ShaderCooker::getExtensionForFormat( desc._targetFormat ) );
        }

        /** @brief 소스의 절대 경로입니다(지문용 — 컴파일러와 같은 해석기 `ShaderSourceResolver`). 못 풀면 빈 글이며 지문이 0 이라 맞지 않는다. */
        string findSourcePath( const ShaderSourceID& source )
        {
            const std::optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
            string                                    error;
            const std::optional<ShaderCompileInput>   input = resolver.has_value() ? resolver->resolve( source, error ) : std::nullopt;
            return input.has_value() ? input->_source.str() : string{};
        }

        /** @brief 디스크 파일이 있고 비어 있지 않으면 그 바이트를 읽어 true 입니다(로컬 라이브 수정 캐시). */
        [[nodiscard]] bool readNonEmptyFile( const string& path, vector<uint8>& outBytes )
        {
            return FileUtil::exists( path ) && FileUtil::readFile( path, outBytes ) && outBytes.empty() == false;
        }
    } // namespace
} // namespace sw

namespace sw
{
    string ShaderCache::makeLocalCachePath( const ShaderCompileDesc& desc )
    {
        // **경로에 소스 지문을 한 단계 끼운다.** "캐시 파일이 소스보다 새것인가"를 파일 시간으로 보면 안 된다 —
        // 이 저장소는 산출물까지 커밋해서 `git pull` 이 시간 순서를 임의로 뒤집는다(ShaderCooker::computeSourceFingerprint
        // 주석). 경로가 다르면 낡은 것은 애초에
        // 찾히지 않으므로 시간 비교가 필요 없다. 남는 폴더는 Saved/ 안이라 버려도 그만이다.
        // 지문은 소스만 본다. 이 셰이더가 연 헤더는 컴파일해 봐야 알아서 경로에 넣을 수 없고, 바이트코드 옆의 의존 기록이 헤더의 변경을 맡는다(readLocalCache).
        //
        // **파일 이름이 아니라 폴더**에 넣는 이유는 이름이 쿠커가 쿠킹하는 이름과 글자 단위로 같아야
        // 하기 때문이다(규칙이 둘이면 그중 하나가 기준을 이긴다. `ShaderCookerTest` 가 지키는 계약이다).
        const string_view rhiFolder  = ShaderCooker::getSubfolderForFormat( desc._targetFormat );
        const string      sourcePath = findSourcePath( desc._source );
        const uint64      sourceHash = sourcePath.empty() == false ? ShaderCooker::computeSourceFingerprint( sourcePath, vector<string>{} ) : 0u;

        StringBuilder<constant::kMaxBuffer64> sb;
        sb.appendFormat( "%#", Fmt( sourceHash, Format( 16, Format::Padding::Zero ).hex() ) );
        sb.append( desc._bDebugCodegen != SW_FALSE ? "-dbg" : "-opt" ); // 같은 소스라도 코드젠이 다르면 다른 파일이다

        // 작업 폴더가 아니라 실행 파일 폴더(`Bin`) 기준이다 — 병렬 실행(ctest -j · 시나리오 행렬)도 같은 `Bin` 을 쓰고, 이 캐시는 같은 빌드의 실행끼리 나누는 것이 목적이다(쓰기는 원자적 바꿔치기라 겹쳐도 깨지지 않는다).
        const string rhiDir    = FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._saved.str(), "ShaderCache" ), rhiFolder );
        const string sourceDir = FileUtil::joinPath( rhiDir, string( sb.c_str(), sb.size() ) );
        return FileUtil::joinPath( sourceDir, makeBinaryFileName( desc ) );
    }

    bool ShaderCache::readLocalCache( const ShaderCompileDesc& liveDesc, ShaderCompileResult& outResult )
    {
        const string  localCachePath = makeLocalCachePath( liveDesc );
        vector<uint8> cacheBytes;
        if ( readNonEmptyFile( localCachePath, cacheBytes ) == false )
            return false;
        // 의존 기록이 없거나 연 헤더 중 하나라도 달라졌으면 이 바이트코드는 낡았다 — 경로(소스 지문)는 헤더를 보지 않는다.
        vector<string> listIncludedFile;
        if ( ShaderCooker::readIncludeRecord( localCachePath + ShaderCooker::kIncludeRecordSuffix, listIncludedFile ) == false )
            return false;
        outResult._bytecode         = std::move( cacheBytes );
        outResult._listIncludedFile = std::move( listIncludedFile );
        outResult._bSuccess         = true;
        return true;
    }

    bool ShaderCache::storeLocalCache( const ShaderCompileDesc& liveDesc, const ShaderCompileResult& result )
    {
        const string localCachePath = makeLocalCachePath( liveDesc );

        // **바이트가 같으면 바이트 파일은 쓰지 않는다.** 쓰면 호출자가 "갱신" 으로 세어 재컴파일 콜백이 불리고 바뀌지 않은 셰이더의 PSO 까지 다시 만든다.
        // 의존 기록은 그래도 새로 쓴다 — 헤더만 고쳐 바이트가 그대로면 옛 기록이 남아 이 캐시가 영영 낡은 것으로 보인다.
        vector<uint8> existingBytes;
        const bool    bSameBytes = FileUtil::exists( localCachePath ) && FileUtil::readFile( localCachePath, existingBytes ) && existingBytes == result._bytecode;
        if ( bSameBytes == false )
        {
            if ( FileUtil::ensureParentDirectoryExists( localCachePath ) == false || FileUtil::writeFile( localCachePath, result._bytecode.data(), result._bytecode.size() ) == false )
            {
                SW_LOG_WARNING( "Could not write the shader cache '%#' - the next run recompiles it", localCachePath );
                return true;
            }
        }
        if ( ShaderCooker::writeIncludeRecord( localCachePath + ShaderCooker::kIncludeRecordSuffix, result._listIncludedFile ) == false )
            SW_LOG_WARNING( "Could not write the shader cache record '%#' - the next run recompiles it", localCachePath );
        return bSameBytes == false;
    }

    ShaderCompileDesc ShaderCache::makeLiveCompileDesc( const ShaderCompileDesc& desc )
    {
        ShaderCompileDesc liveDesc = desc;
        if ( gv_shaderDebugCodegen )
            liveDesc._bDebugCodegen = SW_TRUE;
        return liveDesc;
    }

    ShaderLoadBlock::ShaderLoadBlock( ShaderCache* pCache, const ShaderCompileDesc& desc, string cacheKey )
        : AssetLoadBlock{ "Shader", desc._source.str() }
        , _pCache{ pCache }
        , _desc{ desc }
        , _cacheKey{ std::move( cacheKey ) }
        , _pResult{}
    {
    }

    bool ShaderLoadBlock::runLoad( string& outError )
    {
        shared_ptr<ShaderCompileResult> pResult = sw::make_shared<ShaderCompileResult>( _pCache->loadOrCompile( _desc, _cacheKey ) );
        const bool                      bLoaded = pResult->_bSuccess;
        if ( bLoaded == false )
            outError = pResult->_errorMessage.empty() ? string( "the shader could not be compiled or found" ) : pResult->_errorMessage;
        _pResult = std::move( pResult );
        {
            // 이 블록의 칸일 때만 뺀다(캐시를 비운 뒤 같은 요청이 새 블록을 넣었을 수 있다).
            ScopedLock<mutex> lock{ _pCache->_mutexCache };
            const auto        it = _pCache->_mapLoading.find( _cacheKey );
            if ( it != _pCache->_mapLoading.end() && ( it->second.expired() || it->second.lock().get() == this ) )
                _pCache->_mapLoading.erase( it );
        }
        return bLoaded;
    }

    ShaderCache::ShaderCache()
        : _reflectionManifests{}
        , _mapCache{}
        , _mapLoading{}
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
        waitForLoadsInFlight();
        clearCache();
        // 리플렉션 매니페스트도 셰이더 층이 종료될 때 같이 비운다(백엔드 교체로 다시 서면 새 백엔드의 매니페스트를 읽는다). 소멸자에서도 불리므로 서비스 칸을 거치지 않는다.
        _reflectionManifests.clear();
    }

    string ShaderCache::makeCacheKey( const ShaderCompileDesc& desc )
    {
        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( desc._source.str() ).append( '_' ).append( desc._entryPoint ).append( '_' ).append( static_cast<int32>( desc._stage ) ).append( '_' ).append( static_cast<int32>( desc._targetFormat ) );
        sb.append( desc._bDebugCodegen != SW_FALSE ? "_dbg" : "_opt" );
        for ( const auto& def : desc._listDefine )
        {
            sb.append( '_' ).append( def._name ).append( '=' ).append( def._value );
        }
        return string( sb.c_str(), sb.size() );
    }

    ShaderHandle ShaderCache::request( const ShaderCompileDesc& desc )
    {
        return requestWith( desc, AssetLoadStart::Async );
    }

    ShaderHandle ShaderCache::requestWith( const ShaderCompileDesc& desc, AssetLoadStart start )
    {
        string                      cacheKey = makeCacheKey( desc );
        shared_ptr<ShaderLoadBlock> pBlock;
        {
            ScopedLock<mutex> lock{ _mutexCache };
            const auto        it = _mapLoading.find( cacheKey );
            if ( it != _mapLoading.end() )
                pBlock = it->second.lock();
            if ( pBlock != nullptr )
                return ShaderHandle{ std::move( pBlock ) };
            // 인메모리 적중도 로드 단계가 본다 — 적중인지는 소스 해시를 구해야 알고, 그 계산(파일 읽기)은 잠금 밖에서 한다.
            pBlock                = sw::make_shared<ShaderLoadBlock>( this, desc, cacheKey );
            _mapLoading[cacheKey] = pBlock;
        }
        AssetLoadBlock::startLoad( pBlock, start );
        return ShaderHandle{ std::move( pBlock ) };
    }

    ShaderCompileResult ShaderCache::getOrCompile( const ShaderCompileDesc& desc )
    {
        const ShaderHandle handle = requestWith( desc, AssetLoadStart::OnCallingThread );
        handle.getBlock()->wait();
        // 실패도 결과를 돌려준다 — 부르는 쪽이 컴파일 오류 문구를 드러낸다.
        const shared_ptr<const ShaderCompileResult>& pResult = handle.getBlock()->getResult();
        if ( pResult != nullptr )
            return *pResult;
        ShaderCompileResult failedResult{};
        failedResult._bSuccess     = false;
        failedResult._errorMessage = handle.getError();
        return failedResult;
    }

    void ShaderCache::waitForLoadsInFlight()
    {
        vector<shared_ptr<ShaderLoadBlock>> listLoading;
        {
            ScopedLock<mutex> lock{ _mutexCache };
            for ( const auto& [key, weakBlock] : _mapLoading )
            {
                shared_ptr<ShaderLoadBlock> pBlock = weakBlock.lock();
                if ( pBlock != nullptr )
                    listLoading.push_back( std::move( pBlock ) );
            }
        }
        for ( const shared_ptr<ShaderLoadBlock>& pBlock : listLoading )
        {
            pBlock->wait();
        }
    }

    ShaderCompileResult ShaderCache::loadOrCompile( const ShaderCompileDesc& desc, const string& inCacheKey )
    {
        SW_MEMORY_SCOPE( Shader );
        string       cacheKey = inCacheKey;
        const string absPath  = findSourcePath( desc._source );

        // 0) 인메모리 캐시 조회 — 항목을 만든 컴파일이 연 헤더까지 포함한 지문이 지금도 같아야 맞다. 파일을 읽는 계산이라 잠금 밖에서 한다(항목을 복사해 나온다).
        if ( absPath.empty() == false )
        {
            ShaderCacheEntry cachedEntry{};
            bool             bCached = false;
            {
                ScopedLock<mutex> lock{ _mutexCache };
                const auto        iter = _mapCache.find( cacheKey );
                if ( iter != _mapCache.end() )
                {
                    cachedEntry = iter->second;
                    bCached     = true;
                }
            }
            if ( bCached )
            {
                const uint64 currentFingerprint = ShaderCooker::computeSourceFingerprint( absPath, cachedEntry._result._listIncludedFile );
                if ( currentFingerprint != 0 && currentFingerprint == cachedEntry._sourceFingerprint )
                    return cachedEntry._result;
            }
        }

        // 쿠킹본 정책(배포 · `-cooked-content`)은 팩의 쿠킹본만 읽는다 — 컴파일러가 없고, 소스도 팩에 없다.
        if ( AssetIO::getContentSource() == ContentSource::Cooked )
        {
            const std::optional<ShaderBinaryLocation> location = ShaderBinaryLocation::make( desc._source, desc._targetFormat );
            if ( location.has_value() == false )
            {
                SW_LOG_ERROR( "Shader source is not inside a domain 'shaders' folder: '%#'", desc._source.c_str() );
                ShaderCompileResult failedResult{};
                failedResult._bSuccess     = false;
                failedResult._errorMessage = "Shader source is not inside a domain 'shaders' folder";
                return failedResult;
            }
            const AssetID precookedID = location->makeFileID( makeBinaryFileName( desc ) );
            vector<uint8> precookedBytes;
            if ( AssetIO::readBinaryAsset( precookedID, precookedBytes ) && precookedBytes.empty() == false )
            {
                SW_LOG_TRACE( "Loaded cooked shader binary: %# (%zu bytes)", precookedID.c_str(), precookedBytes.size() );
                ShaderCompileResult result{};
                result._bytecode = std::move( precookedBytes );
                result._bSuccess = true;
                storeEntry( std::move( cacheKey ), desc, result, absPath );
                return result;
            }
            // 빠진 것은 파일이 아니라 (경로, 스테이지, 진입점, 퍼뮤테이션) 조합이다. 찾던 이름(RHI 폴더 포함) 그대로 적는다.
            SW_LOG_ERROR( "Precompiled shader binary not found in the cooked content: '%#' [%#] — %#", desc._source.c_str(), desc._entryPoint.c_str(), precookedID.c_str() );
            ShaderCompileResult failedResult{};
            failedResult._bSuccess     = false;
            failedResult._errorMessage = "Shader binary missing in the cooked content";
            return failedResult;
        }

        // 소스 정책(개발 실행)은 쿠킹본을 읽지 않는다 — 쿠킹본은 배포 단위라 쿠킹 스테이징(`build/<프리셋>/Cooked`)에만 있고, 개발 실행은 미리 굽지 않아도 돈다(언리얼 에디터의 DDC).
        // 1순위: 로컬 라이브 캐시(`Bin/Saved/ShaderCache`). 경로는 **실시간 컴파일이 실제로 쓰는 요청**으로 정한다 — 아래 컴파일이 그 요청으로 여기에 쓰고, 핫 리로드도 같은 요청으로 쓴다.
        // 경로에 이미 소스 지문이 들어 있고, 연 헤더가 그대로인지는 바이트코드 옆의 의존 기록이 본다. 찾혔다는 것이 곧 "이 소스와 이 헤더들에서 나온 것"이다.
        const ShaderCompileDesc liveDesc = makeLiveCompileDesc( desc );
        ShaderCompileResult     localResult{};
        if ( readLocalCache( liveDesc, localResult ) )
        {
            SW_LOG_TRACE( "Loaded live shader from the local cache: %#", makeLocalCachePath( liveDesc ).c_str() );
            storeEntry( std::move( cacheKey ), desc, localResult, absPath );
            return localResult;
        }

        // 2순위: 실시간 컴파일. 코드젠은 `gv_shaderDebugCodegen` 이 정한다(makeLiveCompileDesc).
        ShaderCompileResult compiledResult = ShaderCompiler::compileHLSL( liveDesc );
        if ( compiledResult._bSuccess )
        {
            (void)storeLocalCache( liveDesc, compiledResult ); // 바뀌었는지는 재컴파일(ShaderRecompiler)만 센다
            storeEntry( std::move( cacheKey ), desc, compiledResult, absPath );
        }
        return compiledResult;
    }

    void ShaderCache::clearCache()
    {
        ScopedLock<mutex> lock{ _mutexCache };
        _mapCache.clear();
    }

    void ShaderCache::collectCompiledDescs( vector<ShaderCompileDesc>& outListDesc ) const
    {
        ScopedLock<mutex> lock{ _mutexCache };
        outListDesc.clear();
        outListDesc.reserve( _mapCache.size() );
        for ( const pair<const string, ShaderCacheEntry>& entry : _mapCache )
        {
            // 파일 경로가 없는 항목은 다시 컴파일할 수 없다.
            if ( entry.second._desc._source.isEmpty() == false )
                outListDesc.push_back( entry.second._desc );
        }
    }

    void ShaderCache::storeEntry( string&& cacheKey, const ShaderCompileDesc& desc, const ShaderCompileResult& result, const string& absSourcePath )
    {
        // 지문은 파일을 읽는 계산이라 잠금 밖에서 한다.
        ShaderCacheEntry entry{};
        entry._sourceFingerprint = absSourcePath.empty() ? 0u : ShaderCooker::computeSourceFingerprint( absSourcePath, result._listIncludedFile );
        entry._desc              = desc;
        entry._result            = result;

        ScopedLock<mutex> lock{ _mutexCache };
        _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
    }
} // namespace sw
