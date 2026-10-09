#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ChatTypesInternal
        {
            static bool isIdCharacter( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.'; }

            /** @brief 접두 뒤에 이름이 한 글자 이상 있는가입니다. */
            static bool hasPrefixWithName( string_view text, string_view prefix ) { return text.size() > prefix.size() && StringUtil::startsWith( text, prefix ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( ChatResult result )
    {
        switch ( result )
        {
            case ChatResult::Ok:
                return "Ok";
            case ChatResult::NotMember:
                return "NotMember";
            case ChatResult::NotJoinable:
                return "NotJoinable";
            case ChatResult::Muted:
                return "Muted";
            case ChatResult::RateLimited:
                return "RateLimited";
            case ChatResult::Repeated:
                return "Repeated";
            case ChatResult::Rejected:
                return "Rejected";
            case ChatResult::TargetOffline:
                return "TargetOffline";
            case ChatResult::Invalid:
                return "Invalid";
            case ChatResult::Unavailable:
                return "Unavailable";
            case ChatResult::TooManyChannels:
                return "TooManyChannels";
            case ChatResult::NotSignedIn:
                return "NotSignedIn";
        }
        return "Unknown";
    }

    bool ChatChannelId::parseKind( string_view channelId, ChatChannelKind& outKind )
    {
        if ( channelId.empty() || channelId.size() > static_cast<size_t>( ChatLimit::kMaxChannelIdSize ) )
            return false;
        for ( const utf8 ch : channelId )
        {
            if ( ChatTypesInternal::isIdCharacter( ch ) == false )
                return false;
        }
        if ( ChatTypesInternal::hasPrefixWithName( channelId, "world." ) )
            outKind = ChatChannelKind::World;
        else if ( ChatTypesInternal::hasPrefixWithName( channelId, "guild." ) )
            outKind = ChatChannelKind::Guild;
        else if ( ChatTypesInternal::hasPrefixWithName( channelId, "party." ) )
            outKind = ChatChannelKind::Party;
        else if ( ChatTypesInternal::hasPrefixWithName( channelId, "custom." ) )
            outKind = ChatChannelKind::Custom;
        else if ( ChatTypesInternal::hasPrefixWithName( channelId, "whisper." ) )
            outKind = ChatChannelKind::Whisper;
        else
            return false;
        return true;
    }

    string ChatChannelId::makeGuild( uint64 guildId )
    {
        string channelId( "guild." );
        ServiceKeyUtil::appendHex64( channelId, guildId );
        return channelId;
    }

    string ChatChannelId::makeParty( uint64 partyId )
    {
        string channelId( "party." );
        ServiceKeyUtil::appendHex64( channelId, partyId );
        return channelId;
    }

    string ChatChannelId::makeWhisper( AccountId first, AccountId second )
    {
        const AccountId low  = first < second ? first : second;
        const AccountId high = first < second ? second : first;
        string          channelId( "whisper." );
        ServiceKeyUtil::appendHex64( channelId, low );
        channelId += '.';
        ServiceKeyUtil::appendHex64( channelId, high );
        return channelId;
    }
} // namespace sw
