/**
 * @file UITextAssetCache.h
 * @brief 텍스트로 읽어 파싱하는 UI 에셋(문서, 스타일 시트)을 경로별로 보관하는 캐시의 공통 구현입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/IAssetCache.h"
#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    /** @brief UI 에셋(문서, 스타일 시트)을 다시 읽어 바꿨다는 알림입니다(인자는 정규화한 경로). `UISystem` 이 받아 화면을 다시 만들거나 맞춥니다. */
    using UIAssetReloadedDelegate = Delegate<void( string_view )>;

    /**
     * @brief 텍스트 하나를 에셋으로 파싱합니다. 실패하면 nullptr 를 반환하고 @p outError 에 `<경로>:<줄>: <이유>` 를 둡니다.
     * @details 에셋 타입은 파생 캐시만 안다. 공통 구현은 `shared_ptr<const void>` 로 보관하고, 파생 캐시가 자기 타입으로 되돌린다.
     */
    using UITextAssetParseFunction = shared_ptr<const void> ( * )( string_view text, string_view path, string& outError );
} // namespace sw

namespace sw
{
    /**
     * @class UITextAssetCache
     * @brief 경로에서 파싱한 에셋을 찾아 나눠 주는 공통 구현입니다(`UIDocumentCache` 와 `UIStyleSheetCache` 가 파생합니다).
     * @details 텍스트는 메모리 텍스트(`registerMemoryText`, 테스트와 도구), 리소스(팩, `Content/`), 절대 경로 순서로 찾는다.
     *          실패는 캐시에 남기지 않는다(고치면 다음 요청이 다시 읽는다). 다시 읽기(`reload`)가 실패하면 기존 에셋을 그대로 두고 오류를 남긴다
     *          (실패가 화면을 지우지 않으며, 현지화 `reloadChangedFile` 과 같은 규칙이다). 게임 스레드에서만 쓴다.
     */
    class SW_API UITextAssetCache : public IAssetCache
    {
    public:
        ~UITextAssetCache() override;

        /**
         * @brief 다시 읽어 바꾼 에셋을 알릴 함수를 겁니다(`UISystem` 이 걸며, 그 에셋을 쓰는 화면을 다시 만들거나 맞춥니다).
         * @details 캐시는 `UISystem::getDocumentCache` 로 누구나 닿으므로 칸 주인을 구독으로 정한다 — 이미 걸려 있으면 단언, 구독이 풀리면 빈다.
         */
        [[nodiscard]] Subscription bindReloadedHandler( UIAssetReloadedDelegate handler ) { return _reloadedHandler.bind( std::move( handler ) ); }

        const utf8* getAssetKindName() const override { return _pAssetKindName; }
        bool        isCached( string_view relativePath ) const override;
        /** @brief 에셋을 다시 읽어 파싱합니다(에디터 핫 리로드). 되면 바꾸고 알림을 부릅니다. 디바이스는 쓰지 않습니다. */
        void   reload( const AssetID& assetID, IRHIDevice* pDevice );
        size_t getCachedCount() const override { return _mapPathToAsset.size(); }
        /** @brief 읽어 둔 에셋과 메모리 글을 모두 비웁니다. */
        void clear() override;

    protected:
        /**
         * @param pAssetKindName 레지스트리의 종류 이름("UIDocument")입니다.
         * @param pAssetLabel    오류와 로그 텍스트에 쓰는 이름("UI document")입니다.
         * @param pParse         텍스트를 에셋으로 바꾸는 파서입니다.
         */
        UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse );

        /** @brief 에셋을 나눠 줍니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 를 반환하고 @p outError 를 채웁니다. */
        shared_ptr<const void> findOrLoadAsset( string_view path, string& outError );
        /** @brief 파일 대신 읽을 텍스트를 경로에 등록합니다. 이미 읽어 둔 그 경로의 에셋은 버립니다. */
        void registerMemoryText( string_view path, string_view text );
        /** @brief 텍스트를 읽어 파싱한 횟수입니다. */
        uint32 getParseCount() const { return _parseCount; }

    private:
        /** @brief 텍스트를 찾습니다(메모리, 리소스, 절대 경로 순서). */
        [[nodiscard]] bool readText( const string& key, const AssetID& assetID, string& outText ) const;

    private:
        unordered_map<string, shared_ptr<const void>> _mapPathToAsset;      ///< 정규화한 경로에서 파싱한 에셋을 찾는 맵입니다.
        unordered_map<string, string>                 _mapPathToMemoryText; ///< 정규화한 경로에서 메모리 텍스트를 찾는 맵입니다.
        DelegateSlot<UIAssetReloadedDelegate>         _reloadedHandler;
        const utf8*                                   _pAssetKindName;
        const utf8*                                   _pAssetLabel;
        UITextAssetParseFunction                      _pParse;
        uint32                                        _parseCount;
    };
} // namespace sw
