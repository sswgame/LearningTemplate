/**
 * @file UIDocumentCache.h
 * @brief 읽어 둔 UI 문서를 경로별로 나눠 주는 캐시입니다(에셋 종류 `UIDocument`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/UI/Document/UITextAssetCache.h"

namespace sw
{
    struct UIDocumentAsset;

    /**
     * @class UIDocumentCache
     * @brief UI 문서(`*.ui.xml`)를 경로별로 보관하고 파싱한 노드 트리를 나눠 줍니다. 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details 엔진 범위 서브시스템 `UIDocuments` 다(`engine::getSubsystem<UIDocumentCache>()`) — 화면 스택(`UISystem`) · 오프스크린 화면 · 알림 · 옵션 메뉴 · 에디터 미리보기가
     *          같은 캐시를 읽으므로 화면 스택의 값 멤버가 아니다(언리얼 에셋 레지스트리 · Godot `ResourceLoader` 캐시처럼 화면과 수명이 따로다).
     *          기동이 에셋 캐시 레지스트리(`AssetManager`)에 등록한다(종료, 진단, 핫 리로드가 다른 캐시와 같은 경로를 쓴다).
     *          찾는 순서, 실패, 다시 읽기 규칙은 `UITextAssetCache` 를 따른다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIDocumentCache final : public UITextAssetCache, public ISubsystem
    {
    public:
        UIDocumentCache();
        ~UIDocumentCache() override;

        // ISubsystem — 기동이 에셋 캐시 레지스트리에 올리고 종료가 내린다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIDocuments";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 에셋 캐시 레지스트리(`Asset`)에 오른다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 문서를 나눠 받습니다. 처음이면 읽고 파싱합니다.
         * @return 읽지 못하거나 파싱이 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. 실패는 캐시에 남기지 않습니다(고치면 다음 요청이 다시 읽는다).
         */
        shared_ptr<const UIDocumentAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 문서 텍스트를 경로에 등록합니다(테스트와 도구용). 이미 읽어 둔 그 경로의 문서는 버립니다. */
        void registerMemoryDocument( string_view path, string_view text ) { registerMemoryText( path, text ); }
        /** @brief 문서 글을 읽어 파싱한 횟수입니다(캐시가 파일을 한 번만 읽는지 테스트가 본다). */
        uint32 getParseCount() const { return UITextAssetCache::getParseCount(); }

    private:
        Subscription _assetCacheRegistration; ///< 에셋 캐시 레지스트리 등록 구독(기동이 받고 종료가 놓는다)
    };
} // namespace sw
