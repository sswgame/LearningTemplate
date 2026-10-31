/**
 * @file ShaderCache.h
 * @brief 셰이더 컴파일 결과 캐시 매니저입니다.
 * @note AssetManager 가 아닙니다. 셰이더 바이트코드는 RHI와 컴파일러의 수명을 따르며 팩 에셋 인스턴스와 분리합니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    class ShaderCache;

    /**
     * @class ShaderLoadBlock
     * @brief 셰이더 바이트코드 하나를 얻는 로드의 공유 상태입니다 — 쿠킹본 정책은 팩의 쿠킹본, 소스 정책은 컴파일 — 셰이더 DDC 를 먼저 본다(`ShaderCache::loadOrCompile`).
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
        [[nodiscard]] bool initialize();
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
         * @brief 메모리 캐시에 든 이 요청의 바이트코드를 @p outBytecode 에 담습니다. 없으면 false 입니다.
         * @details 재컴파일(`ShaderRecompiler`)이 "바이트가 바뀌었나" 를 이것과 견준다 — 바뀌지 않은 셰이더의 PSO 를 다시 만들지 않는다.
         */
        [[nodiscard]] bool findCachedBytecode( const ShaderCompileDesc& desc, vector<uint8>& outBytecode ) const;
        /**
         * @brief 실시간 컴파일이 실제로 쓰는 요청입니다. `gv_shaderDebugCodegen` 이 켜졌으면 디버그 코드젠(RenderDoc 에서 한 줄씩), 아니면 요청 그대로(쿠킹과 같은 최적화)입니다.
         * @details 캐시의 실시간 컴파일과 `ShaderRecompiler` 의 재컴파일이 **이 하나**로 코드젠을 정합니다.
         *          둘이 따로 정하면 DDC 에 두 코드젠이 섞이고, 핫 리로드한 셰이더만 디버그 정보를 잃습니다.
         *          쿠킹된 바이너리는 이 규칙을 타지 않습니다(쿠커는 늘 최적화 코드젠).
         */
        static ShaderCompileDesc makeLiveCompileDesc( const ShaderCompileDesc& desc );

        /** @brief 읽은 리플렉션 매니페스트입니다. `ShaderReflectionLibrary` 의 조회 · 비우기가 서비스 칸으로 찾아 씁니다. */
        ShaderReflectionManifestCache& getReflectionManifests() { return _reflectionManifests; }

    private:
        friend class ShaderLoadBlock;

        /** @brief 인메모리 캐시의 키입니다(경로 · 진입점 · 스테이지 · 대상 · 코드젠 · define). */
        static string makeCacheKey( const ShaderCompileDesc& desc );
        /** @brief `request` 와 `getOrCompile` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(`AssetLoadStart`). */
        ShaderHandle requestWith( const ShaderCompileDesc& desc, AssetLoadStart start );
        /** @brief 로드 단계 본문입니다 — 인메모리 캐시 다음, 쿠킹본 정책은 팩의 쿠킹본만, 소스 정책은 컴파일(셰이더 DDC 를 먼저 본다)입니다(이 스레드). */
        ShaderCompileResult loadOrCompile( const ShaderCompileDesc& desc, const string& cacheKey );
        /** @brief 인메모리 캐시에 넣습니다. 지문은 결과의 소스(`_sourceFile`)와 연 헤더로 셉니다. */
        void storeEntry( string&& cacheKey, const ShaderCompileDesc& desc, const ShaderCompileResult& result );
        /** @brief 진행 중인 로드를 모두 기다립니다(종료 — 워커의 로드가 사라진 캐시에 쓰지 않게). */
        void waitForLoadsInFlight();

        ShaderReflectionManifestCache                                _reflectionManifests; ///< 읽은 리플렉션 매니페스트(`ShaderReflectionLibrary` 의 조회가 찾는다)
        unordered_map<string, ShaderCacheEntry> _mapCache            SW_GUARDED_BY( _mutexCache );
        unordered_map<string, weak_ptr<ShaderLoadBlock>> _mapLoading SW_GUARDED_BY( _mutexCache ); ///< 읽거나 컴파일 중인 로드(같은 요청은 같은 블록). `_mutexCache` 로 지킨다
        mutable mutex                                                _mutexCache;
    };
} // namespace sw
