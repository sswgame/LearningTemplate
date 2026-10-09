#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/SocialTypes.h"

namespace sw
{
    const utf8* toString( SocialResult result )
    {
        static constexpr const utf8* kArrName[] = { "Ok", "NotSignedIn", "Invalid", "NotFound", "AlreadyFriend", "AlreadyRequested",
                                                    "FriendLimit", "PendingLimit", "BlockLimit", "YouBlocked", "Unavailable", "Conflict",
                                                    "GuildNameTaken", "AlreadyInGuild", "NotInGuild", "NotAllowed", "GuildFull", "InviteExpired" };
        static_assert( SW_COUNT_OF( kArrName ) == static_cast<size_t>( SocialResult::Count ), "SocialResult names must match the enum" );
        const size_t index = static_cast<size_t>( result );
        return index < SW_COUNT_OF( kArrName ) ? kArrName[index] : "Unknown";
    }
} // namespace sw
