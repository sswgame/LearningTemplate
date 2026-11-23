/**
 * @file AssetLoadProfiler.h
 * @brief 에셋 로딩 프로파일러 — 에셋 하나를 읽을 때 IO · 해석 · GPU 올리기 시간, 바이트, 동기 · 비동기를 모아 종류별 표와 가장 느린 로드를 냅니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/EnumArray.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

namespace sw
{
    /** @brief 로드 한 번의 단계입니다. 단계를 나눌 수 없는 로더(파일을 읽으며 해석한다)는 `IO` 하나에 모두 적습니다. */
    enum class AssetLoadPhase : uint8
    {
        IO = 0, ///< 파일 · 팩에서 바이트를 읽는다
        Decode, ///< 바이트를 객체로 푼다(XML · 메시 · 텍스처 머리)
        Upload, ///< GPU 자원을 만들고 올린다
        Count
    };

    /** @brief 로드 한 번의 기록입니다. */
    struct SW_API AssetLoadRecord
    {
        string                            _path{};
        hashed_string                     _kind{}; ///< "Texture" · "Mesh" · "Material" · "Prefab" · "Scene" …
        EnumArray<AssetLoadPhase, uint64> _arrPhaseNanos{};
        uint64                            _bytes{ 0 };
        uint8                             _bAsync{ SW_FALSE }; ///< 워커 스레드에서 읽었다(스트리밍 · 비동기 씬)
        uint8                             _bSucceeded{ SW_TRUE };

        uint64 computeTotalNanos() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 에셋 종류 하나의 누적입니다. */
    struct AssetLoadKindSummary
    {
        hashed_string                     _kind{};
        EnumArray<AssetLoadPhase, uint64> _arrPhaseNanos{};
        uint64                            _maxTotalNanos{ 0 };
        uint64                            _bytes{ 0 };
        uint32                            _count{ 0 };
        uint32                            _asyncCount{ 0 };
        uint32                            _failedCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetLoadProfiler
     * @brief 에셋 로드 기록부입니다 — 엔진 서비스(`engine::findSubsystem<AssetLoadProfiler>()`)로, 서비스 범위가 소유합니다(Engine.dll 안 — 모듈 핫 리로드에 사라지지 않는다). 스레드 안전합니다.
     * @details 엔진 범위를 세우지 않는 호스트(도구)에는 이 서비스가 없고, 그때 `AssetLoadScope` 는 아무것도 재지 않는다.
     *          로더가 `AssetLoadScope` 를 열면 단계마다 시간이 측정되고, 닫힐 때 여기 넘어온다. 여기서:
     *          - 종류별 누적(수 · 바이트 · 단계별 시간 · 가장 긴 로드 · 비동기 수 · 실패 수)
     *          - 가장 느린 로드 `kSlowestCount` 개(경로 · 단계별 시간)
     *          - 엔진 프레임 프로파일러가 있으면 `Asset.<종류>.<단계>` 구간 · `Asset.Bytes` 카운터에도 더한다(`frame breakdown` 표에 로딩이 보인다).
     *          `-gv_assetLoadProfile=0` 이면 아무것도 모으지 않는다. 보고는 `report`(개발 도구가 `-gv_assetLoadReport=1` 이면 엔진 종료 때 부른다 — `ProfilerLaunchSubsystem`).
     *          언리얼 Insights 의 LoadTimeProfiler(패키지마다 IO · 직렬화 시간) · 유니티 프로파일러의 Loading 마커와 같은 자리입니다.
     */
    class SW_API AssetLoadProfiler final : public Subsystem
    {
    public:
        static constexpr uint32      kSlowestCount           = 16;
        static constexpr uint64      kSyncLoadLogNanoseconds = static_cast<uint64>( constant::time::kNanosecondsPerMillisecond ); ///< 씬 갱신 안의 동기 로드를 알리는 문턱
        static constexpr const utf8* kSubsystemName          = "AssetLoadProfiler";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        AssetLoadProfiler();

        /** @brief 기록이 켜졌는지입니다(게임 스레드가 뜬 사본 — 어느 스레드에서든 부른다). */
        bool isEnabled() const;
        /** @brief 기록을 켜거나 끕니다(`-gv_assetLoadProfile` 을 바꾸고 사본도 다시 뜬다). 게임 스레드 전용입니다. */
        void setEnabled( bool bEnabled );
        /** @brief 사본을 지금 변수 값으로 다시 뜹니다. **게임 스레드 전용**(Debug 단언) — 기동 때와 프레임마다 엔진이 부른다(`Engine::updateFrameServices`). */
        void refreshEnabled();
        /** @brief 로드 한 번을 더합니다. 꺼져 있으면 버립니다. */
        void submit( const AssetLoadRecord& record );
        /**
         * @brief 씬 갱신(컴포넌트 틱 · 프레임 시스템)이 도는 동안인지를 정합니다(게임 스레드 — `Engine::updateScenes`).
         * @details 그동안의 동기 로드는 그 프레임을 로드 시간만큼 멈춘다. 씬을 짓는 로드(로딩 화면 · 에디터 명령)는 이 밖이라 세지 않는다.
         */
        void setSceneUpdateActive( bool bActive );
        /**
         * @brief 부른 스레드에서 끝난 동기 로드(`AssetLoadStart::OnCallingThread`) 하나를 받습니다. 게임 스레드의 씬 갱신 안이면 세고, 경로마다 처음 한 번
         *        `kSyncLoadLogNanoseconds` 이상 걸린 것을 로그(`AssetLoad.SyncInSceneUpdate`)로 알립니다.
         * @details 언리얼이 게임 스레드의 동기 로드(`FlushAsyncLoading`)를 로그로 알리는 자리다(기억). 정보 로그인 까닭은 씬의 첫 프레임(BeginPlay)이 늘 얼마쯤 동기로 읽기 때문이다.
         */
        void recordSyncLoad( const utf8* pKind, string_view path, uint64 elapsedNanoseconds );
        /** @brief 씬 갱신 안에서 일어난 동기 로드 수 · 시간 합입니다. */
        struct SyncLoadTotal
        {
            uint32 _count{ 0 };
            uint64 _nanoseconds{ 0 };
        };
        SyncLoadTotal getSceneUpdateSyncLoads() const;
        /** @brief 종류별 누적을 총 시간이 긴 것부터 담습니다. */
        void collectSummaries( vector<AssetLoadKindSummary>& outListSummary ) const;
        /** @brief 가장 느린 로드를 느린 것부터 담습니다. */
        void collectSlowest( vector<AssetLoadRecord>& outListRecord ) const;
        /** @brief 지금까지 받은 로드 수입니다. */
        uint32 getLoadCount() const;
        /** @brief 종류별 표와 가장 느린 로드를 로그로 남깁니다. 받은 것이 없으면 한 줄입니다. */
        void report( const utf8* pTitle ) const;
        void reset();

    private:
        /** @brief 종류 하나의 누적과 프레임 프로파일러 구간 번호(단계마다)입니다. */
        struct KindEntry
        {
            AssetLoadKindSummary              _summary{};
            EnumArray<AssetLoadPhase, uint32> _arrProfilerSlot{};
        };

        mutable mutex                          _mutex;
        vector<KindEntry> _listKind            SW_GUARDED_BY( _mutex );
        vector<AssetLoadRecord> _listSlowest   SW_GUARDED_BY( _mutex ); ///< 느린 것부터(최대 kSlowestCount)
        uint32 _loadCount                      SW_GUARDED_BY( _mutex );
        SyncLoadTotal _syncLoadTotal           SW_GUARDED_BY( _mutex );
        unordered_set<string> _setSyncLoadPath SW_GUARDED_BY( _mutex ); ///< 이미 알린 경로
        atomic<uint8>                          _bEnabled;               ///< `-gv_assetLoadProfile` 의 사본(게임 스레드가 뜬다) — 로드 워커 · 렌더 스레드의 `submit` 이 잠금 없이 읽는다
        atomic<uint8>                          _bSceneUpdateActive;     ///< 게임 스레드가 쓰고, 로드를 끝낸 스레드가 읽는다

        SW_SERVICE_SLOT( AssetLoadProfiler, Optional );
    };
} // namespace sw

namespace sw
{
    /**
     * @class AssetLoadScope
     * @brief 로드 한 번을 측정합니다. 열 때 `IO` 단계가 시작되고, `beginPhase` 가 앞 단계를 닫고 다음을 열며, 닫힐 때 마지막 단계를 닫고 기록을 넘긴다.
     * @code
     *     AssetLoadScope scope( "Texture", path );
     *     // 파일 읽기
     *     scope.beginPhase( AssetLoadPhase::Upload );
     *     // GPU 올리기
     *     scope.setBytes( byteCount );
     *     scope.setSucceeded();
     * @endcode
     *          메인 스레드가 아닌 곳에서 열리면 비동기로 적는다. 꺼져 있으면 시계를 읽지 않는다.
     */
    class SW_API AssetLoadScope
    {
    public:
        AssetLoadScope( const utf8* pKind, string_view path, bool bAsync = false );
        ~AssetLoadScope();

        AssetLoadScope( const AssetLoadScope& )            = delete;
        AssetLoadScope& operator=( const AssetLoadScope& ) = delete;

        void beginPhase( AssetLoadPhase phase );
        void setBytes( uint64 bytes ) { _record._bytes = bytes; }
        /** @brief 로드가 성공했습니다. 부르지 않고 닫히면 실패로 적힌다(실패 길마다 적지 않아도 된다). */
        void setSucceeded() { _record._bSucceeded = SW_TRUE; }

    private:
        AssetLoadProfiler* _pProfiler; ///< 서비스가 없으면(엔진 범위를 세우지 않는 호스트) nullptr — 이 범위는 아무것도 재지 않는다
        AssetLoadRecord    _record;
        int64              _phaseStartNanos;
        AssetLoadPhase     _phase;
        uint8              _bActive;
    };
} // namespace sw
