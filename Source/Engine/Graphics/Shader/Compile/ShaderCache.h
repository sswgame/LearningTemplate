/**
 * @file ShaderCache.h
 * @brief 셰이더 컴파일 결과 캐시 매니저입니다.
 * @note AssetManager 가 아닙니다. 셰이더 바이트코드는 RHI와 컴파일러의 수명을 따르며 팩 에셋 인스턴스와 분리합니다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Time/BusyClock.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderRequestLog.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    class IShaderBytecodeProvider;
    class ShaderCache;

    /**
     * @class ShaderLoadOperation
     * @brief 셰이더 바이트코드 하나를 얻는 로드의 공유 상태입니다 — 쿠킹본 정책은 팩의 쿠킹본, 소스 정책은 컴파일 — 셰이더 DDC 를 먼저 본다(`ShaderCache::loadOrCompile`).
     * @details 결과는 실패해도 남긴다(`getResult` — 컴파일 오류 문구). 로드 작업은 캐시를 원시 포인터로 가리킨다 — 캐시 종료(`shutdown`)가 진행 중인 로드를 모두 기다린다.
     */
    class SW_API ShaderLoadOperation final : public AssetLoadOperation
    {
    public:
        using AssetType = const ShaderCompileResult;

        ShaderLoadOperation( ShaderCache* pCache, const ShaderCompileDesc& desc, string cacheKey, uint64 replaceCountAtStart );

        /** @brief 바이트코드입니다. `Ready` 일 때만 쓸 수 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const ShaderCompileResult>& getAsset() const { return _pResult; }
        /** @brief 끝난 로드의 결과입니다. 실패해도 오류 문구가 들어 있습니다. 끝나기 전에는 nullptr 입니다. */
        const shared_ptr<const ShaderCompileResult>& getResult() const { return _pResult; }

    protected:
        [[nodiscard]] expected<void> runLoad() override;

    private:
        friend class ShaderCache;

        ShaderCache*                          _pCache;
        ShaderCompileDesc                     _desc;
        string                                _cacheKey;
        uint64                                _replaceCountAtStart; ///< 넣을 때의 `ShaderCache::_replaceCount` — 그 뒤에 바뀐 항목은 이 로드보다 새것이다
        shared_ptr<const ShaderCompileResult> _pResult;
    };

    /** @brief 셰이더 로드 핸들입니다(`ShaderCache::request`). */
    using ShaderHandle = AssetHandle<ShaderLoadOperation>;
} // namespace sw

namespace sw
{
    /// @brief 인메모리 캐시 한 항목입니다(컴파일 요청 + 바이트코드 + 소스 지문).
    struct ShaderCacheEntry
    {
        /** @brief 이 항목을 만든 컴파일 요청. 수동 리로드가 이것으로 다시 컴파일합니다. */
        ShaderCompileDesc _desc;
        uint64            _replaceSerial{ 0 }; ///< 재컴파일이 이 항목을 바꾼 번호입니다(`ShaderCache::replaceCompiled`). 로드가 넣은 항목은 0.
        /** @brief 컴파일 결과입니다. 지금도 맞는지는 공급자가 본다(`IShaderBytecodeProvider::isCurrent` — 실행 중 컴파일은 결과의 쿠킹 기록으로). */
        ShaderCompileResult _result;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 컴파일 결과 디스크 캐시 매니저입니다.
     * @details 엔진에서는 서브시스템 `ShaderCache`(모든 호스트)입니다. 서버는 빈 캐시만 만듭니다(씬 쿠킹의 Headless 가 의존합니다).
     */
    class SW_API ShaderCache : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 항목이 이 상수를 가리킵니다). */
        static constexpr const utf8* kSubsystemName      = "ShaderCache";
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::Shader;
        ShaderCache();
        ~ShaderCache() override;

        ShaderCache( const ShaderCache& )            = delete;
        ShaderCache& operator=( const ShaderCache& ) = delete;

        /**
         * @brief 셰이더 캐시를 초기화합니다. 바이트코드 공급자는 콘텐츠 정책(쿠킹본 · 소스)으로 고릅니다.
         * @details 엔진 설정 중 셰이더에 들어가는 값의 사본(`ShaderEnginePolicy`)을 뜨고, 설정 다시 읽기 알림(게임 스레드)마다 다시 뜬다 — 요청이 그 사본을 싣고 워커로 간다.
         */
        [[nodiscard]] bool initialize();
        /**
         * @brief 공급자 @p pProvider 를 걸고 캐시를 초기화합니다. `initialize` 가 정책으로 고른 공급자를 이것으로 겁니다.
         * @details 테스트가 디스크 · 팩 없이 공급자 규칙(쿠킹본의 상수 지문 등)만 시험할 때 바로 부른다. 비어 있으면 오류로 false 입니다.
         */
        [[nodiscard]] bool initializeWithProvider( unique_ptr<IShaderBytecodeProvider> pProvider );
        /** @brief 셰이더 캐시를 정리하고 종료합니다. 리플렉션 매니페스트 캐시(`ShaderReflectionLibrary`)도 비웁니다. */
        void shutdown();

        // Subsystem: 기동 단계 `ShaderCache`.
        SubsystemInitResult initializeSubsystem() override { return initialize() ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed; }
        void                shutdownSubsystem() override { shutdown(); }

        const utf8*       getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask getSubsystemHost() const override { return SubsystemHostMask::All; }
        /** @brief 에셋 매니저 뒤에 섭니다 — 바이트코드 공급자를 고르는 콘텐츠 정책을 그것이 정한다. */
        span<const utf8* const> getSubsystemDependencies() const override;

        /**
         * @brief 바이트코드 로드를 요청합니다. 같은 요청(경로 · 진입점 · 스테이지 · 대상 · 코드젠 · define)을 읽는 중이면 그 로드를 받고, 아니면 워커가 읽거나
         *        컴파일합니다(엔진 태스크가 없으면 이 자리에서). PSO 를 만들기 전에 미리 데워 둘 때 씁니다.
         */
        ShaderHandle request( const ShaderCompileDesc& desc );
        /** @brief 캐시에 있으면 반환하고, 없거나 소스가 바뀌었으면 컴파일한 뒤 캐시합니다. `request` 와 같은 로드 작업을 기다립니다(새 로드는 이 자리에서). */
        ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
        /**
         * @brief 요청 @p desc 의 바인딩 리플렉션을 구합니다. 쿠킹본 정책은 쿠킹된 매니페스트, 소스 정책은 이 캐시가 얻은 바이트코드에서 만든다(공급자가 정한다).
         * @details 매니페스트와 바이트코드는 같은 출처여야 한다 — 섞으면 레이아웃과 바이트코드가 어긋난다. 머티리얼 스키마와 바인딩 레이아웃이 이 하나로 구한다.
         */
        [[nodiscard]] bool getReflection( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );
        /** @brief 컴파일 캐시를 비웁니다. */
        void clearCache();

        /**
         * @brief 지금까지 컴파일한 셰이더들의 요청을 모읍니다.
         * @details **"이 실행에서 실제로 쓰이는 셰이더"의 기준이 여기입니다.** 수동 리로드는 따로 등록 목록을
         *          두지 않고 이것을 씁니다(따로 채우는 목록은 채우는 쪽을 빠뜨리면 리로드 단축키가 빈 목록을 돕니다).
         *          **컴파일 중인 요청도 든다** — 편집 앞의 소스를 읽은 컴파일이 편집 뒤에 끝나면 옛 바이트코드가 캐시에 남는다. 목록에 들어야 다시 컴파일해 견준다.
         */
        void collectCompiledDescs( vector<ShaderCompileDesc>& outListDesc ) const;

        /**
         * @brief 메모리 캐시에 든 이 요청의 바이트코드를 @p outBytecode 에 담습니다. 없으면 false 입니다.
         * @details 재컴파일(`ShaderRecompiler`)이 "바이트가 바뀌었나" 를 이것과 견준다 — 바뀌지 않은 셰이더의 PSO 를 다시 만들지 않는다.
         */
        [[nodiscard]] bool findCachedBytecode( const ShaderCompileDesc& desc, vector<uint8>& outBytecode ) const;
        /**
         * @brief 재컴파일(`ShaderRecompiler`)이 새로 낸 바이트코드로 이 요청의 항목 하나만 바꿉니다. 성공한 결과가 아니면 오류를 남기고 false 입니다.
         * @details 캐시를 통째로 비우지 않는다 — 비우면 이번 묶음에서 컴파일이 깨진 셰이더의 마지막 성공 판까지 사라져 다음 요청이 깨진 소스를 다시 컴파일하다 진다.
         *          언리얼은 재컴파일이 실패한 셰이더의 이전 셰이더 맵을 그대로 쓴다 — 같은 규칙이다.
         *          이 앞에 시작한 로드는 옛 소스를 읽었을 수 있다 — 그 로드는 이 항목을 덮지 않고 이 항목을 답으로 돌려준다.
         */
        [[nodiscard]] bool replaceCompiled( const ShaderCompileDesc& desc, const ShaderCompileResult& result );
        /**
         * @brief 실시간 컴파일이 실제로 쓰는 요청입니다. @p bDebugCodegen 이 켜졌으면 디버그 코드젠(RenderDoc 에서 한 줄씩), 아니면 요청 그대로(쿠킹과 같은 최적화)입니다.
         * @details 캐시의 실시간 컴파일과 `ShaderRecompiler` 의 재컴파일이 **이 하나**로 코드젠을 정합니다. 깃발은 둘 다 `isLiveDebugCodegen` 의 값을 넘긴다.
         *          둘이 따로 정하면 DDC 에 두 코드젠이 섞이고, 핫 리로드한 셰이더만 디버그 정보를 잃습니다.
         *          쿠킹된 바이너리는 이 규칙을 타지 않습니다(쿠커는 늘 최적화 코드젠).
         */
        [[nodiscard]] static ShaderCompileDesc makeLiveCompileDesc( const ShaderCompileDesc& desc, bool bDebugCodegen );
        /**
         * @brief 실시간 컴파일의 디버그 코드젠 깃발입니다. 진단 정책(`RenderDiagnosticsPolicy::_bShaderDebugCodegen`)을 기동(`initialize`, 기동 스레드)에서 한 번 뜬 값이다.
         * @details 진단 정책의 채우는 쪽은 개발 변수(`gv_shaderDebugCodegen`)를 읽는다 — 컴파일 워커가 그것을 직접 읽으면 콘솔 쓰기와 겹친다. 기동 때 컴파일한 셰이더도 따라야 해
         *          명령줄로 주는 값이라 실행 중 바꿀 일이 없다 — 공급자(`LiveShaderBytecodeProvider`)가 만들 때 사본을 든다.
         */
        [[nodiscard]] bool isLiveDebugCodegen() const { return _bLiveDebugCodegen != SW_FALSE; }

        /**
         * @brief 요청에 싣는 엔진 설정 사본입니다(`initialize` 와 설정 다시 읽기 알림이 뜬다). 엔진 없이 공급자만 건 캐시(`initializeWithProvider` — 테스트)는 비어 있다.
         * @details 캐시를 거치지 않고 컴파일하는 쪽(`ShaderRecompiler`)이 요청에 이것을 싣는다. 아무 스레드에서 불러도 된다(잠근다).
         */
        [[nodiscard]] optional<ShaderEnginePolicy> findEnginePolicy() const;
        /** @brief 메모리 캐시가 맞아 공급자를 부르지 않은 로드 수입니다(초기화마다 0 부터). 쿠킹본 정책은 지문이 상수라 같은 요청의 둘째 로드부터 맞아야 한다. */
        [[nodiscard]] const DevCounter& getMemoryHitCount() const { return _memoryHitCount; }
        /** @brief 공급자(팩 · 컴파일)까지 간 로드 수입니다(초기화마다 0 부터). */
        [[nodiscard]] const DevCounter& getProviderLoadCount() const { return _providerLoadCount; }
        /**
         * @brief 컴파일(셰이더 공급자 로드 — 팩 읽기 · 컴파일, 그리고 RHI 의 파이프라인 생성 — 드라이버 링크 · 특수화)이 하나라도 진행 중이던 벽시계 시간입니다(겹친 일은 한 번).
         * @details 자동화의 모든 벽시계 시한(전체 · `WaitUntil timeoutWallSeconds`)과 행 감시가 이만큼을 뺀다 — 빈 캐시의 첫 실행이 컴파일하는 동안 시한에 지지 않게.
         *          파이프라인 생성은 `IRHIResourceFactory::createPipelineState` 한 입구가 센다(GL 은 링크가 프레임 하나에 수십 초 걸렸다). 시계가 스스로 잠근다.
         */
        [[nodiscard]] BusyClock&       getCompileBusyClock() { return _compileBusy; }
        [[nodiscard]] const BusyClock& getCompileBusyClock() const { return _compileBusy; }

        /** @brief 읽은 리플렉션 매니페스트입니다. `ShaderReflectionLibrary` 의 조회 · 비우기가 서비스 칸으로 찾아 씁니다. */
        ShaderReflectionManifestCache& getReflectionManifests() { return _reflectionManifests; }

    private:
        friend class ShaderLoadOperation;

        /** @brief 인메모리 캐시의 키입니다(경로 · 진입점 · 스테이지 · 대상 · 코드젠 · define). */
        static string makeCacheKey( const ShaderCompileDesc& desc );
        /** @brief `request` 와 `getOrCompile` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(`AssetLoadStart`). */
        ShaderHandle requestWith( const ShaderCompileDesc& desc, AssetLoadStart start );
        /** @brief 로드 단계 본문입니다 — 인메모리 캐시 다음, 쿠킹본 정책은 팩의 쿠킹본만, 소스 정책은 컴파일(셰이더 DDC 를 먼저 본다)입니다(이 스레드). */
        ShaderCompileResult loadOrCompile( const ShaderCompileDesc& desc, const string& cacheKey, uint64 replaceCountAtStart );
        /** @brief 기동 때 고른 공급자입니다. 초기화 전에 부르면 멈춘다. */
        const IShaderBytecodeProvider& getProvider() const;
        /** @brief 진행 중인 로드를 모두 기다립니다(종료 — 워커의 로드가 사라진 캐시에 쓰지 않게). */
        void waitForLoadsInFlight();
        /** @brief 설정 다시 읽기 알림입니다(게임 스레드). 엔진 설정이면 요청에 실을 사본을 다시 뜬다. */
        void onConfigReloaded( const hashed_string& configTypeName );
        /** @brief 요청 기록(`-shader-request-log`)을 켰고 모은 요청이 있으면 그 파일에 씁니다(종료 — 진행 중인 로드를 기다린 뒤). */
        void writeRequestLog();

    private:
        unique_ptr<IShaderBytecodeProvider>                              _pProvider;           ///< 기동 때(`initialize`) 콘텐츠 정책으로 고른 바이트코드 공급자. 로드 워커는 읽기만 한다
        ShaderReflectionManifestCache                                    _reflectionManifests; ///< 읽은 리플렉션 매니페스트(`ShaderReflectionLibrary` 의 조회가 찾는다)
        unordered_map<string, ShaderCacheEntry> _mapCache                SW_GUARDED_BY( _mutexCache );
        unordered_map<string, weak_ptr<ShaderLoadOperation>> _mapLoading SW_GUARDED_BY( _mutexCache ); ///< 읽거나 컴파일 중인 로드(같은 요청은 같은 로드 작업). `_mutexCache` 로 지킨다
        mutable mutex                                                    _mutexCache;
        optional<ShaderEnginePolicy> _enginePolicy                       SW_GUARDED_BY( _mutexCache ); ///< 요청에 싣는 엔진 설정 사본(`findEnginePolicy`) — 워커가 설정을 직접 읽지 않게 요청을 받는 자리에서 싣는다
        uint64 _replaceCount                                             SW_GUARDED_BY( _mutexCache ); ///< `replaceCompiled` 수. 로드가 시작할 때의 값보다 뒤에 바뀐 항목을 가린다
        ShaderRequestLog _requestLog                                     SW_GUARDED_BY( _mutexCache ); ///< 이 실행이 공급자까지 간 요청(백엔드 교체로 메모리 캐시를 비워도 남는다) — `_requestLogPath` 가 있을 때만 모은다
        AbsolutePath                                                     _requestLogPath;              ///< `-shader-request-log` 의 파일. 비면 기록하지 않는다. 기동(`initialize`)에서만 쓰고 로드 워커는 읽기만 한다
        Subscription                                                     _configReloadedSubscription;  ///< 설정 다시 읽기 알림 구독(`onConfigReloaded`)
        DevCounter                                                       _memoryHitCount;              ///< `getMemoryHitCount` — 로드 워커가 함께 센다
        DevCounter                                                       _providerLoadCount;           ///< `getProviderLoadCount` — 로드 워커가 함께 센다
        BusyClock                                                        _compileBusy;                 ///< `getCompileBusyClock` — 로드 워커 · PSO 워커가 함께 센다
        uint8                                                            _bLiveDebugCodegen;           ///< `isLiveDebugCodegen` — 기동에서만 쓴다(그때는 로드가 없다)

        SW_SERVICE_SLOT( ShaderCache, Required );
    };
} // namespace sw
