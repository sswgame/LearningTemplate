/**
 * @file UIClipboardService.h
 * @brief 글 입력 위젯의 복사 · 잘라내기 · 붙여넣기가 쓰는 글 클립보드 창구입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @struct UIClipboardAPI
     * @brief 운영체제 클립보드를 잇는 C 함수 표입니다(창 계층이 기동 때 채웁니다). 비워 두면 엔진 안에서만 오가는 글 한 칸이 클립보드입니다.
     */
    struct UIClipboardAPI
    {
        bool ( *_pRead )( string& outText )   = nullptr; ///< 운영체제 클립보드의 글을 읽습니다. 글이 없으면 false 입니다.
        bool ( *_pWrite )( string_view text ) = nullptr; ///< 운영체제 클립보드에 글을 씁니다. 못 쓰면 false 입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIClipboardService
     * @brief UI 가 쓰는 글 클립보드입니다(엔진 범위 서브시스템 `UIClipboard`). 언리얼의 `FPlatformApplicationMisc::ClipboardCopy/Paste`, 유니티의 `GUIUtility.systemCopyBuffer`,
     *        Godot 의 `DisplayServer.clipboard_get/set` 에 해당합니다.
     * @details 운영체제 연결(`setAPI`)이 없으면 쓴 글을 그대로 보관했다가 돌려줍니다. 그래서 헤드리스 테스트와 전용 서버에서도 복사 붙여넣기가 한 바퀴 돕니다.
     *          연결이 있으면 읽기와 쓰기 모두 운영체제 쪽만 본다(다른 프로그램에서 복사한 글이 오고, 그쪽이 비었으면 비었다). 게임 창 호스트(`App`)가
     *          `PlatformClipboard` 를 끼운다.
     */
    class SW_API UIClipboardService final : public ISubsystem
    {
    public:
        UIClipboardService();

        // ISubsystem — 엔진 범위의 서브시스템이다(`engine::getSubsystem<UIClipboardService>()`, 게임은 `game::getSubsystem<UIClipboardService>()`).
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIClipboard";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override { _text.clear(); }
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 운영체제 클립보드 연결을 정합니다(복사해 두며, 모듈이 내려가기 전에 비웁니다). */
        void setAPI( const UIClipboardAPI& api );
        /** @brief 글을 씁니다. 운영체제 연결이 쓰지 못하면 false 이며(엔진 안 클립보드는 항상 true) 호출 쪽이 사용자에게 알린다. */
        [[nodiscard]] bool setText( string_view text );
        /** @brief 글이 있으면 @p outText 에 담고 true 입니다. 비어 있으면 false 입니다. */
        [[nodiscard]] bool tryGetText( string& outText ) const;

    private:
        UIClipboardAPI _api;
        string         _text; ///< 엔진 안 클립보드입니다(운영체제 연결이 없을 때만 씁니다).
    };
} // namespace sw
