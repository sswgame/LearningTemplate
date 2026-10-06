/**
 * @file UiStyleSheetCache.h
 * @brief 읽어 둔 스타일 시트를 경로로 나눠 주는 캐시입니다(에셋 종류 `UiStyleSheet`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    struct UiStyleSheetAsset;

    /**
     * @class UiStyleSheetCache
     * @brief 스타일 시트(`*.uistyle.xml`)를 경로로 들고 나눠 줍니다. `UiSystem` 이 소유하고 기동 단계 `Ui` 가 에셋 캐시 등록부에 올립니다.
     * @details 글은 메모리 시트(`registerMemorySheet` — 시험) → 리소스 → 절대 경로 순서로 찾습니다. 실패는 캐시에 남기지 않습니다. 게임 스레드만.
     */
    class SW_API UiStyleSheetCache final : public IAssetCache
    {
    public:
        UiStyleSheetCache();
        ~UiStyleSheetCache() override;

        /** @brief 시트를 나눠 받습니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. */
        shared_ptr<const UiStyleSheetAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 시트 글을 경로에 겁니다(시험). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemorySheet( string_view path, string_view text );

        /** @brief 등록부의 종류 이름("UiStyleSheet")입니다. */
        const utf8* getAssetKindName() const override { return "UiStyleSheet"; }
        bool        isCached( string_view relativePath ) const override;
        /** @brief 읽어 둔 시트를 버립니다(다음 요청이 다시 읽는다). 디바이스는 쓰지 않습니다. */
        void   reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t getCachedCount() const override { return _mapPathToSheet.size(); }
        void   clear() override;

    private:
        unordered_map<string, shared_ptr<const UiStyleSheetAsset>> _mapPathToSheet;
        unordered_map<string, string>                              _mapPathToMemoryText;
    };
} // namespace sw
