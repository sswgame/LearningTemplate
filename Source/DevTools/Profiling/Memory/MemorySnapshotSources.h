/**
 * @file MemorySnapshotSources.h
 * @brief 메모리 스냅숏(`MemorySnapshot`)을 원천에서 모읍니다 — 태그 · 할당 사이트(`MemoryProfiler`), 에셋(`AssetCacheBase::collectMemoryEntries`),
 *        GPU 자원(`RHIMemoryTracker::collectResources`), 오브젝트 · 컴포넌트 형식(씬의 오브젝트 저장소), 캐시 계수(`CacheStats`).
 * @details 원천은 값 묶음(`MemorySnapshotSources`)으로 받는다 — 테스트는 가짜 캐시 · 추적기를 넣고, 엔진은 `makeEngine` 이 서비스에서 채운다.
 *          수집은 게임 스레드에서 부르고, 원천마다 자기 잠금을 복사하는 동안만 잡는다(추적기 · 캐시 표). 해제는 백엔드가 펜스 뒤 콜백에서 추적기에서 내리므로
 *          GPU 자원 목록은 렌더 스레드를 따로 세우지 않아도 "GPU 가 놓은 뒤" 의 목록이다. 기호 풀기(할당 사이트)는 상위 N 줄만 한다(설정).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "DevTools/DevToolsAPI.h"
#include "DevTools/Profiling/Memory/MemorySnapshot.h"

namespace sw
{
    class AssetCacheBase;
    class GameObjectManager;
    class MemoryProfiler;
    class RHIMemoryTracker;

    /**
     * @struct MemorySnapshotSources
     * @brief 스냅숏 하나를 모을 원천과, 그 원천에서 스냅숏을 짓는 함수들입니다. 없는 원천(nullptr · 빈 목록)의 시트는 빈다. 포인터는 `collect` 가 도는 동안만 쓴다.
     * @details 시트마다 함수 하나다(시트를 더하면 함수 하나가 는다).
     */
    struct SW_DEVTOOLS_API MemorySnapshotSources
    {
        const MemoryProfiler*            _pProfiler{ nullptr };     ///< 태그 · 할당 사이트
        uint32                           _allocationSiteLimit{ 0 }; ///< 할당 사이트 줄 상한(`Config/Dev/MemorySnapshot.json`)
        vector<const AssetCacheBase*>    _listAssetCache{};         ///< 에셋 캐시
        const RHIMemoryTracker*          _pGPUTracker{ nullptr };   ///< GPU 자원
        string                           _gpuBackendName{};         ///< GPU 백엔드 이름(요약 글)
        vector<const GameObjectManager*> _listObjectManager{};      ///< 형식별 수 — 올라온 씬마다 하나
        vector<CacheStatsSnapshot>       _listCacheStats{};         ///< 캐시 계수(측정 창은 `CacheStats::markWindowStart` 의 것)

        /** @brief 이 원천에서 스냅숏을 짓습니다. @p label · @p source 는 머리 글입니다. */
        MemorySnapshot collect( string_view label, string_view source ) const;
        /**
         * @brief 이 프로세스의 엔진 서비스에서 원천을 채웁니다(게임 스레드). 편집 씬 · PIE 씬 · 미리보기 씬이 모두 든다(`SceneManager::collectAllScenes`).
         * @details 엔진 서비스가 없으면(헤드리스 도구) 프로파일러 · 캐시 계수만 있는 원천이다. 반환한 포인터는 다음 프레임까지만 유효하다 — 같은 프레임에 `collect` 한다.
         */
        static MemorySnapshotSources makeEngine( uint32 allocationSiteLimit );

        void collectTags( MemorySheet& outSheet ) const;
        void collectAllocationSites( MemorySheet& outSheet ) const;
        void collectAssets( MemorySheet& outSheet ) const;
        /** @brief GPU 자원 줄을 채우고 바이트 합을 돌려줍니다(크기 모름은 0 으로 센다). */
        uint64 collectGPUResources( MemorySheet& outSheet ) const;
        void   collectTypes( MemorySheet& outSheet ) const;
        void   collectCaches( MemorySheet& outSheet ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 원천 하나를 채우는 함수입니다(게임 스레드, `MemorySnapshotSources::makeEngine` 끝에서 등록 순서로). */
    using MemorySnapshotSourceFillFunction = void ( * )( MemorySnapshotSources& inoutSources );

    /**
     * @struct MemorySnapshotSourceRegistration
     * @brief 구성마다 있고 없는 원천 하나의 등록입니다(`SW_MEMORY_SNAPSHOT_SOURCE`) — 클라이언트의 GPU 자원처럼 전용 서버가 짓지 않는 원천을 그 구성의 파일이 스스로 등록한다.
     * @details 공용 수집은 등록된 원천만 돈다 — 서버 구성이 렌더 이음매를 알 일이 없다(언리얼 memreport 가 모듈마다 등록된 명령을 도는 것과 같은 자리).
     */
    struct SW_DEVTOOLS_API MemorySnapshotSourceRegistration
    {
        const utf8*                      _pName;     ///< 원천 이름(중복 검사 · 로그)
        MemorySnapshotSourceFillFunction _pFunction; ///< 채우는 함수

        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerSource( const MemorySnapshotSourceRegistration* pRegistration );
        static void               unregisterSource( const MemorySnapshotSourceRegistration* pRegistration );
        /** @brief 등록된 원천을 등록 순서로 모두 채웁니다. */
        static void fillAll( MemorySnapshotSources& inoutSources );
    };

    /** @brief `SW_MEMORY_SNAPSHOT_SOURCE` 가 두는 정적 등록자입니다. */
    SW_DEFINE_STATIC_REGISTRAR( MemorySnapshotSourceRegistrar, const MemorySnapshotSourceRegistration*, &MemorySnapshotSourceRegistration::registerSource, &MemorySnapshotSourceRegistration::unregisterSource );
} // namespace sw

/** @brief 메모리 스냅숏 원천을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_MEMORY_SNAPSHOT_SOURCE( id, pName, pFunction ) SW_STATIC_REGISTRATION( memorySnapshotSource, ::sw::MemorySnapshotSourceRegistration, ::sw::MemorySnapshotSourceRegistrar, id, pName, pFunction )
