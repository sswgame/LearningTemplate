#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "LeaderboardCatalog" );

    namespace
    {
        struct LeaderboardCatalogInternal
        {
            /** @brief 빈 글이면 그대로 두고 true, 정수가 아니면 false 입니다. */
            [[nodiscard]] static bool readInt64( const XMLNode& node, const utf8* pName, int64& inoutValue )
            {
                const string_view text = node.getAttributeText( pName );
                return text.empty() || StringUtil::parseInt64( text, inoutValue );
            }

            [[nodiscard]] static bool readOrder( string_view text, LeaderboardOrder& inoutOrder )
            {
                if ( text.empty() )
                    return true;
                if ( text == "descending" )
                    inoutOrder = LeaderboardOrder::Descending;
                else if ( text == "ascending" )
                    inoutOrder = LeaderboardOrder::Ascending;
                else
                    return false;
                return true;
            }

            [[nodiscard]] static bool readUpdate( string_view text, LeaderboardUpdate& inoutUpdate )
            {
                if ( text.empty() )
                    return true;
                if ( text == "best" )
                    inoutUpdate = LeaderboardUpdate::Best;
                else if ( text == "latest" )
                    inoutUpdate = LeaderboardUpdate::Latest;
                else if ( text == "sum" )
                    inoutUpdate = LeaderboardUpdate::Sum;
                else
                    return false;
                return true;
            }

            [[nodiscard]] static bool readReset( string_view text, LeaderboardReset& inoutReset )
            {
                if ( text.empty() )
                    return true;
                if ( text == "none" )
                    inoutReset = LeaderboardReset::None;
                else if ( text == "daily" )
                    inoutReset = LeaderboardReset::Daily;
                else if ( text == "weekly" )
                    inoutReset = LeaderboardReset::Weekly;
                else if ( text == "season" )
                    inoutReset = LeaderboardReset::Season;
                else
                    return false;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 LeaderboardCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Board" ); node; node = node.findNextSibling( "Board" ) )
        {
            LeaderboardDefinition board;
            if ( readBoard( node, sourceName, board ) == false )
                continue;
            _listBoard.push_back( std::move( board ) );
            ++loadedCount;
        }
        for ( XMLNode node = root.findChild( "Achievement" ); node; node = node.findNextSibling( "Achievement" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            AchievementDefinition achievement;
            achievement._achievementID = pID;
            achievement._statName      = string( node.getAttributeText( "stat" ) );
            achievement._rewardAssetID = string( node.getAttributeText( "rewardAsset" ) );
            achievement._mailTitleKey  = string( node.getAttributeText( "mailTitle" ) );
            const bool bNumbersOk      = LeaderboardCatalogInternal::readInt64( node, "threshold", achievement._threshold ) &&
                                    LeaderboardCatalogInternal::readInt64( node, "rewardAmount", achievement._rewardAmount );
            if ( bNumbersOk == false )
            {
                SW_LOG_WARNING( "%#: achievement '%#' has a threshold or reward amount that is not an integer - skipped", sourceName, pID );
                continue;
            }
            _listAchievement.push_back( std::move( achievement ) );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Board> or <Achievement> entries", sourceName );
        return loadedCount;
    }

    bool LeaderboardCatalog::readBoard( const XMLNode& node, string_view sourceName, LeaderboardDefinition& outBoard )
    {
        using Internal  = LeaderboardCatalogInternal;
        const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
        if ( pID == nullptr )
            return false;
        outBoard._boardID          = pID;
        outBoard._sourceStat       = string( node.getAttributeText( "sourceStat" ) );
        outBoard._resetMinuteOfDay = node.getAttributeInt( "resetMinute", 0 );
        outBoard._resetDayOfWeek   = node.getAttributeInt( "resetDay", 0 );
        outBoard._bClientSubmit    = node.getAttributeBool( "clientSubmit", false ) ? SW_TRUE : SW_FALSE;
        const bool bEnumsOk        = Internal::readOrder( node.getAttributeText( "order" ), outBoard._order ) && Internal::readUpdate( node.getAttributeText( "update" ), outBoard._update ) &&
                              Internal::readReset( node.getAttributeText( "reset" ), outBoard._reset );
        if ( bEnumsOk == false )
        {
            SW_LOG_WARNING( "%#: board '%#' has an unknown order, update or reset (descending|ascending, best|latest|sum, none|daily|weekly|season) - skipped", sourceName,
                            pID );
            return false;
        }
        for ( XMLNode seasonNode = node.findChild( "Season" ); seasonNode; seasonNode = seasonNode.findNextSibling( "Season" ) )
        {
            LeaderboardSeason& season = outBoard._listSeason.emplace_back();
            season._seasonID          = static_cast<uint32>( seasonNode.getAttributeInt( "id", 0 ) );
            const bool bTimesOk       = Internal::readInt64( seasonNode, "startMs", season._startMs ) && Internal::readInt64( seasonNode, "endMs", season._endMs );
            if ( bTimesOk == false || season._seasonID == 0 )
            {
                SW_LOG_WARNING( "%#: board '%#' has a season without a positive id or with a time that is not an integer - skipped", sourceName, pID );
                return false;
            }
            for ( XMLNode rewardNode = seasonNode.findChild( "Reward" ); rewardNode; rewardNode = rewardNode.findNextSibling( "Reward" ) )
            {
                LeaderboardRewardTier& tier = season._listRewardTier.emplace_back();
                tier._rankFrom              = rewardNode.getAttributeInt( "from", 1 );
                tier._rankTo                = rewardNode.getAttributeInt( "to", tier._rankFrom );
                tier._assetID               = string( rewardNode.getAttributeText( "asset" ) );
                tier._mailTitleKey          = string( rewardNode.getAttributeText( "mailTitle" ) );
                if ( Internal::readInt64( rewardNode, "amount", tier._amount ) == false )
                {
                    SW_LOG_WARNING( "%#: board '%#' has a reward amount that is not an integer - skipped", sourceName, pID );
                    return false;
                }
            }
        }
        return true;
    }
} // namespace sw
