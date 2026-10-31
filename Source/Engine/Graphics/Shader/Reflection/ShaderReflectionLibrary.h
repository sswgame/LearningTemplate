/**
 * @file ShaderReflectionLibrary.h
 * @brief 오프라인에서 쿠킹된 셰이더 리플렉션 매니페스트입니다(RHI 별 파일 하나).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"

namespace sw
{
    struct ShaderCompileDesc;

    /**
     * @class ShaderReflectionLibrary
     * @brief 셰이더 바인딩 리플렉션을 **런타임이 아니라 쿠킹 시점에** 뽑아 두고 읽어 쓰는 라이브러리입니다.
     * @details DXIL 리플렉션은 `dxcompiler.dll`(IDxcUtils::CreateReflection)을 필요로 합니다. D3D12 에는
     *          독립 리플렉션 API 가 없습니다. 그것을 런타임에 하면 배포물에 셰이더 컴파일러를 같이 넣어야 하고,
     *          없으면 바인딩이 조용히 어긋나 DEVICE_HUNG 으로 갑니다(실제로 그랬습니다).
     *          상용 엔진(Unreal 의 셰이더 라이브러리 등)처럼, 바이트코드를 쿠킹할 때 리플렉션도 같이
     *          쿠킹해 배포하고 런타임은 읽기만 합니다.
     *
     *          파일은 **RHI 폴더마다 하나**입니다: `<domain>/shaders/bin/<rhi>/reflection.manifest`.
     *          셰이더마다 사이드카를 두면 팩 엔트리, 조회, 압축 해제가 셰이더 수만큼 늘어납니다. 변형이
     *          수천 개로 늘어도 파일 하나면 I/O 가 일정합니다.
     */
    class SW_API ShaderReflectionLibrary
    {
    public:
        /// @brief 매니페스트 한 개의 내용입니다(쿠킹할 때 채우고 파일로 쿠킹합니다).
        using EntryMap = unordered_map<string, ShaderReflectionData>;

        /** @brief 매니페스트 파일 이름입니다(RHI 폴더 안에 놓입니다). */
        static const utf8* getManifestFileName();

        /**
         * @brief 매니페스트를 파일로 쿠킹합니다(쿠커 전용).
         * @details 항목은 키 순서로 씁니다. 같은 항목 집합이면 넣은 순서와 상관없이 같은 바이트입니다.
         * @param absDirectory 매니페스트를 놓을 절대 디렉터리(`.../shaders/bin/<rhi>`)
         */
        [[nodiscard]] static bool save( const EntryMap& mapEntry, string_view absDirectory );

        /**
         * @brief 매니페스트 하나를 통째로 읽습니다. 캐시하지 않고 신선도도 따지지 않습니다.
         * @details 셰이더 하나가 아니라 **쿠킹된 것 전부**를 보는 쪽이 씁니다. 계약 테스트가 C++ 가 바인딩하는 이름이 실제로 쿠킹됐는지 확인하는 경우입니다.
         *          매니페스트는 리플렉터 없이 읽히므로 DXBC 와 DXIL 리플렉터가 없는 플랫폼에서도 네 백엔드를 다 봅니다.
         * @param binDirectoryRelative 리소스 상대 `<domain>/shaders/bin/<rhi>` 디렉터리
         * @return 파일이 없거나 형식이나 내용이 맞지 않으면 false 입니다(그때 `outMapEntry` 는 비어 있습니다).
         */
        [[nodiscard]] static bool loadManifest( string_view binDirectoryRelative, EntryMap& outMapEntry );

        /**
         * @brief 셰이더 하나의 리플렉션을 쿠킹된 매니페스트에서 조회합니다(쿠킹본 정책의 공급자 `CookedShaderBytecodeProvider` 가 부른다 — 소스 정책은 실행 중 바이트코드에서 만든다).
         * @details 매니페스트는 경로별로 한 번만 읽어 캐시합니다. 팩 파일과 낱개 파일을 가리지 않고 읽습니다. 리플렉션을 구하는 입구는 `ShaderCache::getReflection` 하나입니다.
         *          주의: 한쪽(ShaderBindingLayoutCache)만 다른 출처를 쓰고 Material 이 XML 순서 패킹으로 남으면 셰이더는 24바이트 원소를 읽는데
         *          엔진은 256바이트(상수 버퍼 크기) stride 로 업로드해, 한 배치의 두 번째 머티리얼부터 통째로 어긋납니다.
         * @return 매니페스트에 없으면 false 입니다.
         */
        [[nodiscard]] static bool tryGet( const ShaderCompileDesc& desc, ShaderReflectionData& outReflection );

        /** @brief 캐시를 비웁니다(셰이더를 다시 쿠킹한 뒤, 라이브 셰이더를 편집한 뒤, 엔진 종료 때 `ShaderCache::shutdown`). 캐시 세대가 증가합니다. */
        static void clearCache();

        /** @brief 지금 캐시에 있는 매니페스트 수입니다(RHI 폴더마다 하나이고, 없는 파일도 빈 채로 하나입니다). 진단과 테스트용입니다. */
        static uint32 getCachedManifestCount();

        /**
         * @brief 캐시를 비운 횟수입니다. 이 값이 바뀌면 전에 얻은 리플렉션은 낡았을 수 있습니다.
         * @details 머티리얼은 셰이더 레이아웃을 맞출 때 이 값을 적어 두고, 바뀌면 다시 맞춥니다(`Material::isShaderLayoutSynced`).
         */
        static uint32 getCacheGeneration();
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShaderReflectionManifestCache
     * @brief 읽은 리플렉션 매니페스트(매니페스트 경로 → 항목)와 비운 횟수입니다. 셰이더 서브시스템(`ShaderCache`)이 값으로 들고 `ShaderReflectionLibrary` 의 조회가 찾아 씁니다.
     * @details 예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다. 매니페스트는 셰이더 층의 쿠킹본이라 셰이더 캐시와 수명이 같다
     *          (언리얼 `FShaderCodeLibrary` 가 셰이더 라이브러리 · 그 메타데이터를 셰이더 쪽 객체로 드는 자리). 여러 스레드가 함께 써도 된다(안에서 잠근다).
     */
    class SW_API ShaderReflectionManifestCache
    {
    public:
        ShaderReflectionManifestCache()                                                  = default;
        ShaderReflectionManifestCache( const ShaderReflectionManifestCache& )            = delete;
        ShaderReflectionManifestCache& operator=( const ShaderReflectionManifestCache& ) = delete;

        /** @brief 매니페스트를 모두 버리고 세대를 올립니다(`ShaderReflectionLibrary::clearCache` 의 본문, 셰이더 캐시 종료). */
        void clear() SW_EXCLUDES_LOCK( _mutex );
        /** @brief 지금 든 매니페스트 수입니다. */
        uint32 getCount() SW_EXCLUDES_LOCK( _mutex );
        /** @brief 비운 횟수입니다. */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }

    private:
        friend class ShaderReflectionLibrary; // 조회(`tryGet`)가 잠금 안에서 맵을 채우고 찾는다

        mutex _mutex;
        /// @brief 매니페스트 경로 → 항목입니다. 없는 파일도 빈 채로 캐시해 매번 다시 열지 않습니다.
        unordered_map<string, ShaderReflectionLibrary::EntryMap> _mapManifest SW_GUARDED_BY( _mutex );
        /// @brief clear 횟수입니다 — 머티리얼이 맞춘 레이아웃이 낡았는지 본다.
        atomic<uint32> _generation{ 0 };
    };
} // namespace sw
