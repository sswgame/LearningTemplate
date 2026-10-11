/**
 * @file LocalizationReloadCache.h
 * @brief 로컬라이제이션 파일(원문 테이블, 번역 테이블, 프로젝트)을 에셋 캐시 레지스트리에 노출하는 어댑터입니다. 에디터 핫 리로드가 다른 에셋과 같은 경로로 다시 읽게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    /**
     * @class LocalizationReloadCache
     * @brief 상태가 없는 어댑터입니다. 테이블은 엔진 서비스 `LocalizationManager` 가 가지고, 이 클래스는 바뀐 파일을 그 매니저에 넘깁니다.
     * @details 티어 규칙 때문에 Resource 에 둔다(`IAssetCache` 를 구현하며 `SpriteClipCache` 와 같은 위치다). `clear()` 는 아무것도 비우지 않는다.
     *          캐시 비우기(재초기화)가 화면의 텍스트를 지우면 안 되고, 텍스트의 수명은 매니저가 정하기 때문이다.
     */
    class SW_API LocalizationReloadCache final : public IAssetCache
    {
    public:
        /** @brief 레지스트리의 종류 이름("StringTable")입니다. */
        const utf8* getAssetKindName() const override { return "StringTable"; }
        /** @brief 그 경로가 로드한 프로젝트의 파일이면 true 입니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 그 파일이 든 프로젝트를 다시 읽습니다(`LocalizationManager::reloadChangedFile`). 디바이스는 쓰지 않습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 로드한 프로젝트 개수입니다. */
        size_t getCachedCount() const override;
        /** @brief 아무것도 하지 않습니다(위 설명). */
        void clear() override {}
    };
} // namespace sw
