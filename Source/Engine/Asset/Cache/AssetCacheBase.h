/**
 * @file AssetCacheBase.h
 * @brief 경로를 키로 에셋을 들고 있는 캐시의 공통 계약입니다. 에셋 종류를 늘리는 자리입니다.
 *
 * [왜 필요한가]
 * 캐시가 셋(Material · Texture · Prefab)인데 그것을 아는 코드가 이름으로 셋을 적으면, 종료 경로가 하나를 잊고(재초기화 뒤에도
 * 옛 내용이 남는다) 새 에셋 종류를 더할 때 캐시 클래스 · 매니저 멤버 · 게터 · 종료 경로를 손으로 같이 고쳐야 합니다.
 * **목록이 여럿이면 한쪽만 늘어납니다.** 이 저장소가 린트 · 픽서 · 백엔드에서 같은 결론에 도달한 자리입니다.
 *
 * 상용 엔진도 같은 모양입니다. 언리얼의 `FStreamableManager` · 고도의 `ResourceFormatLoader` · 유니티의
 * `AssetDatabase` 는 모두 "종류마다 등록하고, 공통 동작은 레지스트리를 훑는다" 입니다. 여기서는 그 최소한만
 * 가져옵니다. **레지스트리를 훑어서 되는 일**(종료 · 진단 · 재초기화)만 이 인터페이스로 하고, 종류마다
 * 다른 것(무엇을 어떻게 읽어 오는가)은 구체 캐시가 그대로 갖습니다.
 *
 * @note 수명은 `AssetManager` 가 쥡니다. 레지스트리는 **소유하지 않는 포인터**만 들고 있으므로,
 *       등록한 캐시는 매니저보다 오래 살아야 합니다.
 */
#pragma once
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    // 리로드 함수 형식은 이름만 쓴다 — 캐시 계약을 읽는 TU(1000 여)가 열거 값 · AssetID 정의를 끌지 않게(값은 ReloadResult.h · AssetID.h).
    class AssetID;
    enum class ReloadResult : uint8;

    /**
     * @class CacheTracking
     * @brief 캐시가 계측(`CacheStats`)에 어떻게 서는지의 선언입니다. `AssetCacheBase` 의 생성자가 이 값을 요구해, 계측을 정하지 않은 캐시는 지을 수 없습니다.
     * @details 캐시마다 "조회 자리에서 적중 · 실패를 센다" 는 약속을 손으로 지키면 새 캐시가 잊습니다(`[CacheStats]` 에 줄이 없는 캐시가 생김).
     *          그래서 둘 중 하나만 쓸 수 있게 한다 — 센다(`counted`, 종류 이름의 칸을 올린다) 또는 세지 않는 사유를 적는다(`uncounted`, 조회가 없는 어댑터).
     *          칸은 종류 이름이 키이고 프로세스 끝까지 살아, 엔진을 다시 세워 캐시를 다시 지어도 같은 칸을 이어 센다.
     */
    class CacheTracking final
    {
    public:
        /** @brief 종류 이름 @p kindName 의 칸을 올리거나 이어 받습니다. 캐시는 조회 자리에서 `AssetCacheBase::getCacheStats` 로 센다. */
        static CacheTracking counted( string_view kindName ) { return CacheTracking{ CacheStats::getOrCreate( kindName ), nullptr }; }
        /** @brief 세지 않습니다. @p pReason 은 왜 세지 않는지 적는 문자열 상수이고, 비어 있으면 안 됩니다(조회가 없는 어댑터가 쓴다). */
        static CacheTracking uncounted( const utf8* pReason )
        {
            SW_ASSERT( pReason != nullptr && pReason[0] != 0 );
            return CacheTracking{ CacheStatsHandle{}, pReason };
        }

        /** @brief 칸을 가리키는 핸들입니다(`uncounted` 면 비어 있다). */
        const CacheStatsHandle& getHandle() const { return _handle; }
        /** @brief 세지 않는 사유입니다(`counted` 면 nullptr). */
        const utf8* getUncountedReason() const { return _pUncountedReason; }

    private:
        CacheTracking( const CacheStatsHandle& handle, const utf8* pReason )
            : _handle{ handle }
            , _pUncountedReason{ pReason }
        {
        }

    private:
        CacheStatsHandle _handle;
        const utf8*      _pUncountedReason;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AssetMemoryEntry
     * @brief 캐시가 쥔 에셋 한 줄의 메모리입니다(`AssetCacheBase::collectMemoryEntries`). 메모리 스냅숏의 에셋 표가 읽는다(언리얼 `obj list` · `ListTextures` 의 줄).
     * @details 바이트는 캐시가 아는 만큼의 추정이다 — 텍스처 · 메시는 픽셀 · 정점 버퍼까지, 공유 에셋(`SharedAssetCache`)은 읽으며 남긴 바이트(안쪽 컨테이너까지 — 배포본은 객체 크기),
     *          나머지는 객체 크기(`sizeof`)다. 용도별 합은 메모리 태그 표(`MemoryProfiler`)가 센다. 종류 이름은 캐시의 것(`getAssetKindName`)이라 줄에 두지 않는다.
     */
    struct AssetMemoryEntry
    {
        string _path{};              ///< 리소스 id. 경로가 키가 아닌 캐시(지어 나누는 값)는 `<종류>#<번호>` 다
        uint64 _cpuBytes{ 0 };       ///< CPU 쪽 추정 바이트(객체 + 쥔 버퍼)
        uint64 _gpuBytes{ 0 };       ///< GPU 쪽 논리 바이트(텍스처 밉 · 메시 정점 · 인덱스). GPU 자원이 없으면 0
        uint32 _referenceCount{ 0 }; ///< 쥔 쪽 수(캐시 자신과 이 질의의 사본은 뺀다)
    };

    class AssetLoadOperation;

    /**
     * @struct AssetPin
     * @brief 캐시가 에셋 하나를 붙든 결과입니다(`AssetCacheBase::pinAsset`). 토큰이 비면 그 종류를 고정하는 캐시가 없다.
     * @details 로드를 같이 내주는 것은 프리로드가 그 로드의 끝남 · 실패를 보고 따로 읽지 않게 하려는 것이다(언리얼 `FStreamableHandle` 이 로드한 객체를 핸들이 쥔다 — 감사 A-19).
     */
    struct AssetPin
    {
        shared_ptr<const void>         _token{}; ///< 사는 동안 에셋을 붙드는 토큰
        shared_ptr<AssetLoadOperation> _pLoad{}; ///< 그 에셋을 읽는 로드. 비면 읽기는 그 종류의 다른 단계가 한다(머티리얼 — 게임 스레드 초기화)
    };

    /**
     * @class AssetCacheBase
     * @brief 경로 키 에셋 캐시가 공통으로 답해야 하는 것들입니다.
     */
    class SW_API AssetCacheBase
    {
    public:
        /** @brief 다시 읽을 파일이 있는 캐시인지입니다(`getAssetCacheReloadFunction`). 코드로 짓는 값 표(`WeakInternCache`)가 거짓으로 가린다. */
        static constexpr bool kReloadable = true;

        /** @brief 계측 선언(`CacheTracking`)을 받아 만듭니다. 선언 없이는 지을 수 없다. 나머지 상태는 구현이 갖습니다. */
        explicit AssetCacheBase( const CacheTracking& tracking )
            : _stats{ tracking.getHandle() }
            , _pUncountedReason{ tracking.getUncountedReason() }
        {
        }
        /** @brief 파생 캐시를 포인터로 들고 지울 수 있어야 합니다. */
        virtual ~AssetCacheBase() = default;

        /** @brief 캐시는 복사하지 않습니다. 레지스트리가 포인터로 듭니다. */
        AssetCacheBase( const AssetCacheBase& ) = delete;
        /** @brief 캐시는 복사 대입하지 않습니다. */
        AssetCacheBase& operator=( const AssetCacheBase& ) = delete;

        /**
         * @brief 이 캐시가 다루는 에셋 종류의 이름입니다("Material" · "Texture" · "Prefab").
         * @details 진단 로그와 `AssetManager::findAssetCache` 의 키입니다. 사람이 읽는 이름이지
         *          확장자가 아닙니다. 확장자는 에디터의 `EditorAssetTypeRegistry` 가 압니다.
         */
        virtual const utf8* getAssetKindName() const = 0;

        /** @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다. */
        virtual bool isCached( string_view relativePath ) const = 0;

        /** @brief 지금 들고 있는 항목 수입니다. */
        virtual size_t getCachedCount() const = 0;

        /**
         * @brief 지금 쥔 에셋마다 한 줄(`AssetMemoryEntry`)을 @p outListEntry 뒤에 덧붙입니다. 어느 스레드에서 불러도 됩니다(캐시가 자기 잠금을 잠깐 잡는다).
         * @details 순수 가상이라 새 캐시는 이 질의를 빠뜨리고 지을 수 없다 — 에셋 표에 줄이 없는 캐시가 생기지 않는다(`getCachedCount` 와 같은 진단 계약).
         *          스스로 데이터를 쥐지 않는 어댑터(데이터는 서비스가 쥔다)는 아무것도 덧붙이지 않는다 — 그 몫은 메모리 태그 표에 있다.
         */
        virtual void collectMemoryEntries( vector<AssetMemoryEntry>& outListEntry ) const = 0;

        /**
         * @brief @p relativePath 가 이 캐시가 다루는 종류의 경로면 로드를 걸고(워커), 그 에셋을 붙드는 토큰과 로드를 돌려줍니다. 이 캐시의 종류가 아니면 빈 토큰입니다.
         * @details 캐시가 약한 참조인 종류(메시 · 프리팹)는 미리 읽은 에셋이 쓰기 전에 사라질 수 있다 — 토큰이 사는 동안 남는다(`AssetPinSet`, 언리얼
         *          `FStreamableHandle` 의 유지). 로드가 실패해도 토큰은 나온다(실패는 그 종류의 로드가 알린다). 기본은 고정하지 않는 캐시다 —
         *          스스로 수명을 쥐는 표(오디오 · 현지화 다시 읽기)와 디바이스가 있어야 요청하는 텍스처가 그렇다(텍스처는 그것을 쓰는 머티리얼의 고정이 붙든다).
         * @warning 토큰은 캐시(곧 `AssetManager`)보다 먼저 놓습니다.
         */
        virtual AssetPin pinAsset( string_view relativePath )
        {
            (void)relativePath;
            return {};
        }

        /** @brief 캐시를 통째로 비웁니다(종료 · 재초기화). */
        virtual void clear() = 0;

        /**
         * @brief 지금 쥔 @p relativePath 를 다시 읽습니다 — 합성 문맥(로드 축 태그 · 덮어쓰기 층)이 바뀌어 같은 파일이 다른 결과가 됐다(`ResourceComposition`). 다시 읽었으면 true 입니다.
         * @details 핫 리로드(`AssetCacheReloadFunction`, 개발 전용)와 달리 배포 구성에도 있다 — 품질 · 언어를 바꾸는 것은 플레이어다. 기본은 다시 읽지 않는 캐시다:
         *          GPU 수명 · 쓰는 쪽의 수정이 있는 캐시(텍스처 · 머티리얼 · 메시 · 프리팹)는 다음 읽기가 새 결과를 본다. 공유 캐시(`SharedAssetCache`)는 새 판을 넣는다(`SharedAssetRef` 로 쥔 쪽이 새 판을 본다).
         */
        virtual bool reloadComposed( string_view relativePath )
        {
            (void)relativePath;
            return false;
        }

        /**
         * @brief 프레임 경계(게임 스레드, 틱 밖)에서 다시 읽기 정리가 남긴 옛 판을 놓습니다(`SharedAssetTable::collectRetired`). 놓은 수를 돌려줍니다. 기본은 판을 남기지 않는 캐시다.
         */
        virtual size_t collectRetiredVersions() { return 0; }

        /** @brief 계측 칸을 올린 캐시인지 반환합니다. false 면 `[CacheStats]` 가 `n/a` 로 찍습니다. */
        bool isCounted() const { return _pUncountedReason == nullptr; }
        /** @brief 세지 않는 사유입니다(`isCounted` 면 nullptr). */
        const utf8* getUncountedReason() const { return _pUncountedReason; }

    protected:
        /** @brief 이 캐시의 계측 칸입니다. 조회가 적중이면 `recordHit`, 새로 읽거나 지으면 `recordMiss`, `clear` 에서 `recordClear` 를 부릅니다. 세지 않는 캐시는 빈 핸들이라 기록이 아무 일도 하지 않습니다. */
        const CacheStatsHandle& getCacheStats() const { return _stats; }

    private:
        CacheStatsHandle _stats;
        const utf8*      _pUncountedReason;
    };
    /**
     * @brief 캐시 하나의 핫 리로드 함수입니다 — 캐시에 있으면 디스크에서 다시 읽습니다(에디터 · 개발 도구). 런타임 계약(`AssetCacheBase`)에는 없다.
     * @details 캐시를 등록할 때 같이 넘긴다(`SW_BUILT_IN_ASSET_CACHE` · `AssetManager::registerAssetCache`). 배포 빌드는 `nullptr` 이라 리로드 본문이
     *          인스턴스화되지도 링크되지도 않는다(템플릿이 아닌 캐시의 본문은 개발 전용 `<캐시>Reload.cpp` 다). 디바이스는 받지 않는다 — GPU 자원을 가진
     *          캐시(텍스처 · 머티리얼 · 메시)는 엔진이 붙인 디바이스(`bindDevice`, `SceneRHISubsystem` 이 RHI 를 세울 때 붙이고 내릴 때 뗀다)를 쓴다(언리얼 리임포트도
     *          RHI 를 인자로 받지 않는다). 예전에는 부르는 쪽(에디터)이 디바이스를 찾아 캐시 여덟의 인자로 관통시켰다.
     *          결과(`ReloadResult`)를 돌려준다 — 예전에는 `void` 라 파싱이 실패해도 부르는 쪽이 성공으로 적었다. 실패면 캐시는 이전 판을 지킨다(Reloaded · NotTracked · Failed).
     */
    using AssetCacheReloadFunction = ReloadResult ( * )( AssetCacheBase& cache, const AssetID& assetID );
} // namespace sw
