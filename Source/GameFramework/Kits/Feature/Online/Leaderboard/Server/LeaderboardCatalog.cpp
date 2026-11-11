#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    uint32 LeaderboardCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 정의의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값 · 모르는 열거 이름은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<LeaderboardCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&LeaderboardCatalog::loadBoard>( "Board" ),
            Element::of<&LeaderboardCatalog::_listAchievement>( "Achievement" ),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool LeaderboardCatalog::loadBoard( const XMLNode& node, string_view sourceName )
    {
        LeaderboardDef board;
        if ( readBoard( node, sourceName, board ) == false )
            return false;
        _listBoard.push_back( std::move( board ) );
        return true;
    }

    bool LeaderboardCatalog::readBoard( const XMLNode& node, string_view sourceName, LeaderboardDef& outBoard )
    {
        bool bValid = XMLFields::readProperties( node, outBoard, sourceName );
        for ( XMLNode seasonNode = node.findChild( "Season" ); seasonNode; seasonNode = seasonNode.findNextSibling( "Season" ) )
        {
            LeaderboardSeason season;
            bValid = XMLFields::readProperties( seasonNode, season, sourceName ) && bValid;
            for ( XMLNode rewardNode = seasonNode.findChild( "Reward" ); rewardNode; rewardNode = rewardNode.findNextSibling( "Reward" ) )
            {
                LeaderboardRewardTier tier;
                if ( XMLFields::readProperties( rewardNode, tier, sourceName ) )
                    season._listRewardTier.push_back( std::move( tier ) );
                else
                    bValid = false;
            }
            outBoard._listSeason.push_back( std::move( season ) );
        }
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<LeaderboardCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( LeaderboardCatalog, LeaderboardCatalog, SW_KIT_SERVER_API, LeaderboardCatalog::kAssetType.withLoader( AssetTypeLoader::of<LeaderboardCatalog>( MemoryTag::Game ) ) );
} // namespace sw
