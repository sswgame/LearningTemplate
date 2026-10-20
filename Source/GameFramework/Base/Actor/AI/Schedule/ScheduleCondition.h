/**
 * @file ScheduleCondition.h
 * @brief 일정 조건입니다. 요일, 계절의 날(범위 가능), 주, 날짜 범위, 계절, 날씨, 하루의 때, 게임 플래그 조건식, 태그(관계, 퀘스트), 호감도 단계, 확률을 봅니다.
 * @details 루틴, 일정 블록, 축제, 약속이 같은 조건을 씁니다. 스타듀 밸리의 요일과 비와 하트 일정, 스카이림 Radiant AI 패키지 조건에 해당합니다.
 *          조건은 절(clause)마다 따로 판정해 어느 절이 막았는지 남깁니다. "왜 여기 있나" 추적이 그것을 보여 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class TagContainer;
    class XMLNode;

    /** @brief 조건의 절입니다. 비트로 결과를 담습니다. */
    enum class ScheduleConditionClause : uint8
    {
        Weekday = 0,
        DayOfSeason,
        Season,
        Weather,
        Phase,
        Flags,
        Tags,
        Chance,
        Week,      ///< 계절의 몇째 주인지(`weeks`). 1 부터 세며 1 ~ 7 일이 첫째 주다
        DateRange, ///< 계절을 넘나드는 날짜 범위(`from`, `to`)
        Affinity,  ///< NPC 의 호감도(관계 단계) 범위(`minAffinity`, `maxAffinity`)
        Count
    };

    SW_GF_API const utf8* toString( ScheduleConditionClause clause );
} // namespace sw

namespace sw
{
    /** @brief 조건을 판정할 때의 세계 상태입니다. 빈 필드(널 포인터나 빈 이름)는 "모름" 이고, 그 절을 요구하는 조건은 거짓입니다. */
    struct ScheduleConditionContext
    {
        const GameFlags*             _pFlags{ nullptr };
        const TagContainer*          _pNpcTags{ nullptr };   ///< NPC 자신의 태그(관계, 퀘스트 진행)
        const TagContainer*          _pWorldTags{ nullptr }; ///< 세계 태그(축제 중, 경보)
        hashed_string                _weekday{};
        hashed_string                _season{};
        hashed_string                _weather{};
        int32                        _day{ 0 };                ///< 0 부터 센 전체 날. 확률 굴림의 키로 쓴다
        const vector<hashed_string>* _pSeasonOrder{ nullptr }; ///< 한 해의 계절 순서(날짜 범위가 비교한다). 없으면 날짜 범위 절은 거짓이다.
        int32                        _dayOfSeason{ 0 };        ///< 1 부터
        int32                        _affinity{ 0 };           ///< NPC 의 호감도(관계 단계). 게임이 `ScheduleSystem::setNpcAffinity` 로 넣는다.
        uint32                       _chanceKey{ 0 };          ///< 확률 굴림의 키. 시드, NPC, 루틴을 섞은 값이다
        DayPhase                     _phase{ DayPhase::Day };
    };
} // namespace sw

namespace sw
{
    /** @brief 판정 결과입니다. 본 절과 막은 절을 비트로 담습니다. */
    struct ScheduleConditionResult
    {
        uint16 _checkedMask{ 0 };
        uint16 _failedMask{ 0 };

        bool isPassed() const { return _failedMask == 0; }
        bool hasChecked( ScheduleConditionClause clause ) const { return ( _checkedMask & ( 1u << static_cast<uint32>( clause ) ) ) != 0; }
        bool hasFailed( ScheduleConditionClause clause ) const { return ( _failedMask & ( 1u << static_cast<uint32>( clause ) ) ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 조건이 쓰는 이름의 목록입니다. 목록이 비면 그 종류의 이름은 검사하지 않습니다(아무 이름이나 받는다). */
    struct ScheduleConditionVocabulary
    {
        vector<hashed_string> _listWeekday{};
        vector<hashed_string> _listSeason{};
        vector<hashed_string> _listWeather{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ScheduleCondition
     * @brief 일정 조건 하나입니다. 모든 절이 참이어야 참이고, 적지 않은 절은 늘 참입니다.
     * @code
     *     <Routine id="rainy" priority="10" weathers="rain,storm" days="Sat,Sun" seasons="Spring" daysOfSeason="1,2,5-9" weeks="1,3"
     *              from="Spring 20" to="Summer 5" phases="Day" flags="bridgeFixed && !festival" tags="Relationship.Player.Friend" notTags="Quest.Missing"
     *              minAffinity="4" maxAffinity="7" chance="0.5"/>
     * @endcode
     * @details 확률은 날과 키의 해시로 굴립니다. 그래서 시드가 같으면 같은 날 같은 답이 나오고, 저장, 재생, 화면 밖 시뮬레이션이 같은 결과를 냅니다.
     */
    struct SW_GF_API ScheduleCondition
    {
        vector<hashed_string> _listWeekday{};
        vector<hashed_string> _listSeason{};
        vector<hashed_string> _listWeather{};
        vector<int32>         _listDayOfSeason{};
        vector<int32>         _listWeek{};
        vector<TagID>         _listRequiredTag{};
        vector<TagID>         _listForbiddenTag{};
        string                _flags{}; ///< `GameFlags` 조건식
        hashed_string         _fromSeason{};
        hashed_string         _toSeason{};
        int32                 _fromDay{ 0 };                       ///< 0 이면 시작 한계가 없다
        int32                 _toDay{ 0 };                         ///< 0 이면 끝 한계가 없다
        int32                 _minAffinity{ MathUtil::kMinInt32 }; ///< 호감도 하한(없으면 `kMinInt32`)
        int32                 _maxAffinity{ MathUtil::kMaxInt32 }; ///< 호감도 상한(없으면 `kMaxInt32`)
        float32               _chance{ 1.0f };
        uint8                 _phaseMask{ 0 }; ///< `makeDayPhaseBit` 의 합. 0 이면 언제나

        /** @brief 조건 속성 이름이면 true 입니다. 읽는 쪽이 모르는 속성을 가려낼 때 씁니다. */
        static bool isConditionAttribute( const utf8* pName );
        /** @brief "a, b;c" 를 이름 목록으로 읽습니다. */
        static void parseNameList( string_view text, vector<hashed_string>& outListName );
        /** @brief "Dawn,Day" 를 때 마스크로 읽습니다. 모르는 이름은 @p ownerName 과 함께 경고합니다. */
        static uint8 parsePhaseMask( string_view text, string_view sourceName, string_view ownerName );

        /**
         * @brief 조건 속성을 모두 읽습니다. 어휘에 없는 이름은 경고합니다(데이터 검사가 잡는다).
         * @return 깨진 숫자 · 범위 밖 확률 · 뒤집힌 호감도 범위 · 문법이 틀린 플래그 식이 있으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool readFromNode( const XMLNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName, string_view ownerName );

        bool                    isEmpty() const;
        ScheduleConditionResult evaluate( const ScheduleConditionContext& context ) const;
        bool                    matches( const ScheduleConditionContext& context ) const { return evaluate( context ).isPassed(); }
    };

    /** @brief 하루의 때에 해당하는 조건 마스크 비트입니다. */
    constexpr uint8 makeDayPhaseBit( DayPhase phase )
    {
        return static_cast<uint8>( 1u << static_cast<uint32>( phase ) );
    }
} // namespace sw
