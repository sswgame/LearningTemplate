/**
 * @file IAssetCache.h
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

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class CacheTracking
     * @brief 캐시가 계측(`CacheStats`)에 어떻게 서는지의 선언입니다. `IAssetCache` 의 생성자가 이 값을 요구해, 계측을 정하지 않은 캐시는 지을 수 없습니다.
     * @details 캐시마다 "조회 자리에서 적중 · 실패를 센다" 는 약속을 손으로 지키면 새 캐시가 잊습니다(`[CacheStats]` 에 줄이 없는 캐시가 생김).
     *          그래서 둘 중 하나만 쓸 수 있게 한다 — 센다(`counted`, 종류 이름의 칸을 올린다) 또는 세지 않는 사유를 적는다(`uncounted`, 조회가 없는 어댑터).
     *          칸은 종류 이름이 키이고 프로세스 끝까지 살아, 엔진을 다시 세워 캐시를 다시 지어도 같은 칸을 이어 센다.
     */
    class CacheTracking final
    {
    public:
        /** @brief 종류 이름 @p kindName 의 칸을 올리거나 이어 받습니다. 캐시는 조회 자리에서 `IAssetCache::getCacheStats` 로 센다. */
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

        CacheStatsHandle _handle;
        const utf8*      _pUncountedReason;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAssetCache
     * @brief 경로 키 에셋 캐시가 공통으로 답해야 하는 것들입니다.
     */
    class SW_API IAssetCache
    {
    public:
        /** @brief 계측 선언(`CacheTracking`)을 받아 만듭니다. 선언 없이는 지을 수 없다. 나머지 상태는 구현이 갖습니다. */
        explicit IAssetCache( const CacheTracking& tracking )
            : _stats{ tracking.getHandle() }
            , _pUncountedReason{ tracking.getUncountedReason() }
        {
        }
        /** @brief 파생 캐시를 포인터로 들고 지울 수 있어야 합니다. */
        virtual ~IAssetCache() = default;

        /** @brief 캐시는 복사하지 않습니다. 레지스트리가 포인터로 듭니다. */
        IAssetCache( const IAssetCache& ) = delete;
        /** @brief 캐시는 복사 대입하지 않습니다. */
        IAssetCache& operator=( const IAssetCache& ) = delete;

        /**
         * @brief 이 캐시가 다루는 에셋 종류의 이름입니다("Material" · "Texture" · "Prefab").
         * @details 진단 로그와 `AssetManager::findAssetCache` 의 키입니다. 사람이 읽는 이름이지
         *          확장자가 아닙니다. 확장자는 에디터의 `EditorAssetTypeRegistry` 가 압니다.
         */
        virtual const utf8* getAssetKindName() const = 0;

        /** @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다. */
        virtual bool isCached( string_view relativePath ) const = 0;

        /**
         * @brief 캐시에 있으면 디스크에서 다시 읽습니다(에디터 핫 리로드).
         * @param pDevice GPU 자원을 다시 올려야 하는 캐시가 씁니다. 필요 없는 캐시는 무시합니다.
         * @details **디바이스를 인자로 받습니다.** 캐시가 마지막으로 본 디바이스를 들고 있으면
         *          백엔드를 바꾼 뒤 그 포인터가 사라진 디바이스를 가리킵니다.
         */
        virtual void reload( const AssetID& assetID, IRHIDevice* pDevice ) = 0;

        /** @brief 지금 들고 있는 항목 수입니다. */
        virtual size_t getCachedCount() const = 0;

        /**
         * @brief @p relativePath 가 이 캐시가 다루는 종류의 경로면 로드를 걸고(워커), 그 에셋을 붙드는 토큰을 돌려줍니다. 이 캐시의 종류가 아니면 nullptr 입니다.
         * @details 캐시가 약한 참조인 종류(메시 · 프리팹)는 미리 읽은 에셋이 쓰기 전에 사라질 수 있다 — 토큰이 사는 동안 남는다(`AssetPinSet`, 언리얼
         *          `FStreamableHandle` 의 유지). 로드가 실패해도 토큰은 나온다(실패는 그 종류의 로드가 알린다). 기본은 고정하지 않는 캐시다 —
         *          스스로 수명을 쥐는 표(오디오 · 현지화 다시 읽기)와 디바이스가 있어야 요청하는 텍스처가 그렇다(텍스처는 그것을 쓰는 머티리얼의 고정이 붙든다).
         * @warning 토큰은 캐시(곧 `AssetManager`)보다 먼저 놓습니다.
         */
        virtual shared_ptr<const void> pinAsset( string_view relativePath )
        {
            (void)relativePath;
            return {};
        }

        /** @brief 캐시를 통째로 비웁니다(종료 · 재초기화). */
        virtual void clear() = 0;

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
} // namespace sw
