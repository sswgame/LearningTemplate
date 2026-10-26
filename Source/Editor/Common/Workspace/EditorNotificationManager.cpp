#include "pch.h"

#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/VectorUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Probe/AutomationProbe.h"

#include <cstdlib>

namespace sw::editor
{
    namespace
    {
        struct EditorNotificationManagerInternal
        {
            /** @brief 개발 명령 notify.push <초> <글> — 알림 하나를 띄운다(시나리오가 토스트가 시간이 지나 사라지는지 본다). */
            static bool runPush( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() < 2 || pContext == nullptr )
                    return false;
                float32 durationSec = 0.0f;
                if ( StringUtil::parseFloat( listArgument[0], durationSec ) == false )
                    durationSec = 0.0f; // 숫자가 아니면 아래 검사가 양수가 아니라고 답한다
                if ( durationSec <= 0.0f )
                {
                    outReply = "the duration must be a positive number of seconds";
                    return false;
                }
                string message = listArgument[1];
                for ( size_t index = 2; index < listArgument.size(); ++index )
                {
                    message += " " + listArgument[index];
                }
                pContext->getSubsystem<EditorNotificationManager>().push( "notify.push", message, NotificationType::Info, durationSec );
                outReply = "pushed";
                return true;
            }

            /** @brief 탐침 Editor.NotificationCount — 지금 떠 있는 알림 수입니다. */
            [[nodiscard]] static bool readNotificationCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                outValue = static_cast<float64>( pContext->getSubsystem<EditorNotificationManager>().getNotifications().size() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NotifyPush, "notify.push", "notify.push <seconds> <text>", "Show a toast notification for the given seconds (scenarios check that it goes away)",
                    &EditorNotificationManagerInternal::runPush );
    SW_AUTOMATION_PROBE( editorNotificationCount, "Editor.NotificationCount", "Toast notifications on screen now", &EditorNotificationManagerInternal::readNotificationCount );

    void EditorNotificationManager::push( string_view title, string_view message, NotificationType type,
                                          float32 durationSec, float32 progress )
    {
        SW_ASSERT( durationSec > 0.0f ); // 표시 시간이 없는 알림은 호출 오류다 — 조용히 다른 값으로 바꾸지 않는다
        NotificationItem item;
        item._title       = string{ title };
        item._message     = string{ message };
        item._type        = type;
        item._durationSec = durationSec;
        item._elapsedSec  = 0.0f;
        item._progress    = progress;

        _listNotification.push_back( std::move( item ) );
    }

    void EditorNotificationManager::pushRevealable( string_view title, string_view message, NotificationType type, string_view revealPath, float32 durationSec )
    {
        push( title, message, type, durationSec );
        _listNotification.back()._revealPath = string{ revealPath };
    }

    void EditorNotificationManager::update( float32 deltaTime )
    {
        if ( _listNotification.empty() )
            return;

        for ( NotificationItem& item : _listNotification )
        {
            item._elapsedSec += deltaTime;
        }

        VectorUtil::removeIf( _listNotification, []( const NotificationItem& item )
        { return item._elapsedSec >= item._durationSec; } );
    }
} // namespace sw::editor
