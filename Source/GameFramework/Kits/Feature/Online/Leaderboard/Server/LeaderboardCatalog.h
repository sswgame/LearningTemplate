/**
 * @file LeaderboardCatalog.h
 * @brief 순위표 보드와 업적의 정의 데이터입니다. 서버 조립이 기동 때 읽어 순위표 서비스에 등록합니다.
 * @details 형식은 다음과 같습니다. 속성을 빼면 정의의 기본값입니다. 규칙(id 형식, 시즌 겹침 등)은 등록할 때 순위표 서비스가 봅니다.
 * @code
 *     <LeaderboardCatalog>
 *         <Board id="kills" order="descending" update="best" reset="weekly" resetMinute="0" resetDay="0" sourceStat="kills" clientSubmit="false">
 *             <Season id="1" startMs="1767225600000" endMs="1769904000000">
 *                 <Reward from="1" to="10" asset="cur.gem" amount="100" mailTitle="mail.season_reward"/>
 *             </Season>
 *         </Board>
 *         <Achievement id="first_blood" stat="kills" threshold="1" rewardAsset="cur.gold" rewardAmount="100" mailTitle="mail.achievement"/>
 *     </LeaderboardCatalog>
 * @endcode
 *          순위표 정의를 데이터로 두는 것은 PlayFab 의 Leaderboard 정의나 EOS 개발자 포털의 Leaderboard 설정에 해당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"

namespace sw
{
    class XMLNode;

    /**
     * @class LeaderboardCatalog
     * @brief 보드와 업적 정의 모음입니다. 읽은 뒤에는 바꾸지 않습니다.
     */
    class SW_KIT_SERVER_API LeaderboardCatalog : public XMLCatalog<LeaderboardCatalog>
    {
        friend class XMLCatalog<LeaderboardCatalog>;

    public:
        const vector<LeaderboardDefinition>& getBoards() const { return _listBoard; }
        const vector<AchievementDefinition>& getAchievements() const { return _listAchievement; }

    private:
        static constexpr const utf8* kXMLRootName = "LeaderboardCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Board>` 와 그 `<Season>` · `<Reward>` 를 스키마로 읽습니다. 하나라도 틀리면 오류를 남기고 false 입니다(그 보드는 넣지 않는다). */
        [[nodiscard]] static bool readBoard( const XMLNode& node, string_view sourceName, LeaderboardDefinition& outBoard );

        vector<LeaderboardDefinition> _listBoard{};
        vector<AchievementDefinition> _listAchievement{};
    };
} // namespace sw
