/**
 * @file UIDocumentCache.h
 * @brief 읽어 둔 UI 문서를 경로로 나눠 주는 캐시입니다(에셋 종류 `UIDocument`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Document/UITextAssetCache.h"

namespace sw
{
    struct UIDocumentAsset;

    /**
     * @class UIDocumentCache
     * @brief UI 문서(`*.ui.xml`)를 경로로 들고 파싱한 노드 트리를 나눠 줍니다 — 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details `UISystem` 이 소유하고, 기동 단계 `UI` 가 에셋 캐시 등록부(`AssetManager`)에 올립니다(종료 · 진단 · 핫 리로드가 다른 캐시와 같은 길).
     *          찾는 순서 · 실패 · 다시 읽기 규칙은 `UITextAssetCache` 입니다. 게임 스레드만.
     */
    class SW_API UIDocumentCache final : public UITextAssetCache
    {
    public:
        UIDocumentCache();
        ~UIDocumentCache() override;

        /**
         * @brief 문서를 나눠 받습니다. 처음이면 읽고 파싱합니다.
         * @return 읽지 못하거나 파싱이 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. 실패는 캐시에 남기지 않습니다(고치면 다음 요청이 다시 읽는다).
         */
        shared_ptr<const UIDocumentAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 문서 글을 경로에 겁니다(시험 · 도구). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemoryDocument( string_view path, string_view text ) { registerMemoryText( path, text ); }
        /** @brief 문서 글을 읽어 파싱한 횟수입니다(캐시가 파일을 한 번만 읽는지 시험이 본다). */
        uint32 getParseCount() const { return UITextAssetCache::getParseCount(); }
    };
} // namespace sw
