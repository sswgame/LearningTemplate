#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/Shader/Compile/ShaderBytecodeProvider.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    ShaderCompileDesc ShaderCache::makeLiveCompileDesc( const ShaderCompileDesc& desc, bool bDebugCodegen )
    {
        // 기본(최적화 코드젠)은 쿠킹과 같은 바이트라 그림이 셰이더를 어디서 얻었느냐(캐시 · 컴파일 · 쿠킹본)나 빌드 구성에 달리지 않는다 — 골든 비교가 그것에 기댄다.
        // 디버그 코드젠은 진단 정책이 켤 때만이다(언리얼 `r.Shaders.Optimize=0` 과 같은 자리). 깃발은 부르는 쪽이 사본으로 넘긴다 — 워커가 진단 정책을 직접 읽지 않는다.
        ShaderCompileDesc liveDesc = desc;
        if ( bDebugCodegen )
            liveDesc._bDebugCodegen = SW_TRUE;
        return liveDesc;
    }

    ShaderLoadOperation::ShaderLoadOperation( ShaderCache* pCache, const ShaderCompileDesc& desc, string cacheKey )
        : AssetLoadOperation{ "Shader", desc._source.str(), MemoryTag::Shader }
        , _pCache{ pCache }
        , _desc{ desc }
        , _cacheKey{ std::move( cacheKey ) }
        , _pResult{}
    {
    }

    expected<void> ShaderLoadOperation::runLoad()
    {
        shared_ptr<ShaderCompileResult> pResult = sw::make_shared<ShaderCompileResult>( _pCache->loadOrCompile( _desc, _cacheKey ) );
        expected<void>                  loaded  = {};
        if ( pResult->_bSuccess == false )
            loaded = unexpected( Error{ ErrorKind::ExternalFailure, "the shader could not be compiled or found", pResult->_errorMessage } ); // 컴파일러 글은 대상(앞 `Error::kDetailCapacity` 바이트)
        _pResult = std::move( pResult );
        {
            // 이 로드 작업의 칸일 때만 뺀다(캐시를 비운 뒤 같은 요청이 새 로드 작업을 넣었을 수 있다).
            ScopedLock<mutex> lock{ _pCache->_mutexCache };
            const auto        it = _pCache->_mapLoading.find( _cacheKey );
            if ( it != _pCache->_mapLoading.end() && ( it->second.expired() || it->second.lock().get() == this ) )
                _pCache->_mapLoading.erase( it );
        }
        return loaded;
    }

    ShaderCache::ShaderCache()
        : _pProvider{}
        , _reflectionManifests{}
        , _mapCache{}
        , _mapLoading{}
        , _mutexCache{}
        , _enginePolicy{}
        , _requestLog{}
        , _requestLogPath{}
        , _configReloadedSubscription{}
        , _memoryHitCount{}
        , _providerLoadCount{}
        , _compileBusy{}
        , _bLiveDebugCodegen{ SW_FALSE }
    {
    }

    ShaderCache::~ShaderCache()
    {
        shutdown();
    }

    bool ShaderCache::initialize()
    {
        // 콘텐츠 정책은 기동 때 정해지고(에셋 매니저 - 이 서브시스템이 그 뒤에 선다) 실행 중 바뀌지 않는다 - 공급자를 한 번 고른다.
        // 디버그 코드젠도 여기(기동 스레드)서 한 번 뜬다 — 컴파일 워커는 공급자가 든 사본만 본다.
        _bLiveDebugCodegen      = RenderDiagnostics::get()._bShaderDebugCodegen == SW_TRUE ? SW_TRUE : SW_FALSE;
        const bool bInitialized = AssetIO::getContentSource() == ContentSource::Cooked ? initializeWithProvider( make_unique<CookedShaderBytecodeProvider>() )
                                                                                       : initializeWithProvider( make_unique<LiveShaderBytecodeProvider>( isLiveDebugCodegen() ) );
        if ( bInitialized == false )
            return false;
        // 셰이더에 들어가는 설정 값은 여기(기동 스레드)와 다시 읽기 알림(게임 스레드)에서만 뜬다 — 컴파일 워커는 요청에 실린 사본만 본다.
        ConfigManager* const pConfigManager = engine::findSubsystem<ConfigManager>();
        if ( pConfigManager == nullptr )
        {
            SW_LOG_ERROR_ID( "ShaderCache.NoConfigManager", "ShaderCache needs the Config subsystem - the engine settings that shaders read come from it" );
            return false;
        }
        const ShaderEnginePolicy policy = ShaderEnginePolicy::capture();
        {
            ScopedLock<mutex> lock{ _mutexCache };
            _enginePolicy = policy;
        }
        _configReloadedSubscription = pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &ShaderCache::onConfigReloaded, this ) );
        // 요청 기록은 명시 인자로만 켠다(묶음 검증의 시나리오 체인 — Test/Scenario/CMakeLists.txt). 다음 검증이 시간을 재기 전에 이 목록만 컴파일해 DDC 를 데운다(`ShaderRequestLog`).
        _requestLogPath.clear();
        (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::SHADER_REQUEST_LOG, _requestLogPath ); // 없으면 기록하지 않는다
        return true;
    }

    void ShaderCache::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName )
            return;
        const ShaderEnginePolicy policy = ShaderEnginePolicy::capture();
        ScopedLock<mutex>        lock{ _mutexCache };
        _enginePolicy = policy;
    }

    optional<ShaderEnginePolicy> ShaderCache::findEnginePolicy() const
    {
        ScopedLock<mutex> lock{ _mutexCache };
        return _enginePolicy;
    }

    bool ShaderCache::initializeWithProvider( unique_ptr<IShaderBytecodeProvider> pProvider )
    {
        if ( pProvider == nullptr )
        {
            SW_LOG_ERROR_ID( "ShaderCache.NoProvider", "ShaderCache needs a bytecode provider (cooked or live) - not initialized" );
            return false;
        }
        clearCache();
        _memoryHitCount.reset();
        _providerLoadCount.reset();
        _pProvider = std::move( pProvider );
        return true;
    }

    span<const utf8* const> ShaderCache::getSubsystemDependencies() const
    {
        // 콘텐츠 정책(`AssetIO::getContentSource`)은 에셋 매니저의 초기화가 정한다. 셰이더에 들어가는 설정 값은 Config 에서 뜬다.
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName, ConfigManager::kSubsystemName };
        return kArrDependency;
    }

    void ShaderCache::shutdown()
    {
        _configReloadedSubscription.reset();
        waitForLoadsInFlight();
        writeRequestLog();
        clearCache();
        // 리플렉션 매니페스트도 셰이더 층이 종료될 때 같이 비운다(백엔드 교체로 다시 서면 새 백엔드의 매니페스트를 읽는다). 소멸자에서도 불리므로 서비스 칸을 거치지 않는다.
        _reflectionManifests.clear();
        _pProvider.reset(); // 진행 중인 로드를 모두 기다린 뒤다
    }

    string ShaderCache::makeCacheKey( const ShaderCompileDesc& desc )
    {
        // 구분자는 경로 · 식별자 · define 값에 올 수 없는 줄바꿈이다 — `_` 로 이으면 값 `1_B=1` 하나와 define 둘(`A=1`, `B=1`)이 같은 키가 됐다.
        constexpr utf8                           kSeparator = '\n';
        StringBuilder<constant::buffer::kMax256> sb;
        sb.append( desc._source.str() ).append( kSeparator ).append( desc._entryPoint ).append( kSeparator ).append( static_cast<int32>( desc._stage ) ).append( kSeparator ).append( static_cast<int32>( desc._targetFormat ) );
        sb.append( kSeparator ).append( desc._bDebugCodegen != SW_FALSE ? "dbg" : "opt" );
        for ( const string& define : ShaderCooker::makeCanonicalDefines( desc._listDefine ) )
        {
            sb.append( kSeparator ).append( define );
        }
        return string( sb.c_str(), sb.size() );
    }

    ShaderHandle ShaderCache::request( const ShaderCompileDesc& desc )
    {
        return requestWith( desc, AssetLoadStart::Async );
    }

    ShaderHandle ShaderCache::requestWith( const ShaderCompileDesc& desc, AssetLoadStart start )
    {
        string                          cacheKey = makeCacheKey( desc );
        shared_ptr<ShaderLoadOperation> pOperation;
        {
            ScopedLock<mutex> lock{ _mutexCache };
            const auto        it = _mapLoading.find( cacheKey );
            if ( it != _mapLoading.end() )
                pOperation = it->second.lock();
            if ( pOperation != nullptr )
                return ShaderHandle{ std::move( pOperation ) };
            // 인메모리 적중도 로드 단계가 본다 — 적중인지는 소스 해시를 구해야 알고, 그 계산(파일 읽기)은 잠금 밖에서 한다.
            // 엔진 설정 사본을 요청에 싣는다 — 로드 워커는 설정을 직접 읽지 않는다(제자리 덮기의 울타리는 렌더 스레드만 기다린다).
            ShaderCompileDesc loadDesc = desc;
            if ( loadDesc._enginePolicy.has_value() == false )
                loadDesc._enginePolicy = _enginePolicy;
            pOperation            = sw::make_shared<ShaderLoadOperation>( this, loadDesc, cacheKey );
            _mapLoading[cacheKey] = pOperation;
        }
        AssetLoadOperation::startLoad( pOperation, start );
        return ShaderHandle{ std::move( pOperation ) };
    }

    ShaderCompileResult ShaderCache::getOrCompile( const ShaderCompileDesc& desc )
    {
        const ShaderHandle handle = requestWith( desc, AssetLoadStart::OnCallingThread );
        handle.getOperation()->wait();
        // 실패도 결과를 돌려준다 — 부르는 쪽이 컴파일 오류 문구를 드러낸다.
        const shared_ptr<const ShaderCompileResult>& pResult = handle.getOperation()->getResult();
        if ( pResult != nullptr )
            return *pResult;
        ShaderCompileResult failedResult{};
        failedResult._bSuccess     = false;
        failedResult._errorMessage = handle.getError();
        return failedResult;
    }

    void ShaderCache::waitForLoadsInFlight()
    {
        vector<shared_ptr<ShaderLoadOperation>> listLoading;
        {
            ScopedLock<mutex> lock{ _mutexCache };
            for ( const auto& [key, weakOperation] : _mapLoading )
            {
                shared_ptr<ShaderLoadOperation> pOperation = weakOperation.lock();
                if ( pOperation != nullptr )
                    listLoading.push_back( std::move( pOperation ) );
            }
        }
        for ( const shared_ptr<ShaderLoadOperation>& pOperation : listLoading )
        {
            pOperation->wait();
        }
    }

    const IShaderBytecodeProvider& ShaderCache::getProvider() const
    {
        if ( _pProvider == nullptr )
            SW_FATAL( "ShaderCache is used before initialize chose the bytecode provider (cooked or live)" );
        return *_pProvider;
    }

    bool ShaderCache::getReflection( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection )
    {
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag };
        return getProvider().getReflection( *this, desc, outReflection );
    }

    ShaderCompileResult ShaderCache::loadOrCompile( const ShaderCompileDesc& desc, const string& inCacheKey )
    {
        const ScopedMemoryTag          memoryScope{ kSubsystemMemoryTag };
        const IShaderBytecodeProvider& provider = getProvider();
        string                         cacheKey = inCacheKey;

        // 0) 인메모리 캐시 조회 — 넣을 때의 지문(공급자가 센다 - 실행 중 컴파일은 소스와 연 헤더의 내용)이 지금도 같아야 맞다. 파일을 읽는 계산이라 잠금 밖에서 한다(항목을 복사해 나온다).
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
            const uint64 currentFingerprint = provider.computeFingerprint( cachedEntry._result, desc );
            if ( currentFingerprint != 0 && currentFingerprint == cachedEntry._sourceFingerprint )
            {
                _memoryHitCount.add();
                return cachedEntry._result;
            }
        }

        // 1) 공급자 — 쿠킹본 정책은 팩의 쿠킹본, 소스 정책은 실행 중 컴파일(셰이더 DDC 를 먼저 본다).
        _providerLoadCount.add();
        ShaderCompileResult result;
        {
            BusyScope busy{ _compileBusy };
            result = provider.load( desc );
        }
        if ( result._bSuccess )
        {
            storeEntry( std::move( cacheKey ), desc, result );
            return result;
        }
        // 낡은 항목이 있으면(편집하다 깨진 소스) 마지막 성공 판으로 그린다 — 언리얼 · 유니티도 컴파일이 깨진 셰이더는 이전 판을 쓴다. 오류는 위에서 이미 남겼다.
        if ( bCached && cachedEntry._result._bSuccess )
        {
            SW_LOG_WARNING_ID( "ShaderCache.KeptLastGoodShader", "Shader '%#' (%#) failed to compile - keeping the last compiled version", desc._source.c_str(), desc._entryPoint.c_str() );
            return cachedEntry._result;
        }
        return result;
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

    bool ShaderCache::findCachedBytecode( const ShaderCompileDesc& desc, vector<uint8>& outBytecode ) const
    {
        outBytecode.clear();
        ScopedLock<mutex> lock{ _mutexCache };
        const auto        iter = _mapCache.find( makeCacheKey( desc ) );
        if ( iter == _mapCache.end() )
            return false;
        outBytecode = iter->second._result._bytecode;
        return true;
    }

    bool ShaderCache::replaceCompiled( const ShaderCompileDesc& desc, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || result._bytecode.empty() )
        {
            SW_LOG_ERROR_ID( "ShaderCache.ReplaceWithFailedResult", "Shader '%#' (%#): only a successful compile result can replace a cache entry", desc._source.c_str(), desc._entryPoint.c_str() );
            return false;
        }
        storeEntry( makeCacheKey( desc ), desc, result );
        return true;
    }

    void ShaderCache::storeEntry( string&& cacheKey, const ShaderCompileDesc& desc, const ShaderCompileResult& result )
    {
        // 지문은 파일을 읽는 계산이라 잠금 밖에서 한다.
        ShaderCacheEntry entry{};
        entry._sourceFingerprint = getProvider().computeFingerprint( result, desc );
        entry._desc              = desc;
        entry._result            = result;

        ScopedLock<mutex> lock{ _mutexCache };
        if ( _requestLogPath.empty() == false )
            (void)_requestLog.add( desc ); // 이미 적은 요청이면 할 일이 없다
        _mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
    }

    void ShaderCache::writeRequestLog()
    {
        string text;
        {
            ScopedLock<mutex> lock{ _mutexCache };
            // 공급자까지 간 요청이 없는 프로세스(백엔드별 부모 — 창 · GPU 가 없다)는 쓰지 않는다. 파일은 이 실행의 기록이라 덮는다(앞 실행의 낡은 요청이 쌓이지 않는다).
            if ( _requestLogPath.empty() || _requestLog.size() == 0 )
                return;
            text        = _requestLog.makeText();
            _requestLog = ShaderRequestLog{};
        }
        if ( FileUtil::ensureParentDirectoryExists( _requestLogPath ) == false || FileUtil::writeTextFile( _requestLogPath, text ) == false )
            SW_LOG_WARNING_ID( "ShaderCache.RequestLogNotWritten", "Could not write the shader request log '%#' - the next warm-up compiles nothing for this run", _requestLogPath );
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( ShaderCache, GameVisible, Startup, (), SW_ENGINE_DEFAULT_FACTORY( ShaderCache ) );
} // namespace sw
