#include "pch.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    SW_LOG_CALLER( "ScheduleCondition" );

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

            /** @brief 계절 순서에서 @p season 의 번호입니다. 모르면 −1 입니다. */
            static int32 findSeasonIndex( const vector<hashed_string>* pListSeason, const hashed_string& season )
            {
                if ( pListSeason == nullptr )
                    return -1;
                for ( size_t index = 0; index < pListSeason->size(); ++index )
                {
                    if ( ( *pListSeason )[index] == season )
                        return static_cast<int32>( index );
                }
                return -1;
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

            static void parseTagList( string_view text, vector<TagID>& outListTag )
            {
                outListTag.clear();
                StringUtil::forEachToken( text, kNameSeparators, [&]( string_view token )
                { outListTag.push_back( TagID::request( token ) ); } );
            }

            static bool hasTag( const ScheduleConditionContext& context, const TagID& tag )
            {
                const bool bNpc   = context._pNpcTags != nullptr && context._pNpcTags->hasTag( tag );
                const bool bWorld = context._pWorldTags != nullptr && context._pWorldTags->hasTag( tag );
                return bNpc || bWorld;
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
            if ( StringUtil::equals( pName, pKnown, true ) )
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
                    mask   = static_cast<uint8>( mask | makeDayPhaseBit( phase ) );
                    bFound = true;
                }
            }
            if ( bFound == false )
                SW_LOG_WARNING( "%#: '%#' has an unknown phase '%#'", sourceName, ownerName, token );
        } );
        return mask;
    }

    bool ScheduleCondition::readFromNode( const XMLNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName, string_view ownerName )
    {
        using Internal = ScheduleConditionInternal;
        parseNameList( node.getAttributeText( "days" ), _listWeekday );
        parseNameList( node.getAttributeText( "seasons" ), _listSeason );
        parseNameList( node.getAttributeText( "weathers" ), _listWeather );
        Internal::warnUnknownNames( _listWeekday, vocabulary._listWeekday, "day", "Schedule.DayUnknown", sourceName, ownerName );
        Internal::warnUnknownNames( _listSeason, vocabulary._listSeason, "season", "Schedule.SeasonUnknown", sourceName, ownerName );
        Internal::warnUnknownNames( _listWeather, vocabulary._listWeather, "weather", "Schedule.WeatherUnknown", sourceName, ownerName );

        // 계절의 날은 1 부터 366 까지(`a-b` 범위 가능). 주는 1 부터 53 까지.
        Internal::parseDayList( node.getAttributeText( "daysOfSeason" ), _listDayOfSeason, 366, sourceName, ownerName, "day of season" );
        Internal::parseDayList( node.getAttributeText( "weeks" ), _listWeek, 53, sourceName, ownerName, "week" );

        // 계절을 넘나드는 날짜 범위. 둘 다 적으면 `from` 이 `to` 보다 늦을 때 해를 넘는 범위다(Winter 20 ~ Spring 5).
        if ( Internal::parseDate( node.getAttributeText( "from" ), vocabulary._listSeason, _fromSeason, _fromDay, sourceName, ownerName, "from" ) == false )
            _fromDay = 0;
        if ( Internal::parseDate( node.getAttributeText( "to" ), vocabulary._listSeason, _toSeason, _toDay, sourceName, ownerName, "to" ) == false )
            _toDay = 0;

        // 호감도(관계 단계) 범위 — 한쪽만 적어도 된다. 적지 않은 쪽은 형식의 끝(초기값 kMinInt32 · kMaxInt32)이라 "제한 없음" 이다.
        bool                   bValid = true;
        std::optional<int32>   minAffinity;
        std::optional<int32>   maxAffinity;
        std::optional<float32> chance;
        bValid = node.readOptionalAttribute( "minAffinity", minAffinity ) && bValid;
        bValid = node.readOptionalAttribute( "maxAffinity", maxAffinity ) && bValid;
        bValid = node.readOptionalAttribute( "chance", chance ) && bValid;
        if ( minAffinity.has_value() )
            _minAffinity = *minAffinity;
        if ( maxAffinity.has_value() )
            _maxAffinity = *maxAffinity;
        if ( _minAffinity > _maxAffinity )
        {
            SW_LOG_ERROR_ID( "Schedule.AffinityRangeEmpty", "%#: '%#' has minAffinity %# above maxAffinity %# - it can never match", sourceName, ownerName, _minAffinity, _maxAffinity );
            bValid = false;
        }

        _phaseMask = parsePhaseMask( node.getAttributeText( "phases" ), sourceName, ownerName );
        Internal::parseTagList( node.getAttributeText( "tags" ), _listRequiredTag );
        Internal::parseTagList( node.getAttributeText( "notTags" ), _listForbiddenTag );
        if ( chance.has_value() )
        {
            const bool bChanceInRange = 0.0f <= *chance && *chance <= 1.0f;
            if ( bChanceInRange )
                _chance = *chance;
            else
                SW_LOG_ERROR( "%#: '%#' has chance %# outside [0, 1]", sourceName, ownerName, *chance );
            bValid = bChanceInRange && bValid;
        }

        const string_view flags = node.getAttributeText( "flags" );
        _flags.assign( flags.data(), flags.size() );
        if ( _flags.empty() == false )
        {
            const GameFlags noFlags;
            bool            bIgnored = false;
            if ( GameFlags::parseCondition( _flags, noFlags, bIgnored ) == false )
            {
                SW_LOG_ERROR_ID( "Schedule.FlagsInvalid", "%#: '%#' has an invalid flags expression '%#'", sourceName, ownerName, _flags );
                bValid = false;
            }
        }
        return bValid;
    }

    bool ScheduleCondition::isEmpty() const
    {
        const bool bNoNames    = _listWeekday.empty() && _listSeason.empty() && _listWeather.empty() && _listDayOfSeason.empty() && _listWeek.empty();
        const bool bNoTags     = _listRequiredTag.empty() && _listForbiddenTag.empty();
        const bool bNoDates    = _fromDay <= 0 && _toDay <= 0;
        const bool bNoAffinity = _minAffinity == MathUtil::kMinInt32 && _maxAffinity == MathUtil::kMaxInt32;
        return bNoNames && bNoTags && bNoDates && bNoAffinity && _flags.empty() && _chance >= 1.0f && _phaseMask == 0;
    }

    ScheduleConditionResult ScheduleCondition::evaluate( const ScheduleConditionContext& context ) const
    {
        using Internal = ScheduleConditionInternal;
        ScheduleConditionResult result;
        if ( _listWeekday.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Weekday, VectorUtil::contains( _listWeekday, context._weekday ) );
        if ( _listDayOfSeason.empty() == false )
        {
            bool bFound = false;
            for ( const int32 dayOfSeason : _listDayOfSeason )
            {
                bFound = bFound || dayOfSeason == context._dayOfSeason;
            }
            Internal::mark( result, ScheduleConditionClause::DayOfSeason, bFound );
        }
        if ( _listWeek.empty() == false )
        {
            const int32 week = ( context._dayOfSeason - 1 ) / Internal::kDaysPerWeek + 1;
            Internal::mark( result, ScheduleConditionClause::Week, VectorUtil::contains( _listWeek, week ) );
        }
        if ( _fromDay > 0 || _toDay > 0 )
        {
            // 날짜를 (계절 번호, 날) 열쇠로 바꿔 비교한다. 계절 순서를 모르면(context 에 없으면) 거짓이다.
            const int32 currentSeason = Internal::findSeasonIndex( context._pSeasonOrder, context._season );
            const int32 fromSeason    = _fromDay > 0 ? Internal::findSeasonIndex( context._pSeasonOrder, _fromSeason ) : 0;
            const int32 toSeason      = _toDay > 0 ? Internal::findSeasonIndex( context._pSeasonOrder, _toSeason ) : 0;
            bool        bInRange      = false;
            const bool  bKnownSeasons = currentSeason >= 0 && fromSeason >= 0 && toSeason >= 0;
            if ( bKnownSeasons )
            {
                const int32 current = currentSeason * Internal::kSeasonKeyStride + context._dayOfSeason;
                const int32 from    = fromSeason * Internal::kSeasonKeyStride + _fromDay;
                const int32 to      = toSeason * Internal::kSeasonKeyStride + _toDay;
                if ( _fromDay > 0 && _toDay > 0 )
                    bInRange = from <= to ? ( from <= current && current <= to ) : ( from <= current || current <= to );
                else if ( _fromDay > 0 )
                    bInRange = from <= current;
                else
                    bInRange = current <= to;
            }
            Internal::mark( result, ScheduleConditionClause::DateRange, bInRange );
        }
        if ( _minAffinity != MathUtil::kMinInt32 || _maxAffinity != MathUtil::kMaxInt32 )
            Internal::mark( result, ScheduleConditionClause::Affinity, _minAffinity <= context._affinity && context._affinity <= _maxAffinity );
        if ( _listSeason.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Season, VectorUtil::contains( _listSeason, context._season ) );
        if ( _listWeather.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Weather, VectorUtil::contains( _listWeather, context._weather ) );
        if ( _phaseMask != 0 )
            Internal::mark( result, ScheduleConditionClause::Phase, ( _phaseMask & makeDayPhaseBit( context._phase ) ) != 0 );
        if ( _flags.empty() == false )
            Internal::mark( result, ScheduleConditionClause::Flags, context._pFlags != nullptr && context._pFlags->evaluate( _flags ) );
        if ( _listRequiredTag.empty() == false || _listForbiddenTag.empty() == false )
        {
            bool bPassed = true;
            for ( const TagID& tag : _listRequiredTag )
            {
                bPassed = bPassed && Internal::hasTag( context, tag );
            }
            for ( const TagID& tag : _listForbiddenTag )
            {
                bPassed = bPassed && Internal::hasTag( context, tag ) == false;
            }
            Internal::mark( result, ScheduleConditionClause::Tags, bPassed );
        }
        if ( _chance < 1.0f )
        {
            const float32 roll = GameHash::toUnitFloat( GameHash::hashCoord( context._day, static_cast<int32>( context._chanceKey ), 0x5c4ed01eu ) );
            Internal::mark( result, ScheduleConditionClause::Chance, roll < _chance );
        }
        return result;
    }
} // namespace sw
