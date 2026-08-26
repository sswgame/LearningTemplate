#include "pch.h"

#include "Editor/Popups/EditorNotificationManager.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Gui/EditorChrome.h"

#include <imgui.h>
#include <algorithm>

namespace sw::editor
{
	namespace
	{
		EditorNotificationManager* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getNotificationManager();
			return nullptr;
		}
	} // namespace

	void EditorNotificationManager::push( string_view title, string_view message, NotificationType type,
										  float32 durationSec, float32 progress )
	{
		EditorNotificationManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->pushImpl( title, message, type, durationSec, progress );
	}

	void EditorNotificationManager::updateAndDraw( float32 deltaTime, float32 screenWidth, float32 screenHeight )
	{
		EditorNotificationManager* pManager = getImpl();
		if ( pManager != nullptr )
			pManager->updateAndDrawImpl( deltaTime, screenWidth, screenHeight );
	}

	size_t EditorNotificationManager::getNotificationCount()
	{
		EditorNotificationManager* pManager = getImpl();
		if ( pManager != nullptr )
			return pManager->getNotificationCountImpl();
		return 0;
	}

	void EditorNotificationManager::pushImpl( string_view title, string_view message, NotificationType type,
											  float32 durationSec, float32 progress )
	{
		NotificationItem item;
		item._title		  = string{ title };
		item._message	  = string{ message };
		item._type		  = type;
		item._durationSec = durationSec > 0.5f ? durationSec : 4.0f;
		item._elapsedSec  = 0.0f;
		item._progress	  = progress;

		_listNotifications.push_back( std::move( item ) );
	}

	void EditorNotificationManager::updateAndDrawImpl( float32 deltaTime, float32 screenWidth, float32 screenHeight )
	{
		if ( _listNotifications.empty() )
			return;

		for ( NotificationItem& item : _listNotifications )
		{
			item._elapsedSec += deltaTime;
		}

		_listNotifications.erase(
			std::remove_if( _listNotifications.begin(), _listNotifications.end(),
							[]( const NotificationItem& item )
		{ return item._elapsedSec >= item._durationSec; } ),
			_listNotifications.end() );

		if ( _listNotifications.empty() )
			return;

		constexpr float32 toastWidth  = 300.0f;
		constexpr float32 toastMargin = 12.0f;
		float32			  currentY	  = screenHeight - toastMargin;

		float2 viewportPos{};
		float2 viewportSize{ screenWidth, screenHeight };
		if ( editor::tryGetMainViewportRect( viewportPos, viewportSize ) )
		{
			screenWidth	 = viewportSize._x;
			screenHeight = viewportSize._y;
			currentY	 = viewportPos._y + screenHeight - toastMargin;
		}

		for ( size_t notificationIndex = _listNotifications.size(); notificationIndex > 0; --notificationIndex )
		{
			const NotificationItem& item = _listNotifications[notificationIndex - 1];

			const float32 remainingTime = item._durationSec - item._elapsedSec;
			float32		  alpha			= 1.0f;
			if ( remainingTime < 0.5f )
				alpha = remainingTime / 0.5f;

			ImVec4 bgCol{ 0.12f, 0.12f, 0.14f, 0.92f * alpha };
			ImVec4 borderCol{ 0.3f, 0.3f, 0.3f, 0.8f * alpha };
			ImVec4 titleCol{ 1.0f, 1.0f, 1.0f, 1.0f * alpha };

			switch ( item._type )
			{
				case NotificationType::Success:
					borderCol = ImVec4{ 0.2f, 0.7f, 0.3f, alpha };
					titleCol  = ImVec4{ 0.4f, 0.9f, 0.5f, alpha };
					break;
				case NotificationType::Warning:
					borderCol = ImVec4{ 0.9f, 0.6f, 0.1f, alpha };
					titleCol  = ImVec4{ 1.0f, 0.8f, 0.3f, alpha };
					break;
				case NotificationType::Error:
					borderCol = ImVec4{ 0.9f, 0.2f, 0.2f, alpha };
					titleCol  = ImVec4{ 1.0f, 0.4f, 0.4f, alpha };
					break;
				case NotificationType::Info:
				default:
					borderCol = ImVec4{ 0.2f, 0.5f, 0.9f, alpha };
					titleCol  = ImVec4{ 0.4f, 0.7f, 1.0f, alpha };
					break;
			}

			float32 posX = viewportPos._x + screenWidth - toastWidth - toastMargin;
			currentY -= 75.0f;

			utf8 windowId[32];
			formatstring( windowId, sizeof( windowId ), "##Toast_%#", static_cast<uint64>( notificationIndex ) );

			editor::EditorOverlayDesc toastDesc{};
			toastDesc._pId		 = windowId;
			toastDesc._anchorPos = float2{ posX, currentY };
			toastDesc._size		 = float2{ toastWidth, 0.0f };
			toastDesc._rounding	  = 6.0f;
			toastDesc._borderSize = 1.5f;
			toastDesc._bgAlpha	  = bgCol.w;
			toastDesc._flags	  = editor::EditorOverlayFlags::NoDecoration | editor::EditorOverlayFlags::NoInputs |
							   editor::EditorOverlayFlags::NoNav | editor::EditorOverlayFlags::AutoResize |
							   editor::EditorOverlayFlags::NoSavedSettings | editor::EditorOverlayFlags::NoFocusOnAppearing;

			ImGui::PushStyleColor( ImGuiCol_WindowBg, bgCol );
			ImGui::PushStyleColor( ImGuiCol_Border, borderCol );

			if ( editor::beginOverlay( toastDesc ) )
			{
				ImGui::PushStyleColor( ImGuiCol_Text, titleCol );
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
					ImGui::ProgressBar( item._progress, ImVec2{ -1.0f, 4.0f }, "" );
				}
			}
			editor::endOverlay();

			ImGui::PopStyleColor( 2 );

			currentY -= 8.0f;
		}
	}
} // namespace sw::editor
