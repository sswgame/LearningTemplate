/**
 * @file UiDocumentCache.h
 * @brief 읽어 둔 UI 문서를 경로로 나눠 주는 캐시입니다(에셋 종류 `UiDocument`).
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
    struct UiDocumentAsset;

    /**
     * @class UiDocumentCache
     * @brief UI 문서(`*.ui.xml`)를 경로로 들고 파싱한 노드 트리를 나눠 줍니다 — 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details `UiSystem` 이 소유하고, 기동 단계 `Ui` 가 에셋 캐시 등록부(`AssetManager`)에 올립니다(종료 · 진단 · 핫 리로드가 다른 캐시와 같은 길).
     *          글은 먼저 메모리 문서(`registerMemoryDocument` — 시험 · 도구), 다음 리소스(팩 · `Resource/`), 마지막 절대 경로 순서로 찾습니다.
     *          게임 스레드만.
     */
    class SW_API UiDocumentCache final : public IAssetCache
    {
    public:
        UiDocumentCache();
        ~UiDocumentCache() override;

        /**
         * @brief 문서를 나눠 받습니다. 처음이면 읽고 파싱합니다.
         * @return 읽지 못하거나 파싱이 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. 실패는 캐시에 남기지 않습니다(고치면 다음 요청이 다시 읽는다).
         */
        shared_ptr<const UiDocumentAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 문서 글을 경로에 겁니다(시험 · 도구). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemoryDocument( string_view path, string_view text );
        /** @brief 문서 글을 읽어 파싱한 횟수입니다(캐시가 파일을 한 번만 읽는지 시험이 본다). */
        uint32 getParseCount() const { return _parseCount; }

        /** @brief 등록부의 종류 이름("UiDocument")입니다. */
        const utf8* getAssetKindName() const override { return "UiDocument"; }
        bool        isCached( string_view relativePath ) const override;
        /** @brief 읽어 둔 문서를 버리고 다시 읽습니다. 디바이스는 쓰지 않습니다. */
        void   reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t getCachedCount() const override { return _mapPathToDocument.size(); }
        /** @brief 읽어 둔 문서와 메모리 문서를 모두 비웁니다. */
        void clear() override;

    private:
        /** @brief 문서 글을 찾습니다(메모리 → 리소스 → 절대 경로). */
        [[nodiscard]] bool readDocumentText( const string& key, string_view path, string& outText ) const;

    private:
        unordered_map<string, shared_ptr<const UiDocumentAsset>> _mapPathToDocument;   ///< 정규화한 경로 → 파싱한 문서
        unordered_map<string, string>                            _mapPathToMemoryText; ///< 정규화한 경로 → 메모리 문서 글
        uint32                                                   _parseCount;
    };
} // namespace sw
