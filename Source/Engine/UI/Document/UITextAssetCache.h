/**
 * @file UITextAssetCache.h
 * @brief 텍스트로 읽어 파싱하는 UI 에셋(문서, 스타일 시트)을 경로별로 보관하는 캐시의 공통 구현입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CacheStats.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

namespace sw
{
    struct ResourceContext;

    /** @brief UI 에셋(문서, 스타일 시트)을 다시 읽어 바꿨다는 알림입니다(인자는 정규화한 경로). 화면 스택 · 오프스크린 화면 · 테마가 받아 화면을 다시 만들거나 맞춥니다. */
    using UIAssetReloadedDelegate = Delegate<void( string_view )>;
    /** @brief 다시 읽기 알림의 칸입니다(받는 쪽이 여럿이다 — 화면 스택 `UISystem` 과 오프스크린 화면 `UIOffscreenScreens`). */
    using UIAssetReloadedEvent = MulticastDelegate<void( string_view )>;

    /**
     * @brief 텍스트 하나를 에셋으로 파싱합니다. 실패하면 오류(`<경로>:<줄>: <이유>`)입니다.
     * @details 에셋 타입은 가진 서브시스템만 안다. 공통 구현은 `shared_ptr<const void>` 로 보관하고, 가진 서브시스템이 자기 타입으로 되돌린다.
     *          @p context 는 캐시가 만든 합성 문맥이다(리소스 id = 경로, 변형이면 바꿔 끼운 태그) — 파서는 글을 이 문맥으로 합쳐 읽는다(`XMLDocument::parseComposed`).
     */
    using UITextAssetParseFunction = expected<shared_ptr<const void>> ( * )( string_view text, string_view path, const ResourceContext& context );
} // namespace sw

namespace sw
{
    /**
     * @class UITextAssetCache
     * @brief 경로에서 파싱한 에셋을 찾아 나눠 주는 공통 구현입니다(서브시스템 `UIDocumentCache` 와 `UIStyleSheetCache` 가 값 멤버로 가집니다).
     * @details 텍스트는 메모리 텍스트(`registerMemoryText`, 테스트와 도구), 리소스(팩, `Content/`), 절대 경로 순서로 찾는다.
     *          실패는 캐시에 남기지 않는다(고치면 다음 요청이 다시 읽는다). 다시 읽기(`reload`)가 실패하면 기존 에셋을 그대로 두고 오류를 남긴다
     *          (실패가 화면을 지우지 않으며, 현지화 `reloadChangedFile` 과 같은 규칙이다). 게임 스레드에서만 쓴다.
     *          **변형**: 같은 경로를 바꿔 끼운 태그(`ResourceContext::withTagOverride` — 에디터 UI 미리보기의 기기 견본)로 합친 에셋은 경로 + 태그를 열쇠로 따로 든다.
     *          글은 하나라 핫 리로드 · 태그 다시 읽기(`reload`)는 그 경로의 엔진 판과 변형을 모두 다시 읽고 알림은 한 번이다.
     *          언리얼 위젯 디자이너가 미리보기 기기마다 위젯을 따로 짓는 것과 같은 자리다(기억) — 게임 UI 가 쓰는 엔진 판은 그대로다.
     */
    class SW_API UITextAssetCache final : public AssetCacheBase
    {
    public:
        /**
         * @param pAssetKindName 레지스트리의 종류 이름("UIDocument")입니다.
         * @param pAssetLabel    오류와 로그 텍스트에 쓰는 이름("UI document")입니다.
         * @param pParse         텍스트를 에셋으로 바꾸는 파서입니다.
         */
        UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse );
        ~UITextAssetCache() override;

        /**
         * @brief 다시 읽어 바꾼 에셋을 알리는 칸입니다(`add` 가 돌려준 구독을 든다). 그 에셋을 쓰는 화면을 가진 서비스(화면 스택 · 오프스크린 화면)가 저마다 건다.
         * @details 언리얼 `FCoreUObjectDelegates::OnObjectsReplaced` 처럼 받는 쪽이 여럿이라 한 칸 슬롯이 아니라 다중 칸이다.
         */
        UIAssetReloadedEvent& onReloaded() { return _onReloaded; }

        const utf8* getAssetKindName() const override { return _pAssetKindName; }
        bool        isCached( string_view relativePath ) const override;
        /** @brief 에셋(엔진 판과 변형)을 다시 읽어 파싱합니다(에디터 핫 리로드 · 태그 다시 읽기). 하나라도 되면 바꾸고 알림을 한 번 부릅니다. 디바이스는 쓰지 않습니다. */
        void reload( const AssetID& assetID );
        /** @brief 리소스 태그 · 층이 바뀌어 다시 읽습니다(`AssetManager` 의 합성 다시 읽기 — 배포 구성에도 있다). 들고 있지 않으면 false 입니다. */
        bool   reloadComposed( string_view relativePath ) override;
        size_t getCachedCount() const override { return _mapKeyToEntry.size(); }
        /** @brief 읽어 둔 에셋과 메모리 글을 모두 비웁니다. */
        void clear() override;

        /**
         * @brief 에셋을 나눠 줍니다. 처음이면 읽고 파싱합니다. 실패하면 오류입니다.
         * @param tagOverride 엔진 태그에서 바꿔 끼울 축입니다(빈 집합이면 엔진 판 — 게임 UI 가 쓰는 것). 비지 않으면 변형으로 따로 듭니다.
         */
        expected<shared_ptr<const void>> findOrLoadAsset( string_view path, const ResourceTagSet& tagOverride );
        /** @brief 파일 대신 읽을 텍스트를 경로에 등록합니다. 이미 읽어 둔 그 경로의 에셋은 버립니다. */
        void registerMemoryText( string_view path, string_view text );
        /** @brief 텍스트를 읽어 파싱한 횟수입니다. */
        uint32 getParseCount() const { return _parseCount; }

    private:
        /** @brief 읽어 둔 에셋 하나입니다(엔진 판이거나 변형). */
        struct Entry
        {
            string                 _path{};        ///< 정규화한 경로
            ResourceTagSet         _tagOverride{}; ///< 바꿔 끼운 태그(엔진 판이면 빈 집합)
            shared_ptr<const void> _asset{};
        };

        /** @brief 정규화한 경로 @p path 와 바꿔 끼운 태그의 열쇠입니다(엔진 판은 경로 그대로). */
        [[nodiscard]] static string makeKey( const string& path, const ResourceTagSet& tagOverride );
        /** @brief 텍스트를 찾습니다(메모리, 리소스, 절대 경로 순서). */
        [[nodiscard]] bool readText( const string& key, const AssetID& assetID, string& outText ) const;
        /** @brief @p text 를 경로 @p path · 바꿔 끼운 태그 @p tagOverride 의 합성 문맥으로 파싱합니다. */
        [[nodiscard]] expected<shared_ptr<const void>> parseText( string_view text, string_view path, const ResourceTagSet& tagOverride );

    private:
        unordered_map<string, Entry>  _mapKeyToEntry;       ///< 열쇠(`makeKey`)에서 파싱한 에셋을 찾는 맵입니다.
        unordered_map<string, string> _mapPathToMemoryText; ///< 정규화한 경로에서 메모리 텍스트를 찾는 맵입니다.
        UIAssetReloadedEvent          _onReloaded;
        const utf8*                   _pAssetKindName;
        const utf8*                   _pAssetLabel;
        UITextAssetParseFunction      _pParse;
        uint32                        _parseCount;
    };
} // namespace sw
