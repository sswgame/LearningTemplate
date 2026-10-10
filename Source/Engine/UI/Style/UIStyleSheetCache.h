/**
 * @file UIStyleSheetCache.h
 * @brief 읽어 둔 스타일 시트를 경로로 나눠 주는 캐시입니다(에셋 종류 `UIStyleSheet`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Document/UITextAssetCache.h"

namespace sw
{
    struct UIStyleSheetAsset;

    /**
     * @class UIStyleSheetCache
     * @brief 스타일 시트(`*.uistyle.xml`)를 경로로 들고 나눠 줍니다. `UISystem` 이 소유하고 기동 단계 `UI` 가 에셋 캐시 등록부에 올립니다.
     * @details 찾는 순서 · 실패 · 다시 읽기 규칙은 `UITextAssetCache` 입니다. 게임 스레드만.
     */
    class SW_API UIStyleSheetCache final : public UITextAssetCache
    {
    public:
        UIStyleSheetCache();
        ~UIStyleSheetCache() override;

        /** @brief 시트를 나눠 받습니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. */
        shared_ptr<const UIStyleSheetAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 시트 글을 경로에 겁니다(시험). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemorySheet( string_view path, string_view text ) { registerMemoryText( path, text ); }
    };
} // namespace sw
