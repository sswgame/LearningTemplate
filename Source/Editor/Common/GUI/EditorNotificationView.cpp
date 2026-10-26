#include "pch.h"

#include "Editor/Common/GUI/EditorNotificationView.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"

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

        constexpr float32 toastWidth  = 300.0f;
        constexpr float32 toastMargin = 12.0f;

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

            float32 posX = viewportPos._x + screenWidth - toastWidth - toastMargin;
            currentY -= 75.0f;

            fixed_string<constant::kMaxBuffer32> windowID;
            formatstring( windowID.data(), windowID.capacity(), "##Toast_%#", static_cast<uint64>( notificationIndex ) );

            editor::EditorOverlayDesc toastDesc{};
            toastDesc._pID        = windowID.c_str();
            toastDesc._anchorPos  = float2{ posX, currentY };
            toastDesc._size       = float2{ toastWidth, 0.0f };
            toastDesc._rounding   = 6.0f;
            toastDesc._borderSize = 1.5f;
            toastDesc._bgAlpha    = bgColor._a;
            toastDesc._flags      = editor::EditorOverlayFlags::NoDecoration | editor::EditorOverlayFlags::NoInputs |
                               editor::EditorOverlayFlags::NoNav | editor::EditorOverlayFlags::AutoResize |
                               editor::EditorOverlayFlags::NoSavedSettings | editor::EditorOverlayFlags::NoFocusOnAppearing;

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

                if ( item._progress >= 0.0f )
                {
                    ImGui::ProgressBar( item._progress, ImVec2{ -1.0f, 4.0f * EditorThemeUtil::getDpiScale() }, "" );
                }
            }
            EditorChrome::endOverlay();

            ImGui::PopStyleColor( 2 );

            currentY -= 8.0f;
        }
    }
} // namespace sw::editor
