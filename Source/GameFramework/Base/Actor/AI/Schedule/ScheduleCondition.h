/**
 * @file ScheduleCondition.h
 * @brief 일정 조건 — 요일 · 계절의 날 · 계절 · 날씨 · 하루의 때 · 게임 플래그 조건식 · 태그(관계 · 퀘스트) · 확률입니다.
 * @details 루틴 · 일정 칸 · 축제 · 약속이 같은 조건을 씁니다(스타듀 밸리의 요일 · 비 · 하트 일정, 스카이림 Radiant AI 패키지 조건).
 *          조건은 절(clause)마다 따로 판정해 어느 절이 막았는지 남깁니다 — "왜 여기 있나" 추적이 그것을 보여 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/World/World/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameFlags;
    class TagContainer;
    class XmlNode;

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
        Count
    };

    SW_GF_API const utf8* toString( ScheduleConditionClause clause );
} // namespace sw

namespace sw
{
    /** @brief 조건을 판정할 때의 세계 상태입니다. 빈 칸(널 포인터 · 빈 이름)은 "모름" 이고, 그 절을 요구하는 조건은 거짓입니다. */
    struct ScheduleConditionContext
    {
        const GameFlags*    _pFlags{ nullptr };
        const TagContainer* _pNpcTags{ nullptr };   ///< NPC 자신의 태그(관계 · 퀘스트 진행)
        const TagContainer* _pWorldTags{ nullptr }; ///< 세계 태그(축제 중 · 경보)
        hashed_string       _weekday{};
        hashed_string       _season{};
        hashed_string       _weather{};
        int32               _day{ 0 };         ///< 0 부터 센 전체 날 — 확률 굴림의 열쇠
        int32               _dayOfSeason{ 0 }; ///< 1 부터
        uint32              _chanceKey{ 0 };   ///< 확률 굴림의 열쇠(씨앗 · NPC · 루틴을 섞은 값)
        DayPhase            _phase{ DayPhase::Day };
    };
} // namespace sw

namespace sw
{
    /** @brief 판정 결과 — 본 절과 막은 절의 비트입니다. */
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
     *     <Routine id="rainy" priority="10" weathers="rain,storm" days="Sat,Sun" seasons="Spring" daysOfSeason="1,2"
     *              phases="Day" flags="bridgeFixed && !festival" tags="Relationship.Player.Friend" notTags="Quest.Missing" chance="0.5"/>
     * @endcode
     * @details 확률은 날 · 열쇠의 해시로 굴려 같은 씨앗이면 같은 날 같은 답입니다(저장 · 재생 · 화면 밖 시뮬레이션이 같은 길을 간다).
     */
    struct SW_GF_API ScheduleCondition
    {
        vector<hashed_string> _listWeekday{};
        vector<hashed_string> _listSeason{};
        vector<hashed_string> _listWeather{};
        vector<int32>         _listDayOfSeason{};
        vector<TagID>         _listRequiredTag{};
        vector<TagID>         _listForbiddenTag{};
        string                _flags{}; ///< `GameFlags` 조건식
        float32               _chance{ 1.0f };
        uint8                 _phaseMask{ 0 }; ///< `makeDayPhaseBit` 의 합 — 0 이면 언제나

        /** @brief 조건 속성 이름이면 true 입니다(읽는 쪽이 모르는 속성을 가릴 때). */
        static bool isConditionAttribute( const utf8* pName );
        /** @brief "a, b;c" 를 이름 목록으로 읽습니다. */
        static void parseNameList( string_view text, vector<hashed_string>& outListName );
        /** @brief "Dawn,Day" 를 때 마스크로 읽습니다. 모르는 이름은 경고합니다(@p ownerName 이 경고에 든다). */
        static uint8 parsePhaseMask( string_view text, string_view sourceName, string_view ownerName );

        /** @brief 조건 속성을 모두 읽습니다. 어휘에 없는 이름 · 문법이 틀린 플래그 식은 경고합니다(데이터 검사가 잡는다). */
        void readFromNode( const XmlNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName, string_view ownerName );

        bool                    isEmpty() const;
        ScheduleConditionResult evaluate( const ScheduleConditionContext& context ) const;
        bool                    matches( const ScheduleConditionContext& context ) const { return evaluate( context ).isPassed(); }
    };

    /** @brief 하루의 때 → 조건 마스크 비트입니다. */
    constexpr uint8 makeDayPhaseBit( DayPhase phase ) { return static_cast<uint8>( 1u << static_cast<uint32>( phase ) ); }
} // namespace sw
