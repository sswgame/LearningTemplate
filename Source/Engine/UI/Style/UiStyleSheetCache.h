/**
 * @file UiStyleSheetCache.h
 * @brief 읽어 둔 스타일 시트를 경로로 나눠 주는 캐시입니다(에셋 종류 `UiStyleSheet`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Document/UiTextAssetCache.h"

namespace sw
{
    struct UiStyleSheetAsset;

    /**
     * @class UiStyleSheetCache
     * @brief 스타일 시트(`*.uistyle.xml`)를 경로로 들고 나눠 줍니다. `UiSystem` 이 소유하고 기동 단계 `Ui` 가 에셋 캐시 등록부에 올립니다.
     * @details 찾는 순서 · 실패 · 다시 읽기 규칙은 `UiTextAssetCache` 입니다. 게임 스레드만.
     */
    class SW_API UiStyleSheetCache final : public UiTextAssetCache
    {
    public:
        UiStyleSheetCache();
        ~UiStyleSheetCache() override;

        /** @brief 시트를 나눠 받습니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. */
        shared_ptr<const UiStyleSheetAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 시트 글을 경로에 겁니다(시험). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemorySheet( string_view path, string_view text ) { registerMemoryText( path, text ); }
    };
} // namespace sw
