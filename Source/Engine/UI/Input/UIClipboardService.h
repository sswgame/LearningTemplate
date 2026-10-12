/**
 * @file UIClipboardService.h
 * @brief 글 입력 위젯의 복사 · 잘라내기 · 붙여넣기가 쓰는 글 클립보드 창구입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @struct UIClipboardAPI
     * @brief 운영체제 클립보드를 잇는 C 함수 표입니다(창 계층이 기동 때 채웁니다). 비워 두면 엔진 안에서만 오가는 글 한 칸이 클립보드입니다.
     */
    struct UIClipboardAPI
    {
        bool ( *_pRead )( string& outText )   = nullptr; ///< 운영체제 클립보드의 글을 읽습니다. 글이 없으면 false 입니다.
        void ( *_pWrite )( string_view text ) = nullptr; ///< 운영체제 클립보드에 글을 씁니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIClipboardService
     * @brief UI 가 쓰는 글 클립보드입니다(`UISystem::getClipboard`). 언리얼의 `FPlatformApplicationMisc::ClipboardCopy/Paste`, 유니티의 `GUIUtility.systemCopyBuffer`,
     *        Godot 의 `DisplayServer.clipboard_get/set` 에 해당합니다.
     * @details 운영체제 연결(`setAPI`)이 없으면 쓴 글을 그대로 보관했다가 돌려줍니다. 그래서 헤드리스 테스트와 전용 서버에서도 복사 붙여넣기가 한 바퀴 돕니다.
     *          연결이 있으면 쓸 때 보관본도 같이 갱신하고, 읽을 때 운영체제 쪽을 먼저 봅니다(다른 프로그램에서 복사한 글).
     */
    class SW_API UIClipboardService
    {
    public:
        UIClipboardService();

        /** @brief 운영체제 클립보드 연결을 정합니다(복사해 두며, 모듈이 내려가기 전에 비웁니다). */
        void setAPI( const UIClipboardAPI& api );
        /** @brief 글을 씁니다. */
        void setText( string_view text );
        /** @brief 글이 있으면 @p outText 에 담고 true 입니다. 비어 있으면 false 입니다. */
        [[nodiscard]] bool tryGetText( string& outText ) const;

    private:
        UIClipboardAPI _api;
        string         _text; ///< 엔진 안 보관본입니다(운영체제 연결이 없을 때의 클립보드이자 운영체제 쪽이 비었을 때의 대체입니다).
    };
} // namespace sw
