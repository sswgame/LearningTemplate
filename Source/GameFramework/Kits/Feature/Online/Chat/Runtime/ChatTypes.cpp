#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatTypes.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ChatTypesInternal
        {
            static bool isIDCharacter( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.'; }

            /** @brief 접두어 뒤에 이름이 한 글자 이상 있으면 true 입니다. */
            static bool hasPrefixWithName( string_view text, string_view prefix ) { return text.size() > prefix.size() && StringUtil::startsWith( text, prefix ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ChatChannelID::parseKind( string_view channelID, ChatChannelKind& outKind )
    {
        if ( channelID.empty() || channelID.size() > static_cast<size_t>( ChatLimit::kMaxChannelIDSize ) )
            return false;
        for ( const utf8 ch : channelID )
        {
            if ( ChatTypesInternal::isIDCharacter( ch ) == false )
                return false;
        }
        if ( ChatTypesInternal::hasPrefixWithName( channelID, "world." ) )
            outKind = ChatChannelKind::World;
        else if ( ChatTypesInternal::hasPrefixWithName( channelID, "guild." ) )
            outKind = ChatChannelKind::Guild;
        else if ( ChatTypesInternal::hasPrefixWithName( channelID, "party." ) )
            outKind = ChatChannelKind::Party;
        else if ( ChatTypesInternal::hasPrefixWithName( channelID, "custom." ) )
            outKind = ChatChannelKind::Custom;
        else if ( ChatTypesInternal::hasPrefixWithName( channelID, "whisper." ) )
            outKind = ChatChannelKind::Whisper;
        else
            return false;
        return true;
    }

    string ChatChannelID::makeGuild( uint64 guildID )
    {
        string channelID( "guild." );
        ServiceKeyUtil::appendHex64( channelID, guildID );
        return channelID;
    }

    string ChatChannelID::makeParty( uint64 partyID )
    {
        string channelID( "party." );
        ServiceKeyUtil::appendHex64( channelID, partyID );
        return channelID;
    }

    string ChatChannelID::makeWhisper( AccountID first, AccountID second )
    {
        const AccountID low  = first < second ? first : second;
        const AccountID high = first < second ? second : first;
        string          channelID( "whisper." );
        ServiceKeyUtil::appendHex64( channelID, low );
        channelID += '.';
        ServiceKeyUtil::appendHex64( channelID, high );
        return channelID;
    }
} // namespace sw
