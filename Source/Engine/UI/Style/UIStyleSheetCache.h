/**
 * @file UIStyleSheetCache.h
 * @brief 읽어 둔 스타일 시트를 경로별로 나눠 주는 캐시입니다(에셋 종류 `UIStyleSheet`).
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
    struct UIStyleSheetAsset;

    /**
     * @class UIStyleSheetCache
     * @brief 스타일 시트(`*.uistyle.xml`)를 경로별로 보관하고 나눠 줍니다. 엔진 범위 서브시스템 `UIStyleSheets` 이며 기동이 에셋 캐시 레지스트리에 등록합니다.
     * @details 테마(`UIThemeController`)와 화면 스택이 같이 읽으므로 화면 스택의 값 멤버가 아니다(`UIDocumentCache` 와 같은 자리).
     *          찾는 순서, 실패, 다시 읽기 규칙은 `UITextAssetCache` 를 따른다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIStyleSheetCache final : public UITextAssetCache, public ISubsystem
    {
    public:
        UIStyleSheetCache();
        ~UIStyleSheetCache() override;

        // ISubsystem — 기동이 에셋 캐시 레지스트리에 올리고 종료가 내린다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIStyleSheets";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 에셋 캐시 레지스트리(`Asset`)에 오른다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /** @brief 시트를 나눠 받습니다. 처음이면 읽고 파싱합니다. 실패하면 nullptr 과 @p outError(`<경로>:<줄>: <이유>`)입니다. */
        shared_ptr<const UIStyleSheetAsset> findOrLoad( string_view path, string& outError );
        /** @brief 파일 대신 읽을 시트 글을 경로에 겁니다(테스트). 이미 읽어 둔 그 경로는 버립니다. */
        void registerMemorySheet( string_view path, string_view text ) { registerMemoryText( path, text ); }

    private:
        Subscription _assetCacheRegistration; ///< 에셋 캐시 레지스트리 등록 구독(기동이 받고 종료가 놓는다)
    };
} // namespace sw
