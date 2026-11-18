/**
 * @file PackCookRuleTypes.h
 * @brief 팩 쿠킹 규칙 가운데 엔진 설정(`PackConfig`)과 게임 설정(`GameConfig::_cookRules`)이 함께 쓰는 형식입니다.
 * @details 게임 설정(설정 층)이 이 형식을 칸으로 가지므로 설정 층에 둔다 — 팩 층(`PackConfig.h`)이 이것을 include 한다(반대 방향은 층 순환).
 *          합치는 함수는 `PackCookRules::mergeGameRules` 하나다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct PackRuleScope
     * @brief 합친 쿠킹 규칙 하나가 닿는 팩입니다(`PackCookRules::mergeGameRules` 만 채운다 — 설정 파일의 칸이 아니다).
     * @details 모든 게임을 한 번에 굽는 쿠킹도 팩마다 "엔진 규칙 + 그 게임 규칙만" 으로 판정하게 하는 형식이다. 게임 규칙은 그 게임 팩에만(`_packRoot`),
     *          게임이 같은 이름으로 덮은 엔진 규칙은 그 게임 팩을 뺀 곳에만(`_listExcludedPackRoot`) 닿는다 — 덮기가 다른 팩에 새지 않는다.
     */
    struct PackRuleScope
    {
        string         _packRoot{};             ///< 비면 모든 팩, 아니면 이 팩(`game/nilecity`) 아래 파일에만
        vector<string> _listExcludedPackRoot{}; ///< 이 팩들 아래 파일에는 닿지 않는다(그 게임이 같은 이름으로 덮었다)

        /** @brief 리소스 id @p assetID 의 파일에 이 규칙이 닿으면 true 입니다. */
        bool isApplicableTo( string_view assetID ) const
        {
            if ( _packRoot.empty() == false && isUnder( assetID, _packRoot ) == false )
                return false;
            for ( const string& excluded : _listExcludedPackRoot )
            {
                if ( isUnder( assetID, excluded ) )
                    return false;
            }
            return true;
        }

        /** @brief @p assetID 가 팩 루트 @p packRoot 아래이면 true 입니다(대소문자 무시 — 리소스 id 는 소문자로 정규화돼 있다). */
        static bool isUnder( string_view assetID, string_view packRoot )
        {
            return assetID.size() > packRoot.size() && assetID[packRoot.size()] == '/' && StringUtil::equals( assetID.substr( 0, packRoot.size() ), packRoot, true );
        }
    };

    /**
     * @struct PackAlwaysCookRule
     * @brief 참조가 없어도 담는 파일의 이름 붙은 묶음입니다(언리얼 Primary Asset Rules 의 `AlwaysCook` · Directories to Always Cook).
     * @details 코드가 이름으로 읽는 것(엔진 기본 에셋 · 설정, 시나리오, 종류 경로 규칙으로 찾는 데이터)처럼 참조 칸으로 닿지 않는 파일만 적는다. 이 묶음의 파일은 뿌리라 그 참조도 따라간다.
     *          패턴은 리소스 id(`engine/…` · `game/<팩>/…`)에 `StringUtil::matchesPathPattern` 으로 맞춘다.
     */
    REFLECT()
    struct SW_API PackAlwaysCookRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 묶음 이름 — 왜 참조로 닿지 않는지를 이름으로 적는다(로그 · 문서용)

        PROPERTY( Required )
        vector<string> _listPattern; ///< 리소스 id 패턴(`game/*/maps/**`)

        PROPERTY( Required )
        bool _bAutomationOnly{ SW_FALSE }; ///< 참이면 자동화 실행기가 있는 구성(개발 구성 클라이언트)만 담는다 — 시나리오는 실행기가 없으면 읽히지 않는다

        PackRuleScope _scope{}; ///< 합친 뒤 이 규칙이 닿는 팩(설정 파일의 칸이 아니다 — `mergeGameRules` 가 채운다)
    };

    /**
     * @struct PackEntryIDField
     * @brief 카탈로그 항목을 id 로 가리키는 칸 하나입니다(속성 · 요소 글 · `<item>` 목록, 쉼표로 여럿). 경로가 아니라 이름이라 참조 칸 표로는 보이지 않는다.
     * @details 표에 없는 칸으로만 불리는 항목의 에셋은 빠진다. 없는 id 는 담을 것이 없어 넘어간다(이름을 찾는 일은 런타임 검증의 몫이다).
     */
    REFLECT()
    struct SW_API PackEntryIDField
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _field; ///< 칸 이름(`visual`)

        PROPERTY( Required )
        string _kind; ///< 가리키는 카탈로그의 이름 공간(`visual`)

        PackRuleScope _scope{}; ///< 합친 뒤 이 칸이 닿는 팩(설정 파일의 칸이 아니다 — `mergeGameRules` 가 채운다)
    };

    /**
     * @struct GameCookRules
     * @brief 게임 하나가 엔진 쿠킹 규칙(`PackConfig::_cookRules`) 뒤에 더하는 규칙입니다(`Config/Game/<게임>.json` 의 `_cookRules`).
     * @details 게임 코드 · 데이터만 아는 값(이름으로 이어 붙이는 모델 경로, 그 게임의 프리셋 칸)을 엔진 공용 설정에 두지 않는다. 항상 담기 패턴은 그 게임 팩 루트 기준이다
     *          (`models/` 아래 모두 — 합칠 때 `game/<팩>/` 를 붙인다). 칸이 없으면 더할 것이 없다.
     */
    REFLECT()
    struct SW_API GameCookRules
    {
        REFLECT_BODY();

        PROPERTY()
        vector<PackAlwaysCookRule> _listAlwaysCook; ///< 그 게임 팩 안에서 참조 없이 담는 묶음(패턴은 팩 루트 기준)

        PROPERTY()
        vector<PackEntryIDField> _listEntryIDField; ///< 그 게임만 쓰는 카탈로그 id 칸
    };
} // namespace sw
