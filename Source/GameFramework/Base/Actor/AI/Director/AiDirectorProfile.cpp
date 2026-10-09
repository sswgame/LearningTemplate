#include "pch.h"

#include "GameFramework/Base/Actor/AI/Director/AiDirectorProfile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"
#include "Engine/Utility/Xml/XmlNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "AiDirectorProfile" );

    namespace
    {
        struct AiDirectorProfileInternal
        {
            static constexpr const utf8* kArrRootAttribute[]      = { "startPhase" };
            static constexpr const utf8* kArrCalendarAttribute[]  = { "days", "seasons", "weathers" };
            static constexpr const utf8* kArrIntensityAttribute[] = { "max", "decayPerSecond", "decayDelay" };
            static constexpr const utf8* kArrSignalAttribute[]    = { "id", "kind", "scale", "max", "combat" };
            static constexpr const utf8* kArrPhaseAttribute[]     = { "id", "spawnScale", "rewardScale", "spawnTags" };
            static constexpr const utf8* kArrCurveAttribute[]     = { "time", "scale" };
            static constexpr const utf8* kArrExitAttribute[]      = { "to", "minTime", "intensityAbove", "intensityBelow", "calmFor" };
            static constexpr const utf8* kArrPoolAttribute[]      = { "id", "kind", "trigger", "phase", "interval", "chance", "cooldown", "perMinute",
                                                                      "maxBudget", "need", "needScale", "picks", "pacing" };
            static constexpr const utf8* kArrEncounterAttribute[] = { "id", "weight", "cost", "cooldown", "minTime", "minIntensity", "maxIntensity",
                                                                      "scale", "count", "maxCount", "minCycle", "areas", "pacing" };

            /** @brief 표에 없는 속성마다 경고합니다. @p bCondition 이면 일정 조건 속성도 받습니다. 모르는 것이 있으면 false 입니다. */
            template <size_t Count>
            static bool validateAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], bool bCondition, string_view sourceName )
            {
                return XmlNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Warning, bCondition ? &ScheduleCondition::isConditionAttribute : nullptr );
            }

            /** @brief @p node 의 자식 중 @p arrKnown 에 없는 원소마다 경고합니다. 모르는 것이 있으면 false 입니다. */
            template <size_t Count>
            static bool validateChildren( const XmlNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                return XmlNameCheck::reportUnknownChildren( node, arrKnown, sourceName, LogLevel::Warning );
            }

            template <size_t Count>
            [[nodiscard]] static bool parseEnumName( string_view text, const utf8* const ( &arrName )[Count], uint32& outIndex )
            {
                for ( uint32 index = 0; index < Count; ++index )
                {
                    if ( StringUtil::equals( text, arrName[index], true ) )
                    {
                        outIndex = index;
                        return true;
                    }
                }
                return false;
            }

            static bool contains( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( const hashed_string& entry : listName )
                {
                    if ( entry == name )
                        return true;
                }
                return false;
            }

            static constexpr const utf8* kArrSignalKindName[]  = { "impulse", "rate", "level" };
            static constexpr const utf8* kArrPoolKindName[]    = { "encounter", "reward" };
            static constexpr const utf8* kArrPoolTriggerName[] = { "phaseEnter", "interval", "budget" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AiDirectorSignalKind kind )
    {
        return AiDirectorProfileInternal::kArrSignalKindName[static_cast<uint32>( kind )];
    }

    const utf8* toString( AiDirectorPoolKind kind )
    {
        return AiDirectorProfileInternal::kArrPoolKindName[static_cast<uint32>( kind )];
    }

    const utf8* toString( AiDirectorPoolTrigger trigger )
    {
        return AiDirectorProfileInternal::kArrPoolTriggerName[static_cast<uint32>( trigger )];
    }

    AiDirectorProfile::AiDirectorProfile()
        : _intensity{}
        , _listPhase{}
        , _listPool{}
        , _startPhase{}
        , _startPhaseIndex{ -1 }
    {
    }

    bool AiDirectorProfile::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "AiDirector", root, sourceName ) && loadRoot( root, sourceName );
    }

    bool AiDirectorProfile::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "AiDirector", root ) && loadRoot( root, sourceName );
    }

    void AiDirectorProfile::clear()
    {
        _intensity = AiDirectorIntensityDef{};
        _listPhase.clear();
        _listPool.clear();
        _startPhase      = hashed_string{};
        _startPhaseIndex = -1;
    }

    int32 AiDirectorProfile::findPhaseIndex( const hashed_string& phaseId ) const
    {
        for ( size_t index = 0; index < _listPhase.size(); ++index )
        {
            if ( _listPhase[index]._id == phaseId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 AiDirectorProfile::findSignalIndex( const hashed_string& signalId ) const
    {
        for ( size_t index = 0; index < _intensity._listSignal.size(); ++index )
        {
            if ( _intensity._listSignal[index]._id == signalId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void AiDirectorProfile::collectEncounterIds( vector<hashed_string>& outListId ) const
    {
        for ( const AiDirectorPoolDef& pool : _listPool )
        {
            for ( const AiDirectorEncounterDef& encounter : pool._listEncounter )
            {
                if ( AiDirectorProfileInternal::contains( outListId, encounter._id ) == false )
                    outListId.push_back( encounter._id );
            }
        }
    }

    bool AiDirectorProfile::loadRoot( const XmlNode& root, string_view sourceName )
    {
        using Internal = AiDirectorProfileInternal;
        clear();
        static constexpr const utf8* kArrRootChild[] = { "Calendar", "Intensity", "Phase", "Pool" };
        bool                         bValid          = Internal::validateAttributes( root, Internal::kArrRootAttribute, false, sourceName );
        bValid                                       = Internal::validateChildren( root, kArrRootChild, sourceName ) && bValid;
        _startPhase                                  = hashed_string( root.getAttributeText( "startPhase" ) );

        // 조건 이름의 어휘는 풀보다 먼저 읽는다(파일 안 순서와 상관없이).
        ScheduleConditionVocabulary vocabulary;
        for ( XmlNode node = root.findChild( "Calendar" ); node; node = node.findNextSibling( "Calendar" ) )
        {
            bValid = Internal::validateAttributes( node, Internal::kArrCalendarAttribute, false, sourceName ) && bValid;
            ScheduleCondition::parseNameList( node.getAttributeText( "days" ), vocabulary._listWeekday );
            ScheduleCondition::parseNameList( node.getAttributeText( "seasons" ), vocabulary._listSeason );
            ScheduleCondition::parseNameList( node.getAttributeText( "weathers" ), vocabulary._listWeather );
        }
        for ( XmlNode node = root.findChild( "Intensity" ); node; node = node.findNextSibling( "Intensity" ) )
        {
            bValid = readIntensity( node, sourceName ) && bValid;
        }
        for ( XmlNode node = root.findChild( "Phase" ); node; node = node.findNextSibling( "Phase" ) )
        {
            bValid = readPhase( node, sourceName ) && bValid;
        }
        for ( XmlNode node = root.findChild( "Pool" ); node; node = node.findNextSibling( "Pool" ) )
        {
            bValid = readPool( node, vocabulary, sourceName ) && bValid;
        }
        bValid = resolveReferences( sourceName ) && bValid;
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool AiDirectorProfile::readIntensity( const XmlNode& node, string_view sourceName )
    {
        using Internal                                    = AiDirectorProfileInternal;
        static constexpr const utf8* kArrIntensityChild[] = { "Signal" };
        bool                         bValid               = Internal::validateAttributes( node, Internal::kArrIntensityAttribute, false, sourceName );
        bValid                                            = Internal::validateChildren( node, kArrIntensityChild, sourceName ) && bValid;
        _intensity._max                                   = MathUtil::max( 0.0f, node.getAttributeFloat( "max", _intensity._max ) );
        _intensity._decayPerSecond                        = MathUtil::max( 0.0f, node.getAttributeFloat( "decayPerSecond", _intensity._decayPerSecond ) );
        _intensity._decayDelay                            = MathUtil::max( 0.0f, node.getAttributeFloat( "decayDelay", _intensity._decayDelay ) );
        for ( XmlNode child = node.findChild( "Signal" ); child; child = child.findNextSibling( "Signal" ) )
        {
            bValid          = Internal::validateAttributes( child, Internal::kArrSignalAttribute, false, sourceName ) && bValid;
            const utf8* pId = GameDataXml::findRequiredId( child, sourceName );
            if ( pId == nullptr )
            {
                bValid = false;
                continue;
            }
            AiDirectorSignalDef signal;
            signal._id       = hashed_string( pId );
            signal._scale    = child.getAttributeFloat( "scale", signal._scale );
            signal._max      = child.getAttributeFloat( "max", signal._max );
            signal._bCombat  = child.getAttributeBool( "combat", false ) ? SW_TRUE : SW_FALSE;
            uint32 kindIndex = 0;
            if ( Internal::parseEnumName( child.getAttributeText( "kind" ), Internal::kArrSignalKindName, kindIndex ) == false )
            {
                SW_LOG_WARNING( "%#: signal '%#' has an unknown kind '%#' (impulse, rate, level)", sourceName, pId, child.getAttributeText( "kind" ) );
                bValid = false;
                continue;
            }
            signal._kind = static_cast<AiDirectorSignalKind>( kindIndex );
            if ( findSignalIndex( signal._id ) >= 0 )
            {
                SW_LOG_WARNING( "%#: signal '%#' is declared twice", sourceName, pId );
                bValid = false;
                continue;
            }
            _intensity._listSignal.push_back( signal );
        }
        return bValid;
    }

    bool AiDirectorProfile::readPhase( const XmlNode& node, string_view sourceName )
    {
        using Internal                                = AiDirectorProfileInternal;
        static constexpr const utf8* kArrPhaseChild[] = { "Curve", "Exit" };
        bool                         bValid           = Internal::validateAttributes( node, Internal::kArrPhaseAttribute, false, sourceName );
        bValid                                        = Internal::validateChildren( node, kArrPhaseChild, sourceName ) && bValid;
        const utf8* pId                               = GameDataXml::findRequiredId( node, sourceName );
        if ( pId == nullptr )
            return false;
        AiDirectorPhaseDef phase;
        phase._id          = hashed_string( pId );
        phase._spawnScale  = MathUtil::max( 0.0f, node.getAttributeFloat( "spawnScale", phase._spawnScale ) );
        phase._rewardScale = MathUtil::max( 0.0f, node.getAttributeFloat( "rewardScale", phase._rewardScale ) );
        ScheduleCondition::parseNameList( node.getAttributeText( "spawnTags" ), phase._listSpawnTag );
        for ( XmlNode child = node.findChild( "Curve" ); child; child = child.findNextSibling( "Curve" ) )
        {
            bValid = Internal::validateAttributes( child, Internal::kArrCurveAttribute, false, sourceName ) && bValid;
        }
        (void)phase._spawnCurve.readPoints( node, "Curve", "scale", 0.0f ); // 점 수만 돌려준다 — 점이 없으면 곡선이 대체값을 쓴다
        for ( XmlNode child = node.findChild( "Exit" ); child; child = child.findNextSibling( "Exit" ) )
        {
            bValid = Internal::validateAttributes( child, Internal::kArrExitAttribute, false, sourceName ) && bValid;
            AiDirectorExitDef exit;
            exit._to             = hashed_string( child.getAttributeText( "to" ) );
            exit._minTime        = MathUtil::max( 0.0f, child.getAttributeFloat( "minTime", exit._minTime ) );
            exit._intensityAbove = child.getAttributeFloat( "intensityAbove", exit._intensityAbove );
            exit._intensityBelow = child.getAttributeFloat( "intensityBelow", exit._intensityBelow );
            exit._calmFor        = MathUtil::max( 0.0f, child.getAttributeFloat( "calmFor", exit._calmFor ) );
            phase._listExit.push_back( exit );
        }
        if ( findPhaseIndex( phase._id ) >= 0 )
        {
            SW_LOG_WARNING( "%#: phase '%#' is declared twice", sourceName, pId );
            return false;
        }
        _listPhase.push_back( phase );
        return bValid;
    }

    bool AiDirectorProfile::readPool( const XmlNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName )
    {
        using Internal                               = AiDirectorProfileInternal;
        static constexpr const utf8* kArrPoolChild[] = { "Encounter" };
        bool                         bValid          = Internal::validateAttributes( node, Internal::kArrPoolAttribute, false, sourceName );
        bValid                                       = Internal::validateChildren( node, kArrPoolChild, sourceName ) && bValid;
        const utf8* pId                              = GameDataXml::findRequiredId( node, sourceName );
        if ( pId == nullptr )
            return false;
        AiDirectorPoolDef pool;
        pool._id                    = hashed_string( pId );
        uint32            kindIndex = 0;
        const string_view kindText  = node.getAttributeText( "kind" );
        if ( kindText.empty() == false && Internal::parseEnumName( kindText, Internal::kArrPoolKindName, kindIndex ) == false )
        {
            SW_LOG_WARNING( "%#: pool '%#' has an unknown kind '%#' (encounter, reward)", sourceName, pId, kindText );
            bValid = false;
        }
        pool._kind                     = static_cast<AiDirectorPoolKind>( kindIndex );
        uint32            triggerIndex = static_cast<uint32>( AiDirectorPoolTrigger::Interval );
        const string_view triggerText  = node.getAttributeText( "trigger" );
        if ( Internal::parseEnumName( triggerText, Internal::kArrPoolTriggerName, triggerIndex ) == false )
        {
            SW_LOG_WARNING( "%#: pool '%#' has an unknown trigger '%#' (phaseEnter, interval, budget)", sourceName, pId, triggerText );
            bValid = false;
        }
        pool._trigger    = static_cast<AiDirectorPoolTrigger>( triggerIndex );
        pool._phase      = hashed_string( node.getAttributeText( "phase" ) );
        pool._needSignal = hashed_string( node.getAttributeText( "need" ) );
        pool._interval   = MathUtil::max( 0.01f, node.getAttributeFloat( "interval", pool._interval ) );
        pool._chance     = MathUtil::clamp( node.getAttributeFloat( "chance", pool._chance ), 0.0f, 1.0f );
        pool._cooldown   = MathUtil::max( 0.0f, node.getAttributeFloat( "cooldown", pool._cooldown ) );
        pool._perMinute  = MathUtil::max( 0.0f, node.getAttributeFloat( "perMinute", pool._perMinute ) );
        pool._maxBudget  = MathUtil::max( 0.0f, node.getAttributeFloat( "maxBudget", pool._maxBudget ) );
        pool._needScale  = node.getAttributeFloat( "needScale", pool._needScale );
        pool._picks      = MathUtil::max( 1, node.getAttributeInt( "picks", pool._picks ) );
        ScheduleCondition::parseNameList( node.getAttributeText( "pacing" ), pool._listPacing );
        if ( pool._trigger == AiDirectorPoolTrigger::PhaseEnter && pool._phase.empty() )
        {
            SW_LOG_WARNING( "%#: pool '%#' is triggered on phase enter but names no phase", sourceName, pId );
            bValid = false;
        }

        for ( XmlNode child = node.findChild( "Encounter" ); child; child = child.findNextSibling( "Encounter" ) )
        {
            bValid                   = Internal::validateAttributes( child, Internal::kArrEncounterAttribute, true, sourceName ) && bValid;
            const utf8* pEncounterId = GameDataXml::findRequiredId( child, sourceName );
            if ( pEncounterId == nullptr )
            {
                bValid = false;
                continue;
            }
            AiDirectorEncounterDef encounter;
            encounter._id           = hashed_string( pEncounterId );
            encounter._weight       = MathUtil::max( 0.0f, child.getAttributeFloat( "weight", encounter._weight ) );
            encounter._cost         = MathUtil::max( 0.0f, child.getAttributeFloat( "cost", encounter._cost ) );
            encounter._cooldown     = MathUtil::max( 0.0f, child.getAttributeFloat( "cooldown", encounter._cooldown ) );
            encounter._minTime      = MathUtil::max( 0.0f, child.getAttributeFloat( "minTime", encounter._minTime ) );
            encounter._minIntensity = child.getAttributeFloat( "minIntensity", encounter._minIntensity );
            encounter._maxIntensity = child.getAttributeFloat( "maxIntensity", encounter._maxIntensity );
            encounter._scale        = child.getAttributeFloat( "scale", encounter._scale );
            encounter._count        = MathUtil::max( 0, child.getAttributeInt( "count", encounter._count ) );
            encounter._maxCount     = child.getAttributeInt( "maxCount", encounter._maxCount );
            encounter._minCycle     = MathUtil::max( 0, child.getAttributeInt( "minCycle", encounter._minCycle ) );
            ScheduleCondition::parseNameList( child.getAttributeText( "areas" ), encounter._listArea );
            ScheduleCondition::parseNameList( child.getAttributeText( "pacing" ), encounter._listPacing );
            encounter._condition.readFromNode( child, vocabulary, sourceName, pEncounterId );
            for ( const AiDirectorEncounterDef& other : pool._listEncounter )
            {
                if ( other._id == encounter._id )
                {
                    SW_LOG_WARNING( "%#: pool '%#' lists '%#' twice", sourceName, pId, pEncounterId );
                    bValid = false;
                }
            }
            pool._listEncounter.push_back( encounter );
        }
        if ( pool._listEncounter.empty() )
        {
            SW_LOG_WARNING( "%#: pool '%#' has no <Encounter>", sourceName, pId );
            bValid = false;
        }
        for ( const AiDirectorPoolDef& other : _listPool )
        {
            if ( other._id == pool._id )
            {
                SW_LOG_WARNING( "%#: pool '%#' is declared twice", sourceName, pId );
                bValid = false;
            }
        }
        _listPool.push_back( pool );
        return bValid;
    }

    bool AiDirectorProfile::validatePacingNames( const vector<hashed_string>& listPacing, const hashed_string& ownerId, string_view sourceName ) const
    {
        bool bValid = true;
        for ( const hashed_string& phaseId : listPacing )
        {
            if ( findPhaseIndex( phaseId ) >= 0 )
                continue;
            SW_LOG_WARNING( "%#: '%#' names an unknown pacing phase '%#'", sourceName, ownerId.c_str(), phaseId.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AiDirectorProfile::resolveReferences( string_view sourceName )
    {
        bool bValid = true;
        if ( _listPhase.empty() )
        {
            SW_LOG_WARNING( "%#: no <Phase>", sourceName );
            return false;
        }
        _startPhaseIndex = _startPhase.empty() ? 0 : findPhaseIndex( _startPhase );
        if ( _startPhaseIndex < 0 )
        {
            SW_LOG_WARNING( "%#: start phase '%#' is not a <Phase>", sourceName, _startPhase.c_str() );
            bValid = false;
        }
        // 단계 이름은 나가는 길 · 풀 · 항목이 가리킨다 — 모르는 이름은 쓰는 쪽에서 조용히 "늘 거짓" 이 되므로 여기서 막는다.
        for ( AiDirectorPhaseDef& phase : _listPhase )
        {
            for ( AiDirectorExitDef& exit : phase._listExit )
            {
                exit._toIndex = findPhaseIndex( exit._to );
                if ( exit._toIndex >= 0 )
                    continue;
                SW_LOG_WARNING( "%#: phase '%#' exits to an unknown phase '%#'", sourceName, phase._id.c_str(), exit._to.c_str() );
                bValid = false;
            }
        }
        for ( AiDirectorPoolDef& pool : _listPool )
        {
            bValid = validatePacingNames( pool._listPacing, pool._id, sourceName ) && bValid;
            for ( const AiDirectorEncounterDef& encounter : pool._listEncounter )
            {
                bValid = validatePacingNames( encounter._listPacing, encounter._id, sourceName ) && bValid;
            }
            if ( pool._trigger == AiDirectorPoolTrigger::PhaseEnter )
            {
                pool._phaseIndex = findPhaseIndex( pool._phase );
                if ( pool._phaseIndex < 0 )
                {
                    SW_LOG_WARNING( "%#: pool '%#' waits for an unknown phase '%#'", sourceName, pool._id.c_str(), pool._phase.c_str() );
                    bValid = false;
                }
            }
            if ( pool._needSignal.empty() == false && findSignalIndex( pool._needSignal ) < 0 )
            {
                SW_LOG_WARNING( "%#: pool '%#' reads an unknown signal '%#'", sourceName, pool._id.c_str(), pool._needSignal.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }
} // namespace sw
