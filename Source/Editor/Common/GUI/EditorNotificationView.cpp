#include "pch.h"

#include "Editor/Common/GUI/EditorNotificationView.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 알림 종류의 의미 색(테마 토큰)입니다. 토스트의 테두리와 제목이 함께 씁니다. */
        const Color& getNotificationTypeColor( NotificationType type )
        {
            switch ( type )
            {
                case NotificationType::Success:
                    return EditorThemeUtil::getSuccessColor();
                case NotificationType::Warning:
                    return EditorThemeUtil::getWarningColor();
                case NotificationType::Error:
                    return EditorThemeUtil::getErrorColor();
                case NotificationType::Info:
                    return EditorThemeUtil::getInfoColor();
            }
            SW_ASSERT( false ); // 열거자 밖의 값은 들어오지 않는다
            return EditorThemeUtil::getInfoColor();
        }
    } // namespace

    void EditorNotificationView::draw( const EditorNotificationManager& manager )
    {
        const vector<NotificationItem>& listNotification = manager.getNotifications();
        if ( listNotification.empty() )
            return;

        constexpr float32 toastColumns    = 40.0f;
        constexpr float32 toastMargin     = 12.0f;
        const float32     toastSlotHeight = EditorChrome::resolveTextExtent( EditorTextExtent::ofText( 0.0f, 4.0f ) )._y; ///< 토스트 하나가 쌓이는 세로 간격(글자 줄 4 줄)

        // 메인 뷰포트는 ImGui 프레임 안에서 항상 있다 — 없으면 호출 시점이 틀린 것이라 임의 크기로 계속하지 않고 멈춘다
        float2     viewportPos{};
        float2     viewportSize{};
        const bool bHasViewport = EditorChrome::tryGetMainViewportRect( viewportPos, viewportSize );
        SW_ASSERT( bHasViewport );
        if ( bHasViewport == false )
            return;
        const float32 screenWidth  = viewportSize._x;
        const float32 screenHeight = viewportSize._y;
        float32       currentY     = viewportPos._y + screenHeight - toastMargin;

        for ( size_t notificationIndex = listNotification.size(); notificationIndex > 0; --notificationIndex )
        {
            const NotificationItem& item = listNotification[notificationIndex - 1];

            const float32 remainingTime = item._durationSec - item._elapsedSec;
            float32       alpha         = 1.0f;
            if ( remainingTime < 0.5f )
                alpha = remainingTime / 0.5f;

            const Color bgColor   = EditorThemeUtil::getOverlayBgColor().withAlpha( 0.92f * alpha );
            const Color typeColor = getNotificationTypeColor( item._type ).withAlpha( alpha );

            float32 posX = viewportPos._x + screenWidth - EditorChrome::resolveTextExtent( EditorTextExtent::ofText( toastColumns, 0.0f ) )._x - toastMargin;
            currentY -= toastSlotHeight;

            fixed_string<constant::kMaxBuffer32> windowID;
            formatstring( windowID.data(), windowID.capacity(), "##Toast_%#", static_cast<uint64>( notificationIndex ) );

            editor::EditorOverlayDesc toastDesc{};
            toastDesc._pID        = windowID.c_str();
            toastDesc._anchorPos  = float2{ posX, currentY };
            toastDesc._size       = EditorTextExtent::ofText( toastColumns, 0.0f );
            toastDesc._rounding   = 6.0f;
            toastDesc._borderSize = 1.5f;
            toastDesc._bgAlpha    = bgColor._a;
            toastDesc._flags      = editor::EditorOverlayFlags::NoDecoration |
                               editor::EditorOverlayFlags::NoNav | editor::EditorOverlayFlags::AutoResize |
                               editor::EditorOverlayFlags::NoSavedSettings | editor::EditorOverlayFlags::NoFocusOnAppearing;
            // 단추가 있는 토스트만 마우스를 받는다. 나머지는 뒤의 패널로 입력을 통과시킨다.
            if ( item._revealPath.empty() )
                toastDesc._flags = toastDesc._flags | editor::EditorOverlayFlags::NoInputs;

            ImGui::PushStyleColor( ImGuiCol_WindowBg, EditorColorUtil::toImVec4( bgColor ) );
            ImGui::PushStyleColor( ImGuiCol_Border, EditorColorUtil::toImVec4( typeColor ) );

            if ( EditorChrome::beginOverlay( toastDesc ) )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( typeColor ) );
                ImGui::TextUnformatted( item._title.c_str() );
                ImGui::PopStyleColor();

                if ( item._message.empty() == false )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( Color{ 0.85f, 0.85f, 0.85f, alpha } ) );
                    ImGui::TextWrapped( "%s", item._message.c_str() );
                    ImGui::PopStyleColor();
                }

                if ( item._revealPath.empty() == false )
                {
                    // 콘텐츠 브라우저 메뉴와 같은 글이다(그쪽도 지역화 전 글).
                    if ( ImGui::SmallButton( "Show in Explorer" ) )
                        (void)EditorAssetFileCommands::showInFileExplorer( item._revealPath );
                    EditorSelfTestMarks::note( "notification.showInExplorer" );
                }

                if ( item._progress >= 0.0f )
                {
                    ImGui::ProgressBar( item._progress, ImVec2{ -1.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 0.25f ) ).y }, "" );
                }
            }
            EditorChrome::endOverlay();

            ImGui::PopStyleColor( 2 );

            currentY -= 8.0f;
        }
    }
} // namespace sw::editor
