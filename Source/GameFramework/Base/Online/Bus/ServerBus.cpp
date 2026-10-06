#include "pch.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"

namespace sw
{
    bool IServerBus::isValidTopic( string_view topic )
    {
        if ( topic.empty() || topic.size() > static_cast<size_t>( kMaxTopicSize ) )
            return false;
        for ( const utf8 ch : topic )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }
} // namespace sw
