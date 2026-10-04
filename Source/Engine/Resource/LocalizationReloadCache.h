/**
 * @file LocalizationReloadCache.h
 * @brief 로컬라이제이션 파일(원문 표 · 번역 표 · 프로젝트)을 에셋 캐시 등록부에 보이는 창구입니다 — 에디터 핫 리로드가 다른 에셋과 같은 길로 다시 읽게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    /**
     * @class LocalizationReloadCache
     * @brief 상태가 없는 창구입니다. 표는 엔진 서비스 `LocalizationManager` 가 갖고, 이것은 바뀐 파일을 그 매니저에 넘깁니다.
     * @details 티어 때문에 Resource 에 삽니다(`IAssetCache` 를 구현한다 — `SpriteClipCache` 와 같은 자리). `clear()` 는 아무것도 비우지 않습니다 —
     *          캐시 비우기(재초기화)가 화면의 글을 지우면 안 되고, 글의 수명은 매니저가 정합니다.
     */
    class SW_API LocalizationReloadCache final : public IAssetCache
    {
    public:
        /** @brief 등록부의 종류 이름("StringTable")입니다. */
        const utf8* getAssetKindName() const override { return "StringTable"; }
        /** @brief 그 경로가 올린 프로젝트의 파일인지입니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 그 파일이 든 프로젝트를 다시 읽습니다(`LocalizationManager::reloadChangedFile`). 디바이스는 쓰지 않습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 올린 프로젝트 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 아무것도 하지 않습니다(위 설명). */
        void clear() override {}
    };
} // namespace sw
