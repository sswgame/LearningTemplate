#include "pch.h"

#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    const utf8* toString( StreamCloseReason reason )
    {
        switch ( reason )
        {
            case StreamCloseReason::None:
                return "None";
            case StreamCloseReason::LocalClose:
                return "LocalClose";
            case StreamCloseReason::RemoteClose:
                return "RemoteClose";
            case StreamCloseReason::Reset:
                return "Reset";
            case StreamCloseReason::IdleTimeout:
                return "IdleTimeout";
            case StreamCloseReason::ConnectFailed:
                return "ConnectFailed";
            case StreamCloseReason::ProtocolError:
                return "ProtocolError";
            case StreamCloseReason::SendQueueOverflow:
                return "SendQueueOverflow";
            case StreamCloseReason::SecurityFailure:
                return "SecurityFailure";
            case StreamCloseReason::Shutdown:
                return "Shutdown";
        }
        return "Unknown";
    }
} // namespace sw
