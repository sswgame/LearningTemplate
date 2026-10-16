#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    MailboxResult MailboxProtocol::fromErrorCode( uint16 errorCode )
    {
        static constexpr OnlineErrorResult<MailboxResult> kArrErrorResult[] = {
            {             OnlineError::kOk,             MailboxResult::Ok},
            {OnlineError::kUnauthenticated,    MailboxResult::NotSignedIn},
            { OnlineError::kInvalidRequest, MailboxResult::InvalidRequest},
            {       OnlineError::kConflict,           MailboxResult::Busy},
        };
        return OnlineError::toResult( errorCode, kArrErrorResult, MailboxResult::Unavailable );
    }
} // namespace sw

namespace sw
{
    const OnlineProtocolEntry MailboxProtocol::kOnlineEntry{ "Mailbox", OnlineMethodRange::kMailbox, MailboxProtocol::MessageTypes{} };
} // namespace sw
