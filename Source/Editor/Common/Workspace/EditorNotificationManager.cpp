#include "pch.h"

#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"

namespace sw::editor
{
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
