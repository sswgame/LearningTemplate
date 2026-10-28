/**
 * @file MatchModeCatalog.h
 * @brief 매칭 모드 정의 데이터입니다. 서버 조립이 기동 때 읽어 매칭 대기열에 넘깁니다.
 * @details 형식은 다음과 같습니다. 속성을 빼면 `MatchModeDefinition` 의 기본값입니다. 속성은 `MatchModeDefinition` 스키마로 읽고(`id` 만 손으로 읽는다) 모르는 속성 · 범위 밖 값은 그 모드를 넣지 않는 오류입니다. id 형식은 대기열이 등록할 때 봅니다.
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

        /** @brief `<id>:<팀 수>x<팀 인원>`(예: `duo:2x2`)을 읽습니다. 나머지 값은 기본값입니다. 형식이 틀리거나 범위 밖이면 false 이고 @p outError 에 이유를 적습니다(범위를 못 보는 프로세스면 그 사실). */
        [[nodiscard]] static bool parseCompactMode( string_view text, MatchModeDefinition& outMode, string& outError );

    private:
        static constexpr const utf8* kXMLRootName = "MatchModeCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Mode>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadMode( const XMLNode& node, string_view sourceName );

        vector<MatchModeDefinition> _listMode{};
    };
} // namespace sw
