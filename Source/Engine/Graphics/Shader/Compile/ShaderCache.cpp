#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCache" );

    namespace
    {
        /** @brief 쿠커가 쓰는 파일 이름 그대로입니다(스템, 스테이지, 진입점, 퍼뮤테이션 해시). 규칙은 한 곳(ShaderCooker)뿐입니다. */
        string makeBinaryFileName( const ShaderCompileDesc& desc )
        {
            const string fileName = FileUtil::getFileNamePart( desc._filePath );
            const string stem     = StringUtil::toLower( FileUtil::removeExtension( fileName ).c_str() );
            return ShaderCooker::computeBinaryFileName( stem, desc._stage, desc._entryPoint,
                                                        ShaderCooker::computePermutationHash( desc._listDefine ),
                                                        ShaderCooker::getExtensionForFormat( desc._targetFormat ) );
        }

        /** @brief 디스크 파일이 있고 비어 있지 않으면 그 바이트를 읽어 true 입니다(로컬 라이브 수정 캐시). */
        [[nodiscard]] bool readNonEmptyFile( const string& path, vector<uint8>& outBytes )
        {
            return FileUtil::exists( path ) && FileUtil::readFile( path, outBytes ) && outBytes.empty() == false;
        }

        /**
         * @brief 쿠킹된 바이너리가 지금 소스에서 나온 것인지입니다. 배포 구성은 늘 true 입니다.
         * @details **신선도는 `cook.stamp` 의 내용 해시로 본다.** mtime 비교는 안 된다. 이 저장소는 쿠킹된 바이너리까지 커밋하므로
         *          `git pull` 이 둘의 시간 순서를 임의로 뒤집어, 소스가 바뀌었는데도 "바이너리가 더 새것"이라 낡은 것을 쓴다.
         *          검사는 개발 빌드 전용이다. 배포에는 다시 컴파일할 길이 없어 거부하면 폴백이 아니라 빈 화면이고, 배포물의 신선도는
         *          쿠킹이 같은 스탬프로 이미 막는다(App --cook-all --verify-shaders).
         * @param outListIncludedFile 최신이면 스탬프에 적힌 이 소스의 헤더(절대 경로)를 담는다 — 인메모리 항목이 헤더를 고쳤을 때 낡음을 알아보는 데 쓴다. 판정하지 않았으면 비어 있다.
         */
        bool isPrecookedCurrent( const string& precookedRelPath, const string& sourceAbsPath, vector<string>& outListIncludedFile )
        {
            outListIncludedFile.clear();
#if !defined( SW_SHIPPING )
            if ( sourceAbsPath.empty() )
                return true;
            const string precookedAbsPath = ResourcePaths::findLooseFile( precookedRelPath ).getDiskPath();
            if ( precookedAbsPath.empty() || FileUtil::exists( precookedAbsPath ) == false )
                return true;
            const string binDirAbs = FileUtil::getDirectoryPart( FileUtil::normalizeSeparators( precookedAbsPath ) );
            if ( ShaderCooker::isCookedOutputCurrent( binDirAbs, sourceAbsPath, &outListIncludedFile ) )
                return true;
            SW_LOG_TRACE( "Pre-cooked shader does not match the current source — recompiling: %#", precookedRelPath.c_str() );
            return false;
#else
            (void)precookedRelPath;
            (void)sourceAbsPath;
            return true;
#endif
        }
    } // namespace
} // namespace sw

namespace sw
{
    string ShaderCache::makePrecookedRelativePath( const ShaderCompileDesc& desc )
    {
        const string      norm      = FileUtil::normalizeSeparators( desc._filePath );
        const string_view rhiFolder = ShaderCooker::getSubfolderForFormat( desc._targetFormat );
        const string      fileName  = makeBinaryFileName( desc );

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
        // **경로에 소스 지문을 한 단계 끼운다.** "캐시 파일이 소스보다 새것인가"를 파일 시간으로 보면 안 된다 —
        // 이 저장소는 산출물까지 커밋해서 `git pull` 이 시간 순서를 임의로 뒤집는다(ShaderCooker::computeSourceFingerprint
        // 주석). 경로가 다르면 낡은 것은 애초에
        // 찾히지 않으므로 시간 비교가 필요 없다. 남는 폴더는 Saved/ 안이라 버려도 그만이다.
        // 지문은 소스만 본다. 이 셰이더가 연 헤더는 컴파일해 봐야 알아서 경로에 넣을 수 없고, 바이트코드 옆의 의존 기록이 헤더의 변경을 맡는다(readLocalCache).
        //
        // **파일 이름이 아니라 폴더**에 넣는 이유는 이름이 쿠커가 쿠킹하는 이름과 글자 단위로 같아야
        // 하기 때문이다(규칙이 둘이면 그중 하나가 기준을 이긴다. `ShaderCookerTest` 가 지키는 계약이다).
        const string_view rhiFolder  = ShaderCooker::getSubfolderForFormat( desc._targetFormat );
        const string      absPath    = ResourcePaths::findLooseFile( desc._filePath ).getDiskPath();
        const uint64      sourceHash = absPath.empty() ? 0u : ShaderCooker::computeSourceFingerprint( absPath, vector<string>{} );

        StringBuilder<constant::kMaxBuffer64> sb;
        sb.appendFormat( "%#", Fmt( sourceHash, Format( 16, Format::Padding::Zero ).hex() ) );
        sb.append( desc._bDebugCodegen != SW_FALSE ? "-dbg" : "-opt" ); // 같은 소스라도 코드젠이 다르면 다른 파일이다

        // 작업 폴더가 아니라 실행 파일 폴더(`Bin`) 기준이다 — 테스트는 App 마다 작업 폴더(쓰기 루트)를 따로 주는데(`AppTestUtil::getRunFolder`) 이 캐시는 같은 빌드의 실행끼리 나누는 것이 목적이다.
        const string rhiDir    = FileUtil::joinPath( FileUtil::joinPath( FileUtil::getBinaryDirectory(), "Saved/ShaderCache" ), rhiFolder );
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
#if defined( SW_DEBUG )
        liveDesc._bDebugCodegen = SW_TRUE;
#endif
        return liveDesc;
    }

    ShaderLoadBlock::ShaderLoadBlock( ShaderCache* pCache, const ShaderCompileDesc& desc, string cacheKey )
        : AssetLoadBlock{ "Shader", desc._filePath }
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
        : _mapCache{}
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
        // 리플렉션 매니페스트 캐시는 프로세스 정적 저장소라 엔진 종료 뒤에도 남는다. 셰이더 층이 종료될 때 같이 비운다.
        // 비우지 않으면 백엔드 교체로 기동 뒤에 읽은 매니페스트가 종료 누수 검사에 남는다.
        ShaderReflectionLibrary::clearCache();
    }

    string ShaderCache::makeCacheKey( const ShaderCompileDesc& desc )
    {
        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( desc._filePath ).append( '_' ).append( desc._entryPoint ).append( '_' ).append( static_cast<int32>( desc._stage ) ).append( '_' ).append( static_cast<int32>( desc._targetFormat ) );
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
        const string absPath  = ResourcePaths::findLooseFile( desc._filePath ).getDiskPath();

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

        // 1순위(로컬 라이브 수정 캐시: Saved/ShaderCache/). 파일 이름에 퍼뮤테이션 해시가 들어간다(헤더 주석 참고).
        // 경로는 **실시간 컴파일이 실제로 쓰는 요청**으로 정한다. 아래 3순위가 그 요청으로 컴파일해 여기에 쓰고, 핫 리로드도 같은 요청으로 쓴다.
        // 경로에 이미 소스 지문이 들어 있고, 연 헤더가 그대로인지는 바이트코드 옆의 의존 기록이 본다. 찾혔다는 것이 곧 "이 소스와 이 헤더들에서 나온 것"이다.
        const ShaderCompileDesc liveDesc = makeLiveCompileDesc( desc );
        ShaderCompileResult     localResult{};
        if ( readLocalCache( liveDesc, localResult ) )
        {
            SW_LOG_TRACE( "Loaded live override shader from local cache: %#", makeLocalCachePath( liveDesc ).c_str() );
            storeEntry( std::move( cacheKey ), desc, localResult, absPath );
            return localResult;
        }

        // 2순위(Git 사전 컴파일 정식 바이너리 패스트 패스: Resource/<domain>/shaders/bin/<rhi>/ 또는 .pack)
        //
        // **지금 소스에서 나오지 않은(낡은) 바이너리는 쓰지 않는다.** 있기만 하면 이기게 두면 HLSL 을 고쳐도 화면이
        // 그대로이고(다시 쿠킹 전까지), 엔진 셰이더에 대해 라이브 컴파일 경로가 사실상 도달 불가가 된다.
        const string   precookedRelPath = makePrecookedRelativePath( desc );
        vector<uint8>  precookedBytes;
        vector<string> listPrecookedHeader;
        const bool     bPrecookedBinaryLoaded = isPrecookedCurrent( precookedRelPath, absPath, listPrecookedHeader ) && ResourceIO::readBinaryResource( precookedRelPath, precookedBytes ) &&
                                            precookedBytes.empty() == false;
        if ( bPrecookedBinaryLoaded )
        {
            SW_LOG_TRACE( "Loaded pre-cooked shader binary: %# (%zu bytes)", precookedRelPath.c_str(), precookedBytes.size() );
            ShaderCompileResult result{};
            result._bytecode         = std::move( precookedBytes );
            result._listIncludedFile = std::move( listPrecookedHeader );
            result._bSuccess         = true;
            storeEntry( std::move( cacheKey ), desc, result, absPath );
            return result;
        }

        // 3순위(런타임 DXC 컴파일 폴백, 개발 모드 전용)
#if !defined( SW_SHIPPING )
        // 라이브 컴파일은 Debug 에서 디버그 코드젠으로 한다(makeLiveCompileDesc). RenderDoc 에서 셰이더를 한 줄씩 볼 수 있어야 한다.
        // 쿠커는 이 경로를 타지 않으므로 쿠킹된 바이너리는 빌드 구성과 무관하게 늘 최적화된 것이다.
        ShaderCompileResult compiledResult = ShaderCompiler::compileHlsl( liveDesc );
        if ( compiledResult._bSuccess )
        {
            (void)storeLocalCache( liveDesc, compiledResult ); // 바뀌었는지는 재컴파일(ShaderRecompiler)만 센다
            storeEntry( std::move( cacheKey ), desc, compiledResult, absPath );
        }
        return compiledResult;
#else
        // 빠진 것은 파일이 아니라 (경로, 스테이지, 진입점, 퍼뮤테이션) 조합이다. 찾던 이름(RHI 폴더 포함) 그대로 적는다.
        SW_LOG_ERROR( "Precompiled shader binary not found in shipping pack: '%#' [%#] — %#",
                      desc._filePath.c_str(), desc._entryPoint.c_str(), precookedRelPath.c_str() );
        ShaderCompileResult failedResult{};
        failedResult._bSuccess     = false;
        failedResult._errorMessage = "Shader binary missing in shipping pack";
        return failedResult;
#endif
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
            if ( entry.second._desc._filePath.empty() == false )
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
