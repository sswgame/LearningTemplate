/**
 * @file MatchModeCatalog.h
 * @brief 매칭 모드 정의 데이터입니다. 서버 조립이 기동 때 읽어 매칭 대기열에 넘깁니다.
 * @details 형식은 다음과 같습니다. 속성을 빼면 `MatchModeDefinition` 의 기본값입니다. 팀 수 · 인원은 읽을 때 `MatchModeDefinition` 의 선언된 범위(`ConfigRangeUtil`)로 거절하고, id 형식은 대기열이 등록할 때 봅니다.
 * @code
 *     <MatchModeCatalog>
 *         <Mode id="duo" teamCount="2" teamSize="2" serverKind="game" regionRelaxMs="30000" maxWaitMs="120000"
 *               baseRatingWindow="100" ratingWindowPerSecond="25" maxRatingWindow="1000" buildVersion="0"/>
 *     </MatchModeCatalog>
 * @endcode
 *          도구와 테스트처럼 데이터 파일이 없는 조립은 `parseCompactMode`(`<id>:<팀 수>x<팀 인원>`)로 같은 정의를 만듭니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    class XMLNode;

    /**
     * @class MatchModeCatalog
     * @brief 매칭 모드 정의 모음입니다. 읽은 뒤에는 바꾸지 않습니다.
     */
    class SW_KIT_SERVER_API MatchModeCatalog : public XMLCatalog<MatchModeCatalog>
    {
        friend class XMLCatalog<MatchModeCatalog>;

    public:
        const vector<MatchModeDefinition>& getModes() const { return _listMode; }

        /** @brief `<id>:<팀 수>x<팀 인원>`(예: `duo:2x2`)을 읽습니다. 나머지 값은 기본값입니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseCompactMode( string_view text, MatchModeDefinition& outMode );

    private:
        static constexpr const utf8* kXMLRootName = "MatchModeCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        vector<MatchModeDefinition> _listMode{};
    };
} // namespace sw
