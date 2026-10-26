/**
 * @file ShaderCache.h
 * @brief 셰이더 컴파일 결과 캐시 매니저입니다.
 * @note AssetManager 가 아닙니다. 셰이더 바이트코드는 RHI와 컴파일러의 수명을 따르며 팩 에셋 인스턴스와 분리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Resource/AssetHandle.h"

namespace sw
{
    class ShaderCache;

    /**
     * @class ShaderLoadBlock
     * @brief 셰이더 바이트코드 하나(로컬 캐시 · 쿠킹본 · 실시간 컴파일)를 얻는 로드의 공유 상태입니다. 로드 단계가 `ShaderCache` 의 세 순위를 그대로 탑니다.
     * @details 결과는 실패해도 남긴다(`getResult` — 컴파일 오류 문구). 블록은 캐시를 원시 포인터로 가리킨다 — 캐시 종료(`shutdown`)가 진행 중인 로드를 모두 기다린다.
     */
    class SW_API ShaderLoadBlock final : public AssetLoadBlock
    {
    public:
        using AssetType = const ShaderCompileResult;

        ShaderLoadBlock( ShaderCache* pCache, const ShaderCompileDesc& desc, string cacheKey );

        /** @brief 바이트코드입니다. `Ready` 일 때만 쓸 수 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<const ShaderCompileResult>& getAsset() const { return _pResult; }
        /** @brief 끝난 로드의 결과입니다. 실패해도 오류 문구가 들어 있습니다. 끝나기 전에는 nullptr 입니다. */
        const shared_ptr<const ShaderCompileResult>& getResult() const { return _pResult; }

    protected:
        [[nodiscard]] bool runLoad( string& outError ) override;

    private:
        friend class ShaderCache;

        ShaderCache*                          _pCache;
        ShaderCompileDesc                     _desc;
        string                                _cacheKey;
        shared_ptr<const ShaderCompileResult> _pResult;
    };

    /** @brief 셰이더 로드 핸들입니다(`ShaderCache::request`). */
    using ShaderHandle = AssetHandle<ShaderLoadBlock>;
} // namespace sw

namespace sw
{
    /// @brief 인메모리 캐시 한 항목입니다(컴파일 요청 + 바이트코드 + 소스 지문).
    struct ShaderCacheEntry
    {
        /** @brief 이 항목을 만든 컴파일 요청. 수동 리로드가 이것으로 다시 컴파일합니다. */
        ShaderCompileDesc _desc;
        /** @brief 항목을 넣을 때의 소스 지문입니다(`ShaderCooker::computeSourceFingerprint`, 소스와 `_result._listIncludedFile` 의 헤더). 지금 지문과 다르면 낡았다. 0 이면 맞을 수 없다. */
        uint64              _sourceFingerprint{ 0 };
        ShaderCompileResult _result;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 컴파일 결과 디스크 캐시 매니저입니다.
     * @details 엔진에서는 서브시스템 `ShaderCache`(모든 호스트)입니다. 서버는 빈 캐시만 만듭니다(씬 쿠킹의 Headless 가 의존합니다).
     */
    class SW_API ShaderCache : public ISubsystem
    {
    public:
        ShaderCache();
        ~ShaderCache() override;

        ShaderCache( const ShaderCache& )            = delete;
        ShaderCache& operator=( const ShaderCache& ) = delete;

        /** @brief 셰이더 캐시를 초기화합니다. */
        bool initialize();
        /** @brief 셰이더 캐시를 정리하고 종료합니다. 리플렉션 매니페스트 캐시(`ShaderReflectionLibrary`)도 비웁니다. */
        void shutdown();

        // ISubsystem: 기동 단계 `ShaderCache`.
        SubsystemInitResult initializeSubsystem() override { return initialize() ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed; }
        void                shutdownSubsystem() override { shutdown(); }
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 항목이 이 상수를 가리킵니다). */
        static constexpr const utf8* kSubsystemName = "ShaderCache";

        const utf8*       getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask getSubsystemHost() const override { return SubsystemHostMask::All; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Shader; }

        /**
         * @brief 바이트코드 로드를 요청합니다. 같은 요청(경로 · 진입점 · 스테이지 · 대상 · 코드젠 · define)을 읽는 중이면 그 로드를 받고, 아니면 워커가 읽거나
         *        컴파일합니다(엔진 태스크가 없으면 이 자리에서). PSO 를 만들기 전에 미리 데워 둘 때 씁니다.
         */
        ShaderHandle request( const ShaderCompileDesc& desc );
        /** @brief 캐시에 있으면 반환하고, 없거나 소스가 바뀌었으면 컴파일한 뒤 캐시합니다. `request` 와 같은 로드 블록을 기다립니다(새 로드는 이 자리에서). */
        ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
        /** @brief 컴파일 캐시를 비웁니다. */
        void clearCache();

        /**
         * @brief 지금까지 컴파일한 셰이더들의 요청을 모읍니다.
         * @details **"이 실행에서 실제로 쓰이는 셰이더"의 기준이 여기입니다.** 수동 리로드는 따로 등록 목록을
         *          두지 않고 이것을 씁니다(따로 채우는 목록은 채우는 쪽을 빠뜨리면 리로드 단축키가 빈 목록을 돕니다).
         */
        void collectCompiledDescs( vector<ShaderCompileDesc>& outListDesc ) const;

        /**
         * @brief 이 컴파일 요청이 읽을 쿠킹된 바이너리의 리소스 상대 경로입니다(`<domain>/shaders/bin/<rhi>/<이름>`).
         * @details 파일 이름은 `ShaderCooker::computeBinaryFileName` 그대로입니다(스템, 스테이지, 진입점, **퍼뮤테이션 해시**).
         *          주의: 해시를 빼면 쿠커는 퍼뮤테이션마다 다른 파일을 쿠킹해 두는데 캐시는 늘 해시 0(define 없음) 파일을 읽어,
         *          어떤 define 도 GPU 에 닿지 않습니다. 리플렉션 매니페스트(ShaderReflectionLibrary::tryGet)는 해시를 넣은 키로
         *          찾으므로 레이아웃은 맞고 바이트코드만 틀린, 가장 조용한 종류의 어긋남이 됩니다.
         */
        static string makePrecookedRelativePath( const ShaderCompileDesc& desc );
        /**
         * @brief 이 컴파일 요청의 로컬 라이브 캐시 경로입니다(`<Bin>/Saved/ShaderCache/<rhi>/<소스 지문>-<opt|dbg>/<이름>`, 절대 경로 — 작업 폴더와 무관). 이름 규칙은 위와 같습니다.
         * @details 런타임 컴파일 결과와 ShaderRecompiler 의 재컴파일 결과가 같은 자리에 쓰입니다. 여기도 해시가 빠져
         *          있으면 처음 컴파일된 퍼뮤테이션 하나가 같은 셰이더의 다른 퍼뮤테이션 모두를 덮습니다.
         *          경로의 지문은 소스(와 도구 지문)만 본다 — 헤더는 컴파일 뒤에야 알아서 경로에 넣을 수 없고, 바이트코드 옆의 의존 기록(`<경로>.deps`)이 맡는다(`readLocalCache`).
         */
        static string makeLocalCachePath( const ShaderCompileDesc& desc );
        /**
         * @brief 로컬 라이브 캐시를 읽습니다. 바이트코드가 있고 **의존 기록의 헤더가 모두 지금도 같을 때만** 담고 true 입니다.
         * @param liveDesc `makeLiveCompileDesc` 로 정한 요청
         * @param outResult 성공하면 바이트코드와 연 헤더 목록(`_listIncludedFile`)을 담는다
         */
        [[nodiscard]] static bool readLocalCache( const ShaderCompileDesc& liveDesc, ShaderCompileResult& outResult );
        /**
         * @brief 컴파일 결과를 로컬 라이브 캐시에 씁니다 — 바이트코드와, 헤더를 고쳤을 때 낡음을 알아볼 의존 기록입니다. ShaderCache 의 실시간 컴파일과 ShaderRecompiler 가 같이 씁니다.
         * @return 바이트코드가 새로 생겼거나 이전과 달라졌으면 true 입니다(못 써도 true). 바이트가 같으면 바이트 파일은 다시 쓰지 않고(재컴파일 콜백이 PSO 를 다시 만들지 않게) 의존 기록만 새로 쓴다.
         */
        static bool storeLocalCache( const ShaderCompileDesc& liveDesc, const ShaderCompileResult& result );
        /**
         * @brief 실시간 컴파일이 실제로 쓰는 요청입니다. Debug 는 디버그 코드젠(RenderDoc 에서 한 줄씩 볼 수 있게), 그 밖은 요청 그대로입니다.
         * @details 캐시의 실시간 컴파일과 `ShaderRecompiler` 의 재컴파일이 **이 하나**로 코드젠을 정하고, 로컬 캐시 경로도 이것으로 만듭니다.
         *          둘이 따로 정하면 Debug 의 한 폴더에 두 코드젠이 섞이고, 핫 리로드한 셰이더만 디버그 정보를 잃습니다.
         *          쿠킹된 바이너리는 이 규칙을 타지 않습니다(쿠커는 늘 최적화 코드젠).
         */
        static ShaderCompileDesc makeLiveCompileDesc( const ShaderCompileDesc& desc );

    private:
        friend class ShaderLoadBlock;

        /** @brief 인메모리 캐시의 키입니다(경로 · 진입점 · 스테이지 · 대상 · 코드젠 · define). */
        static string makeCacheKey( const ShaderCompileDesc& desc );
        /** @brief `request` 와 `getOrCompile` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(`AssetLoadStart`). */
        ShaderHandle requestWith( const ShaderCompileDesc& desc, AssetLoadStart start );
        /** @brief 로드 단계 본문입니다 — 인메모리 캐시, 로컬 캐시, 쿠킹본, 실시간 컴파일 순으로 찾습니다(이 스레드). */
        ShaderCompileResult loadOrCompile( const ShaderCompileDesc& desc, const string& cacheKey );
        /** @brief 인메모리 캐시에 넣습니다. 로컬 캐시, 쿠킹된 바이너리, 라이브 컴파일 세 경우가 같은 항목을 만듭니다. @p absSourcePath 가 비면 지문이 0 이라 다시 맞지 않는다. */
        void storeEntry( string&& cacheKey, const ShaderCompileDesc& desc, const ShaderCompileResult& result, const string& absSourcePath );
        /** @brief 진행 중인 로드를 모두 기다립니다(종료 — 워커의 로드가 사라진 캐시에 쓰지 않게). */
        void waitForLoadsInFlight();

        unordered_map<string, ShaderCacheEntry>          _mapCache;
        unordered_map<string, weak_ptr<ShaderLoadBlock>> _mapLoading; ///< 읽거나 컴파일 중인 로드(같은 요청은 같은 블록). `_mutexCache` 로 지킨다
        mutable mutex                                    _mutexCache;
    };
} // namespace sw
