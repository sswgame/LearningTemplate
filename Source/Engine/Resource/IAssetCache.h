/**
 * @file IAssetCache.h
 * @brief 경로를 키로 에셋을 들고 있는 캐시의 공통 계약 — 에셋 종류를 늘리는 자리.
 *
 * [왜 필요한가]
 * 캐시가 셋(Material · Texture · Prefab)인데 그것을 **아는 코드가 이름으로 셋을 적고 있었다.**
 * `ResourceManager::shutdown` 은 머티리얼과 텍스처만 비우고 프리팹을 잊었고(재초기화 뒤에도 옛
 * 프리팹이 남았다), 새 에셋 종류를 하나 더하려면 캐시 클래스 · 매니저 멤버 · 게터 · 종료 경로를
 * 손으로 같이 고쳐야 했다. **목록이 여럿이면 한쪽만 늘어난다** — 이 저장소가 린트·픽서·백엔드에서
 * 이미 같은 결론에 도달한 자리다.
 *
 * 상용 엔진도 같은 모양이다. 언리얼의 `FStreamableManager`·고도의 `ResourceFormatLoader`·유니티의
 * `AssetDatabase` 는 전부 "종류마다 등록하고, 공통 동작은 등록부를 훑는다". 여기서는 그 최소한만
 * 가져온다 — **등록부를 훑어서 되는 일**(종료 · 진단 · 재초기화)만 이 인터페이스로 하고, 종류마다
 * 다른 것(무엇을 어떻게 읽어 오는가)은 구체 캐시가 그대로 갖는다.
 *
 * @note 수명은 `ResourceManager` 가 쥔다. 등록부는 **소유하지 않는 포인터**만 들고 있으므로,
 *       등록한 캐시는 매니저보다 오래 살아야 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class IAssetCache
     * @brief 경로 키 에셋 캐시가 공통으로 답해야 하는 것들입니다.
     */
    class SW_API IAssetCache
    {
    public:
        /** @brief 빈 인터페이스. 구현이 상태를 갖습니다. */
        IAssetCache() = default;
        /** @brief 파생 캐시를 포인터로 들고 지울 수 있어야 합니다. */
        virtual ~IAssetCache() = default;

        /** @brief 캐시는 복사하지 않습니다 - 등록부가 포인터로 든다. */
        IAssetCache( const IAssetCache& ) = delete;
        /** @brief 캐시는 복사 대입하지 않습니다. */
        IAssetCache& operator=( const IAssetCache& ) = delete;

        /**
         * @brief 이 캐시가 다루는 에셋 종류의 이름입니다 ("Material" · "Texture" · "Prefab").
         * @details 진단 로그와 `ResourceManager::findAssetCache` 의 키다. 사람이 읽는 이름이지
         *          확장자가 아니다 — 확장자는 에디터의 `EditorAssetTypeRegistry` 가 안다.
         */
        virtual const utf8* getAssetKindName() const = 0;

        /** @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다. */
        virtual bool isCached( string_view relativePath ) const = 0;

        /**
         * @brief 캐시에 있으면 디스크에서 다시 읽습니다 (에디터 핫리로드).
         * @param pDevice GPU 자원을 다시 올려야 하는 캐시가 씁니다. 필요 없는 캐시는 무시합니다.
         * @details **디바이스를 인자로 받는다.** 캐시가 마지막으로 본 디바이스를 들고 있으면
         *          백엔드를 바꾼 뒤 그 포인터가 죽은 디바이스를 가리킨다 — `MaterialCache` 가
         *          실제로 그렇게 들고 있었다.
         */
        virtual void reload( string_view relativePath, IRHIDevice* pDevice ) = 0;

        /** @brief 지금 들고 있는 항목 수입니다. */
        virtual size_t getCachedCount() const = 0;

        /** @brief 캐시를 통째로 비웁니다 (종료 · 재초기화). */
        virtual void clear() = 0;
    };
} // namespace sw
