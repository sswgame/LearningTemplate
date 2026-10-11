#include "pch.h"

#include "Editor/Common/GUI/EditorNotificationView.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include <imgui.h>

namespace sw::editor
{
    void EditorNotificationView::draw( const EditorNotificationManager& manager, float32 screenWidth, float32 screenHeight )
    {
        const vector<NotificationItem>& listNotification = manager.getNotifications();
        if ( listNotification.empty() )
            return;

        constexpr float32 toastWidth  = 300.0f;
        constexpr float32 toastMargin = 12.0f;
        float32           currentY    = screenHeight - toastMargin;

        float2 viewportPos{};
        float2 viewportSize{ screenWidth, screenHeight };
        if ( EditorChrome::tryGetMainViewportRect( viewportPos, viewportSize ) )
        {
            screenWidth  = viewportSize._x;
            screenHeight = viewportSize._y;
            currentY     = viewportPos._y + screenHeight - toastMargin;
        }

        for ( size_t notificationIndex = listNotification.size(); notificationIndex > 0; --notificationIndex )
        {
            const NotificationItem& item = listNotification[notificationIndex - 1];

            const float32 remainingTime = item._durationSec - item._elapsedSec;
            float32       alpha         = 1.0f;
            if ( remainingTime < 0.5f )
                alpha = remainingTime / 0.5f;

            ImVec4 bgColor{ 0.12f, 0.12f, 0.14f, 0.92f * alpha };
            ImVec4 borderColor{ 0.3f, 0.3f, 0.3f, 0.8f * alpha };
            ImVec4 titleColor{ 1.0f, 1.0f, 1.0f, 1.0f * alpha };

            switch ( item._type )
            {
                case NotificationType::Success:
                {
                    borderColor = ImVec4{ 0.2f, 0.7f, 0.3f, alpha };
                    titleColor  = ImVec4{ 0.4f, 0.9f, 0.5f, alpha };
                    break;
                }
                case NotificationType::Warning:
                {
                    borderColor = ImVec4{ 0.9f, 0.6f, 0.1f, alpha };
                    titleColor  = ImVec4{ 1.0f, 0.8f, 0.3f, alpha };
                    break;
                }
                case NotificationType::Error:
                {
                    borderColor = ImVec4{ 0.9f, 0.2f, 0.2f, alpha };
                    titleColor  = ImVec4{ 1.0f, 0.4f, 0.4f, alpha };
                    break;
                }
                case NotificationType::Info:
                {
                    borderColor = ImVec4{ 0.2f, 0.5f, 0.9f, alpha };
                    titleColor  = ImVec4{ 0.4f, 0.7f, 1.0f, alpha };
                    break;
                }
            }

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
            toastDesc._bgAlpha    = bgColor.w;
            toastDesc._flags      = editor::EditorOverlayFlags::NoDecoration | editor::EditorOverlayFlags::NoInputs |
                               editor::EditorOverlayFlags::NoNav | editor::EditorOverlayFlags::AutoResize |
                               editor::EditorOverlayFlags::NoSavedSettings | editor::EditorOverlayFlags::NoFocusOnAppearing;

            ImGui::PushStyleColor( ImGuiCol_WindowBg, bgColor );
            ImGui::PushStyleColor( ImGuiCol_Border, borderColor );

            if ( EditorChrome::beginOverlay( toastDesc ) )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, titleColor );
                ImGui::TextUnformatted( item._title.c_str() );
                ImGui::PopStyleColor();

                if ( item._message.empty() == false )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4{ 0.85f, 0.85f, 0.85f, alpha } );
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
