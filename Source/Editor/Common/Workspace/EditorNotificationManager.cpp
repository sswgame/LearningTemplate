#include "pch.h"

#include "Editor/Common/Workspace/EditorNotificationManager.h"

#include "Core/Common/StdHeaders.h"

namespace sw::editor
{
    void EditorNotificationManager::push( string_view title, string_view message, NotificationType type,
                                          float32 durationSec, float32 progress )
    {
        NotificationItem item;
        item._title       = string{ title };
        item._message     = string{ message };
        item._type        = type;
        item._durationSec = durationSec > 0.5f ? durationSec : 4.0f;
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

        _listNotification.erase(
            std::remove_if( _listNotification.begin(), _listNotification.end(),
                            []( const NotificationItem& item )
        { return item._elapsedSec >= item._durationSec; } ),
            _listNotification.end() );
    }
} // namespace sw::editor
