#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCondition.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Gameplay/Condition/GameConditions.h"

namespace sw
{
    namespace
    {
        struct ScheduleConditionInternal
        {
            static constexpr const utf8* kNameSeparators = ",;| ";

            static constexpr const utf8* kArrConditionAttribute[] = { "days", "daysOfSeason", "weeks", "from", "to", "seasons", "weathers",
                                                                      "phases", "flags", "tags", "minAffinity", "maxAffinity", "notTags", "chance" };
            /** @brief 날짜 범위 비교용 열쇠에서 계절 한 칸의 폭입니다(계절당 날 수는 이보다 작다). */
            static constexpr int32 kSeasonKeyStride = 1000;
            /** @brief 한 주의 날 수입니다. */
            static constexpr int32 kDaysPerWeek = 7;

            /** @brief 계절 순서에서 @p season 의 번호입니다. 모르면 비어 있습니다. */
            static optional<int32> findSeasonIndex( const vector<hashed_string>* pListSeason, const hashed_string& season )
            {
                if ( pListSeason == nullptr )
                    return nullopt;
                for ( size_t index = 0; index < pListSeason->size(); ++index )
                {
                    if ( ( *pListSeason )[index] == season )
                        return static_cast<int32>( index );
                }
                return nullopt;
            }

            /** @brief "5-9, 12" 를 계절의 날 목록으로 읽습니다. 범위(`a-b`)는 풀어 넣습니다. */
            static void parseDayList( string_view text, vector<int32>& outListDay, int32 maxDay, string_view sourceName, string_view ownerName, const utf8* pWhat )
            {
                outListDay.clear();
                StringUtil::forEachToken( text, kNameSeparators, [&]( string_view token )
                {
                    const size_t dashPos = token.find( '-' );
                    int32        first{ 0 };
                    int32        last{ 0 };
                    bool         bValid{ false };
                    if ( dashPos != string_view::npos )
                    {
                        bValid = StringUtil::parseInt( token.substr( 0, dashPos ), first ) && StringUtil::parseInt( token.substr( dashPos + 1 ), last );
                    }
                    else
                    {
                        bValid = StringUtil::parseInt( token, first );
                        last   = first;
                    }
                    const bool bInvalidDayRange = bValid == false || first < 1 || last < first || maxDay < last;
                    if ( bInvalidDayRange )
                    {
                        SW_LOG_WARNING_ID( "Schedule.DayListInvalid", "%#: '%#' has an invalid %# '%#'", sourceName, ownerName, pWhat, token );
                        return;
                    }
                    for ( int32 day = first; day <= last; ++day )
                    {
                        outListDay.push_back( day );
                    }
                } );
            }

            /** @brief "Spring 20" 를 읽습니다. 비어 있으면 true 이고 날은 0 입니다(한계 없음). */
            [[nodiscard]] static bool parseDate( string_view text, const vector<hashed_string>& listKnownSeason, hashed_string& outSeason, int32& outDay, string_view sourceName,
                                                 string_view ownerName, const utf8* pWhat )
            {
                outSeason = hashed_string{};
                outDay    = 0;
                if ( StringUtil::trim( text ).empty() )
                    return true;
                string_view seasonName;
                string_view dayText;
                StringUtil::forEachToken( text, kNameSeparators, [&]( string_view token )
                {
                    if ( seasonName.empty() )
                        seasonName = token;
                    else if ( dayText.empty() )
                        dayText = token;
                } );
                int32      day{ 0 };
                const bool bInvalidSeasonDate = seasonName.empty() || StringUtil::parseInt( dayText, day ) == false || day < 1;
                if ( bInvalidSeasonDate )
                {
                    SW_LOG_WARNING_ID( "Schedule.DateInvalid", "%#: '%#' has an invalid %# date '%#' (expected \"Season day\")", sourceName, ownerName, pWhat, text );
                    return false;
                }
                outSeason = hashed_string( seasonName );
                outDay    = day;
                warnUnknownNames( { outSeason }, listKnownSeason, "season", "Schedule.SeasonUnknown", sourceName, ownerName );
                return true;
            }

            /** @brief 어휘가 있으면 목록의 이름이 어휘에 있는지 봅니다. */
            static void warnUnknownNames( const vector<hashed_string>& listName, const vector<hashed_string>& listKnown, const utf8* pKind, const utf8* pID, string_view sourceName,
                                          string_view ownerName )
            {
                if ( listKnown.empty() )
                    return;
                for ( const hashed_string& name : listName )
                {
                    if ( VectorUtil::contains( listKnown, name ) == false )
                        SW_LOG_WRITE( sw::LogLevel::Warning, pID, "%#: '%#' has an unknown %# '%#'", sourceName, ownerName, pKind, name.c_str() );
                }
            }

            /** @brief 낱말 목록을 쉼표로 이은 글입니다. 호출 인자로 씁니다. */
            static string joinTokens( string_view text )
            {
                string joined;
                StringUtil::forEachToken( text, kNameSeparators, [&]( string_view token )
                {
                    joined += joined.empty() ? "" : ", ";
                    joined += token;
                } );
                return joined;
            }

            /** @brief 수 목록을 쉼표로 이은 글입니다. */
            static string joinNumbers( const vector<int32>& listValue )
            {
                string joined;
                for ( const int32 value : listValue )
                {
                    joined += joined.empty() ? "" : ", ";
                    joined += to_string( value );
                }
                return joined;
            }

            /** @brief 절 하나를 컴파일해 더합니다. */
            [[nodiscard]] static bool addClause( ScheduleCondition& inoutCondition, ScheduleConditionClause clause, string_view text, string_view sourceName )
            {
                ScheduleConditionClauseExpr entry;
                entry._clause = clause;
                if ( ConditionExpr::compile( text, sourceName, entry._expr ) == false )
                    return false;
                inoutCondition._listClause.push_back( std::move( entry ) );
                return true;
            }

            static bool readWeekday( const GameplayContext& context, const ConditionNameListArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listName, pSchedule->_weekday );
            }

            static bool readSeason( const GameplayContext& context, const ConditionNameListArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listName, pSchedule->_season );
            }

            static bool readWeather( const GameplayContext& context, const ConditionNameListArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listName, pSchedule->_weather );
            }

            static bool readPhase( const GameplayContext& context, const ScheduleDayPhaseArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listPhase, pSchedule->_phase );
            }

            static bool readDayOfSeason( const GameplayContext& context, const ScheduleNumberListArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listValue, pSchedule->_dayOfSeason );
            }

            static bool readWeek( const GameplayContext& context, const ScheduleNumberListArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr && VectorUtil::contains( args._listValue, ( pSchedule->_dayOfSeason - 1 ) / kDaysPerWeek + 1 );
            }

            static float64 readAffinity( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                return pSchedule != nullptr ? static_cast<float64>( pSchedule->_affinity ) : 0.0;
            }

            static bool readDate( const GameplayContext& context, const ScheduleDateArgs& args )
            {
                const ScheduleConditionContext* pSchedule = context.find<ScheduleConditionSlot>();
                if ( pSchedule == nullptr )
                    return false;
                // 날짜를 (계절 번호, 날) 열쇠로 바꿔 비교한다. 계절 순서를 모르면(문맥에 없으면) 거짓이다.
                const optional<int32> currentSeason = findSeasonIndex( pSchedule->_pSeasonOrder, pSchedule->_season );
                const optional<int32> fromSeason    = args._fromDay > 0 ? findSeasonIndex( pSchedule->_pSeasonOrder, args._fromSeason ) : optional<int32>{ 0 }; // 날이 없으면 쓰지 않는 칸이다
                const optional<int32> toSeason      = args._toDay > 0 ? findSeasonIndex( pSchedule->_pSeasonOrder, args._toSeason ) : optional<int32>{ 0 };
                if ( currentSeason.has_value() == false || fromSeason.has_value() == false || toSeason.has_value() == false )
                    return false;
                const int32 current = *currentSeason * kSeasonKeyStride + pSchedule->_dayOfSeason;
                const int32 from    = *fromSeason * kSeasonKeyStride + args._fromDay;
                const int32 to      = *toSeason * kSeasonKeyStride + args._toDay;
                if ( args._fromDay > 0 && args._toDay > 0 )
                    return from <= to ? ( from <= current && current <= to ) : ( from <= current || current <= to );
                return args._fromDay > 0 ? from <= current : current <= to;
            }

            static void mark( ScheduleConditionResult& inoutResult, ScheduleConditionClause clause, bool bPassed )
            {
                const uint16 bit         = static_cast<uint16>( 1u << static_cast<uint32>( clause ) );
                inoutResult._checkedMask = static_cast<uint16>( inoutResult._checkedMask | bit );
                if ( bPassed == false )
                    inoutResult._failedMask = static_cast<uint16>( inoutResult._failedMask | bit );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( ScheduleConditionClause clause )
    {
        switch ( clause )
        {
            case ScheduleConditionClause::Weekday:
                return "days";
            case ScheduleConditionClause::DayOfSeason:
                return "daysOfSeason";
            case ScheduleConditionClause::Season:
                return "seasons";
            case ScheduleConditionClause::Weather:
                return "weathers";
            case ScheduleConditionClause::Phase:
                return "phases";
            case ScheduleConditionClause::Flags:
                return "flags";
            case ScheduleConditionClause::Tags:
                return "tags";
            case ScheduleConditionClause::Chance:
                return "chance";
            case ScheduleConditionClause::Week:
                return "weeks";
            case ScheduleConditionClause::DateRange:
                return "from/to";
            case ScheduleConditionClause::Affinity:
                return "minAffinity/maxAffinity";
            case ScheduleConditionClause::Count:
                return "count";
        }
        return "unknown";
    }

    bool ScheduleCondition::isConditionAttribute( const utf8* pName )
    {
        for ( const utf8* pKnown : ScheduleConditionInternal::kArrConditionAttribute )
        {
            if ( StringUtil::equals( pName, pKnown ) )
                return true;
        }
        return false;
    }

    void ScheduleCondition::parseNameList( string_view text, vector<hashed_string>& outListName )
    {
        XMLReadUtil::parseNameList( text, ScheduleConditionInternal::kNameSeparators, outListName );
    }

    uint8 ScheduleCondition::parsePhaseMask( string_view text, string_view sourceName, string_view ownerName )
    {
        uint8 mask = 0;
        StringUtil::forEachToken( text, ScheduleConditionInternal::kNameSeparators, [&]( string_view token )
        {
            const hashed_string name( token );
            bool                bFound = false;
            for ( uint32 phaseIndex = 0; phaseIndex <= static_cast<uint32>( DayPhase::Dusk ); ++phaseIndex )
            {
                const DayPhase phase = static_cast<DayPhase>( phaseIndex );
                if ( name == hashed_string( getEnumName( phase ) ) )
                {
                    mask   = static_cast<uint8>( mask | ( 1u << phaseIndex ) );
                    bFound = true;
                }
            }
            if ( bFound == false )
                SW_LOG_WARNING_ID( "ScheduleCondition.UnknownPhase", "%#: '%#' has an unknown phase '%#'", sourceName, ownerName, token );
        } );
        return mask;
    }

    bool ScheduleCondition::loadFromXML( const XMLNode& node, const ScheduleConditionSchema& vocabulary, string_view sourceName, string_view ownerName )
    {
        using Internal = ScheduleConditionInternal;
        _listClause.clear();
        vector<hashed_string> listWeekday;
        vector<hashed_string> listSeason;
        vector<hashed_string> listWeather;
        parseNameList( node.getAttributeText( "days" ), listWeekday );
        parseNameList( node.getAttributeText( "seasons" ), listSeason );
        parseNameList( node.getAttributeText( "weathers" ), listWeather );
        Internal::warnUnknownNames( listWeekday, vocabulary._listWeekday, "day", "Schedule.DayUnknown", sourceName, ownerName );
        Internal::warnUnknownNames( listSeason, vocabulary._listSeason, "season", "Schedule.SeasonUnknown", sourceName, ownerName );
        Internal::warnUnknownNames( listWeather, vocabulary._listWeather, "weather", "Schedule.WeatherUnknown", sourceName, ownerName );

        // 계절의 날은 1 부터 366 까지(`a-b` 범위 가능). 주는 1 부터 53 까지.
        vector<int32> listDayOfSeason;
        vector<int32> listWeek;
        Internal::parseDayList( node.getAttributeText( "daysOfSeason" ), listDayOfSeason, 366, sourceName, ownerName, "day of season" );
        Internal::parseDayList( node.getAttributeText( "weeks" ), listWeek, 53, sourceName, ownerName, "week" );

        // 계절을 넘나드는 날짜 범위. 둘 다 적으면 `from` 이 `to` 보다 늦을 때 해를 넘는 범위다(Winter 20 ~ Spring 5).
        hashed_string fromSeason;
        hashed_string toSeason;
        int32         fromDay = 0;
        int32         toDay   = 0;
        if ( Internal::parseDate( node.getAttributeText( "from" ), vocabulary._listSeason, fromSeason, fromDay, sourceName, ownerName, "from" ) == false )
            fromDay = 0;
        if ( Internal::parseDate( node.getAttributeText( "to" ), vocabulary._listSeason, toSeason, toDay, sourceName, ownerName, "to" ) == false )
            toDay = 0;

        // 호감도(관계 단계) 범위 — 한쪽만 적어도 된다. 적지 않은 쪽은 제한이 없다.
        bool              bValid = true;
        optional<int32>   minAffinity;
        optional<int32>   maxAffinity;
        optional<float32> chance;
        bValid = node.readOptionalAttribute( "minAffinity", minAffinity ) && bValid;
        bValid = node.readOptionalAttribute( "maxAffinity", maxAffinity ) && bValid;
        bValid = node.readOptionalAttribute( "chance", chance ) && bValid;
        if ( minAffinity.has_value() && maxAffinity.has_value() && *minAffinity > *maxAffinity )
        {
            SW_LOG_ERROR_ID( "Schedule.AffinityRangeEmpty", "%#: '%#' has minAffinity %# above maxAffinity %# - it can never match", sourceName, ownerName, *minAffinity, *maxAffinity );
            bValid = false;
        }
        if ( chance.has_value() && ( *chance < 0.0f || *chance > 1.0f ) )
        {
            SW_LOG_ERROR_ID( "ScheduleCondition.ChanceOutside", "%#: '%#' has chance %# outside [0, 1]", sourceName, ownerName, *chance );
            bValid = false;
        }

        // 속성은 공통 어휘의 줄임말이다 — 절마다 식 하나로 컴파일한다. 이름 · 수는 위에서 검사한 값이고, 여기서 틀릴 수 있는 것은 `flags` 뿐이다.
        const auto addCall = [&]( ScheduleConditionClause clause, const utf8* pName, const string& arguments )
        {
            if ( arguments.empty() == false )
                bValid = Internal::addClause( *this, clause, string( pName ) + "(" + arguments + ")", sourceName ) && bValid;
        };
        addCall( ScheduleConditionClause::Weekday, "weekday", Internal::joinTokens( node.getAttributeText( "days" ) ) );
        addCall( ScheduleConditionClause::DayOfSeason, "dayOfSeason", Internal::joinNumbers( listDayOfSeason ) );
        addCall( ScheduleConditionClause::Week, "week", Internal::joinNumbers( listWeek ) );
        if ( fromDay > 0 || toDay > 0 )
        {
            string arguments;
            if ( fromDay > 0 )
                arguments += string( "fromSeason=" ) + fromSeason.c_str() + ", fromDay=" + to_string( fromDay );
            if ( toDay > 0 )
                arguments += string( arguments.empty() ? "" : ", " ) + "toSeason=" + toSeason.c_str() + ", toDay=" + to_string( toDay );
            addCall( ScheduleConditionClause::DateRange, "date", arguments );
        }
        if ( minAffinity.has_value() || maxAffinity.has_value() )
        {
            string text;
            if ( minAffinity.has_value() )
                text += "affinity >= " + to_string( *minAffinity );
            if ( maxAffinity.has_value() )
                text += string( text.empty() ? "" : " && " ) + "affinity <= " + to_string( *maxAffinity );
            bValid = Internal::addClause( *this, ScheduleConditionClause::Affinity, text, sourceName ) && bValid;
        }
        addCall( ScheduleConditionClause::Season, "season", Internal::joinTokens( node.getAttributeText( "seasons" ) ) );
        addCall( ScheduleConditionClause::Weather, "weather", Internal::joinTokens( node.getAttributeText( "weathers" ) ) );
        const uint8 phaseMask = parsePhaseMask( node.getAttributeText( "phases" ), sourceName, ownerName );
        string      phaseNames;
        for ( uint32 phaseIndex = 0; phaseIndex <= static_cast<uint32>( DayPhase::Dusk ); ++phaseIndex )
        {
            if ( ( phaseMask & ( 1u << phaseIndex ) ) == 0 )
                continue;
            phaseNames += phaseNames.empty() ? "" : ", ";
            phaseNames += getEnumName( static_cast<DayPhase>( phaseIndex ) );
        }
        addCall( ScheduleConditionClause::Phase, "phase", phaseNames );

        const string_view flags = node.getAttributeText( "flags" );
        if ( StringUtil::trim( flags ).empty() == false && Internal::addClause( *this, ScheduleConditionClause::Flags, flags, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "Schedule.FlagsInvalid", "%#: '%#' has an invalid flags expression '%#'", sourceName, ownerName, flags );
            bValid = false;
        }

        string tagText;
        StringUtil::forEachToken( node.getAttributeText( "tags" ), Internal::kNameSeparators, [&]( string_view token )
        {
            tagText += tagText.empty() ? "" : " && ";
            tagText += "tag(" + string( token ) + ")";
        } );
        StringUtil::forEachToken( node.getAttributeText( "notTags" ), Internal::kNameSeparators, [&]( string_view token )
        {
            tagText += tagText.empty() ? "" : " && ";
            tagText += "!tag(" + string( token ) + ")";
        } );
        if ( tagText.empty() == false )
            bValid = Internal::addClause( *this, ScheduleConditionClause::Tags, tagText, sourceName ) && bValid;

        if ( chance.has_value() && *chance < 1.0f && *chance >= 0.0f )
            addCall( ScheduleConditionClause::Chance, "chance", string( node.getAttributeText( "chance" ) ) );
        return bValid;
    }

    void ScheduleConditionContext::fillGameplayContext( GameplayContext& outContext ) const
    {
        outContext.set<GameFlagsSlot>( _pFlags );
        outContext.set<SubjectTagsSlot>( _pNPCTags );
        outContext.set<WorldTagsSlot>( _pWorldTags );
        outContext.set<ScheduleConditionSlot>( this );
        outContext.setSeed( GameConditions::makeSeed( _day, static_cast<int32>( _chanceKey ) ) );
    }

    ScheduleConditionResult ScheduleCondition::evaluate( const ScheduleConditionContext& context ) const
    {
        using Internal = ScheduleConditionInternal;
        ScheduleConditionResult result;
        if ( _listClause.empty() )
            return result;
        GameplayContext world;
        context.fillGameplayContext( world );
        for ( const ScheduleConditionClauseExpr& entry : _listClause )
        {
            // 플래그를 받지 못한 문맥에서 플래그 절은 "모름" 이라 거짓이다(`!x` 도 참으로 보지 않는다).
            const bool bKnown = entry._clause != ScheduleConditionClause::Flags || context._pFlags != nullptr;
            Internal::mark( result, entry._clause, bKnown && entry._expr.evaluate( world ) );
        }
        return result;
    }

    SW_CONDITION( weekday, ConditionNameListArgs, &ScheduleConditionInternal::readWeekday );
    SW_CONDITION( season, ConditionNameListArgs, &ScheduleConditionInternal::readSeason );
    SW_CONDITION( weather, ConditionNameListArgs, &ScheduleConditionInternal::readWeather );
    SW_CONDITION( phase, ScheduleDayPhaseArgs, &ScheduleConditionInternal::readPhase );
    SW_CONDITION( dayOfSeason, ScheduleNumberListArgs, &ScheduleConditionInternal::readDayOfSeason );
    SW_CONDITION( week, ScheduleNumberListArgs, &ScheduleConditionInternal::readWeek );
    SW_CONDITION( date, ScheduleDateArgs, &ScheduleConditionInternal::readDate );
    SW_CONDITION( affinity, VocabularyNoParams, &ScheduleConditionInternal::readAffinity );
} // namespace sw
