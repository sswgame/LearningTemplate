/**
 * @file UITextAssetCache.h
 * @brief 글로 읽어 파싱하는 UI 에셋(문서 · 스타일 시트)을 경로로 들고 있는 캐시의 공통 몸체입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    /** @brief UI 에셋(문서 · 스타일 시트)을 다시 읽어 바꿨다는 알림입니다(인자는 정규화한 경로). `UISystem` 이 받아 화면을 다시 짓거나 맞춘다. */
    using UIAssetReloadedDelegate = Delegate<void( string_view )>;

    /**
     * @brief 글 하나를 에셋으로 파싱합니다. 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다.
     * @details 에셋 타입은 파생 캐시만 압니다 — 공통 몸체는 `shared_ptr<const void>` 로 들고, 파생 캐시가 자기 타입으로 되돌립니다.
     */
    using UITextAssetParseFunction = shared_ptr<const void> ( * )( string_view text, string_view path, string& outError );
} // namespace sw

namespace sw
{
    /**
     * @class UITextAssetCache
     * @brief 경로 → 파싱한 에셋을 들고 나눠 주는 공통 몸체입니다(`UIDocumentCache` · `UIStyleSheetCache` 가 파생).
     * @details 글은 먼저 메모리 글(`registerMemoryText` — 시험 · 도구), 다음 리소스(팩 · `Resource/`), 마지막 절대 경로 순서로 찾습니다.
     *          실패는 캐시에 남기지 않습니다(고치면 다음 요청이 다시 읽는다). 다시 읽기(`reload`)가 실패하면 **옛 에셋을 그대로 두고** 오류를 남깁니다
     *          (실패가 화면을 지우지 않는다 — 현지화 `reloadChangedFile` 과 같은 규칙). 게임 스레드만.
     */
    class SW_API UITextAssetCache : public IAssetCache
    {
    public:
        ~UITextAssetCache() override;

        /** @brief 다시 읽어 바꾼 에셋을 알릴 함수를 겁니다(`UISystem` 이 건다 — 그 에셋을 쓰는 화면을 다시 짓거나 맞춘다). */
        void setReloadedHandler( const UIAssetReloadedDelegate& handler ) { _reloadedHandler = handler; }

        const utf8* getAssetKindName() const override { return _pAssetKindName; }
        bool        isCached( string_view relativePath ) const override;
        /** @brief 에셋을 다시 읽어 파싱합니다(에디터 핫 리로드). 되면 바꾸고 알림을 부릅니다. 디바이스는 쓰지 않습니다. */
        void   reload( string_view relativePath, IRHIDevice* pDevice ) override;
        size_t getCachedCount() const override { return _mapPathToAsset.size(); }
        /** @brief 읽어 둔 에셋과 메모리 글을 모두 비웁니다. */
        void clear() override;

    protected:
        /**
         * @param pAssetKindName 등록부의 종류 이름("UIDocument")입니다.
         * @param pAssetLabel    오류 · 로그 글에 쓰는 이름("UI document")입니다.
         * @param pParse         글 → 에셋 파서입니다.
         */
        UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse );

        /** @brief 에셋을 나눠 받습니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 과 @p outError 입니다. */
        shared_ptr<const void> findOrLoadAsset( string_view path, string& outError );
        /** @brief 파일 대신 읽을 글을 경로에 겁니다. 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemoryText( string_view path, string_view text );
        /** @brief 글을 읽어 파싱한 횟수입니다. */
        uint32 getParseCount() const { return _parseCount; }

    private:
        /** @brief 글을 찾습니다(메모리 → 리소스 → 절대 경로). */
        [[nodiscard]] bool readText( const string& key, string_view path, string& outText ) const;

    private:
        unordered_map<string, shared_ptr<const void>> _mapPathToAsset;      ///< 정규화한 경로 → 파싱한 에셋
        unordered_map<string, string>                 _mapPathToMemoryText; ///< 정규화한 경로 → 메모리 글
        UIAssetReloadedDelegate                       _reloadedHandler;
        const utf8*                                   _pAssetKindName;
        const utf8*                                   _pAssetLabel;
        UITextAssetParseFunction                      _pParse;
        uint32                                        _parseCount;
    };
} // namespace sw
