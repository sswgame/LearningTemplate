#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "LeaderboardCatalog" );

    uint32 LeaderboardCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 정의의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값 · 모르는 열거 이름은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pElement = node.getName();
            if ( StringUtil::equals( pElement, "Board", true ) )
            {
                LeaderboardDefinition board;
                if ( readBoard( node, sourceName, board ) )
                {
                    _listBoard.push_back( std::move( board ) );
                    ++loadedCount;
                }
            }
            else if ( StringUtil::equals( pElement, "Achievement", true ) )
            {
                AchievementDefinition achievement;
                if ( GameDataXML::readFields( node, achievement, sourceName ) )
                {
                    _listAchievement.push_back( std::move( achievement ) );
                    ++loadedCount;
                }
            }
            else
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, pElement );
            }
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Board> or <Achievement> entries", sourceName );
        return loadedCount;
    }

    bool LeaderboardCatalog::readBoard( const XMLNode& node, string_view sourceName, LeaderboardDefinition& outBoard )
    {
        bool bValid = GameDataXML::readFields( node, outBoard, sourceName );
        for ( XMLNode seasonNode = node.findChild( "Season" ); seasonNode; seasonNode = seasonNode.findNextSibling( "Season" ) )
        {
            LeaderboardSeason season;
            bValid = GameDataXML::readFields( seasonNode, season, sourceName ) && bValid;
            for ( XMLNode rewardNode = seasonNode.findChild( "Reward" ); rewardNode; rewardNode = rewardNode.findNextSibling( "Reward" ) )
            {
                LeaderboardRewardTier tier;
                if ( GameDataXML::readFields( rewardNode, tier, sourceName ) )
                    season._listRewardTier.push_back( std::move( tier ) );
                else
                    bValid = false;
            }
            outBoard._listSeason.push_back( std::move( season ) );
        }
        return bValid;
    }
} // namespace sw
