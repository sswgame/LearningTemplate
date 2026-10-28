#include "pch.h"

#include "GameFramework/Base/Character/AI/Director/AIDirectorProfile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/String/EnumNameTable.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "AIDirectorProfile" );

    namespace
    {
        struct AIDirectorProfileInternal
        {
            static constexpr const utf8* kArrRootAttribute[]     = { "startPhase" };
            static constexpr const utf8* kArrCalendarAttribute[] = { "days", "seasons", "weathers" };

            /** @brief 표에 없는 속성마다 오류를 남깁니다. @p bCondition 이면 일정 조건 속성도 받습니다. 모르는 것이 있으면 false 입니다. */
            template <size_t Count>
            static bool validateAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], bool bCondition, string_view sourceName )
            {
                return XMLNameCheck::reportUnknownAttributes( node, arrKnown, sourceName, LogLevel::Error, bCondition ? &ScheduleCondition::isConditionAttribute : nullptr );
            }

            /** @brief @p node 의 자식 중 @p arrKnown 에 없는 원소마다 오류를 남깁니다. 모르는 것이 있으면 false 입니다. */
            template <size_t Count>
            static bool validateChildren( const XMLNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName )
            {
                return XMLNameCheck::reportUnknownChildren( node, arrKnown, sourceName, LogLevel::Error );
            }

            static constexpr const utf8*                          kArrSignalKindName[]  = { "impulse", "rate", "level" };
            static constexpr const utf8*                          kArrPoolKindName[]    = { "encounter", "reward" };
            static constexpr const utf8*                          kArrPoolTriggerName[] = { "phaseEnter", "interval", "budget" };
            static constexpr EnumNameTable<AIDirectorSignalKind>  kSignalKindTable{ kArrSignalKindName };
            static constexpr EnumNameTable<AIDirectorPoolKind>    kPoolKindTable{ kArrPoolKindName };
            static constexpr EnumNameTable<AIDirectorPoolTrigger> kPoolTriggerTable{ kArrPoolTriggerName };
            static_assert( kSignalKindTable.getCount() == static_cast<size_t>( AIDirectorSignalKind::Level ) + 1, "signal kind names must cover every AIDirectorSignalKind" );
            static_assert( kPoolKindTable.getCount() == static_cast<size_t>( AIDirectorPoolKind::Reward ) + 1, "pool kind names must cover every AIDirectorPoolKind" );
            static_assert( kPoolTriggerTable.getCount() == static_cast<size_t>( AIDirectorPoolTrigger::Budget ) + 1, "pool trigger names must cover every AIDirectorPoolTrigger" );
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AIDirectorSignalKind kind )
    {
        return AIDirectorProfileInternal::kSignalKindTable.toString( kind );
    }

    const utf8* toString( AIDirectorPoolKind kind )
    {
        return AIDirectorProfileInternal::kPoolKindTable.toString( kind );
    }

    const utf8* toString( AIDirectorPoolTrigger trigger )
    {
        return AIDirectorProfileInternal::kPoolTriggerTable.toString( trigger );
    }

    AIDirectorProfile::AIDirectorProfile()
        : _intensity{}
        , _listPhase{}
        , _listPool{}
        , _startPhase{}
        , _startPhaseIndex{ -1 }
    {
    }

    bool AIDirectorProfile::loadFromAsset( const AssetID& assetID )
    {
        XMLDocument doc;
        XMLNode     root;
        string      sourceName;
        return XMLReadUtil::loadRootOrWarn( doc, assetID, "AIDirector", root, sourceName ) && loadRoot( root, sourceName );
    }

    bool AIDirectorProfile::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        XMLNode     root;
        return XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, "AIDirector", root ) && loadRoot( root, sourceName );
    }

    void AIDirectorProfile::clear()
    {
        _intensity = AIDirectorIntensityDef{};
        _listPhase.clear();
        _listPool.clear();
        _startPhase      = hashed_string{};
        _startPhaseIndex = -1;
    }

    int32 AIDirectorProfile::findPhaseIndex( const hashed_string& phaseID ) const
    {
        for ( size_t index = 0; index < _listPhase.size(); ++index )
        {
            if ( _listPhase[index]._id == phaseID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 AIDirectorProfile::findSignalIndex( const hashed_string& signalID ) const
    {
        for ( size_t index = 0; index < _intensity._listSignal.size(); ++index )
        {
            if ( _intensity._listSignal[index]._id == signalID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void AIDirectorProfile::collectEncounterIDs( vector<hashed_string>& outListID ) const
    {
        for ( const AIDirectorPoolDef& pool : _listPool )
        {
            for ( const AIDirectorEncounterDef& encounter : pool._listEncounter )
            {
                if ( VectorUtil::contains( outListID, encounter._id ) == false )
                    outListID.push_back( encounter._id );
            }
        }
    }

    bool AIDirectorProfile::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Internal = AIDirectorProfileInternal;
        clear();
        static constexpr const utf8* kArrRootChild[] = { "Calendar", "Intensity", "Phase", "Pool" };
        bool                         bValid          = Internal::validateAttributes( root, Internal::kArrRootAttribute, false, sourceName );
        bValid                                       = Internal::validateChildren( root, kArrRootChild, sourceName ) && bValid;
        _startPhase                                  = hashed_string( root.getAttributeText( "startPhase" ) );

        // 조건 이름의 어휘는 풀보다 먼저 읽는다(파일 안 순서와 상관없이).
        ScheduleConditionVocabulary vocabulary;
        for ( XMLNode node = root.findChild( "Calendar" ); node; node = node.findNextSibling( "Calendar" ) )
        {
            bValid = Internal::validateAttributes( node, Internal::kArrCalendarAttribute, false, sourceName ) && bValid;
            ScheduleCondition::parseNameList( node.getAttributeText( "days" ), vocabulary._listWeekday );
            ScheduleCondition::parseNameList( node.getAttributeText( "seasons" ), vocabulary._listSeason );
            ScheduleCondition::parseNameList( node.getAttributeText( "weathers" ), vocabulary._listWeather );
        }
        for ( XMLNode node = root.findChild( "Intensity" ); node; node = node.findNextSibling( "Intensity" ) )
        {
            bValid = readIntensity( node, sourceName ) && bValid;
        }
        for ( XMLNode node = root.findChild( "Phase" ); node; node = node.findNextSibling( "Phase" ) )
        {
            bValid = readPhase( node, sourceName ) && bValid;
        }
        for ( XMLNode node = root.findChild( "Pool" ); node; node = node.findNextSibling( "Pool" ) )
        {
            bValid = readPool( node, vocabulary, sourceName ) && bValid;
        }
        bValid = resolveReferences( sourceName ) && bValid;
        if ( bValid == false )
            clear();
        return bValid;
    }

    bool AIDirectorProfile::readIntensity( const XMLNode& node, string_view sourceName )
    {
        using Internal                                    = AIDirectorProfileInternal;
        static constexpr const utf8* kArrIntensityChild[] = { "Signal" };
        bool                         bValid               = Internal::validateChildren( node, kArrIntensityChild, sourceName );
        bValid                                            = GameDataXML::readFields( node, _intensity, sourceName ) && bValid;
        for ( XMLNode child = node.findChild( "Signal" ); child; child = child.findNextSibling( "Signal" ) )
        {
            AIDirectorSignalDef signal;
            if ( GameDataXML::readFields( child, signal, sourceName, { "kind" } ) == false )
            {
                bValid = false;
                continue;
            }
            if ( Internal::kSignalKindTable.parse( child.getAttributeText( "kind" ), signal._kind ) == false )
            {
                SW_LOG_ERROR_ID( "AI.DirectorSignalKindUnknown", "%#: signal '%#' has an unknown kind '%#' (%#)", sourceName, signal._id.c_str(), child.getAttributeText( "kind" ),
                                 Internal::kSignalKindTable.joinNames().c_str() );
                bValid = false;
                continue;
            }
            if ( findSignalIndex( signal._id ) >= 0 )
            {
                SW_LOG_ERROR( "%#: signal '%#' is declared twice", sourceName, signal._id.c_str() );
                bValid = false;
                continue;
            }
            _intensity._listSignal.push_back( signal );
        }
        return bValid;
    }

    bool AIDirectorProfile::readPhase( const XMLNode& node, string_view sourceName )
    {
        using Internal                                = AIDirectorProfileInternal;
        static constexpr const utf8* kArrPhaseChild[] = { "Curve", "Exit" };
        bool                         bValid           = Internal::validateChildren( node, kArrPhaseChild, sourceName );
        AIDirectorPhaseDef           phase;
        if ( GameDataXML::readFields( node, phase, sourceName, { "spawnTags" } ) == false )
            return false;
        ScheduleCondition::parseNameList( node.getAttributeText( "spawnTags" ), phase._listSpawnTag );
        bValid = GameDataXML::readFloatCurve( node, "Curve", 0.0f, phase._spawnCurve, sourceName ) && bValid; // 키가 없으면 곡선이 대체값을 쓴다
        for ( XMLNode child = node.findChild( "Exit" ); child; child = child.findNextSibling( "Exit" ) )
        {
            AIDirectorExitDef exit;
            if ( GameDataXML::readFields( child, exit, sourceName ) )
                phase._listExit.push_back( exit );
            else
                bValid = false;
        }
        if ( findPhaseIndex( phase._id ) >= 0 )
        {
            SW_LOG_ERROR( "%#: phase '%#' is declared twice", sourceName, phase._id.c_str() );
            return false;
        }
        _listPhase.push_back( phase );
        return bValid;
    }

    bool AIDirectorProfile::readPool( const XMLNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName )
    {
        using Internal                               = AIDirectorProfileInternal;
        static constexpr const utf8* kArrPoolChild[] = { "Encounter" };
        bool                         bValid          = Internal::validateChildren( node, kArrPoolChild, sourceName );
        AIDirectorPoolDef            pool;
        if ( GameDataXML::readFields( node, pool, sourceName, { "kind", "trigger", "pacing" } ) == false )
            return false;
        const string_view kindText = node.getAttributeText( "kind" );
        // 종류는 적지 않으면 조우 풀이다(Def 초기값). 계기는 반드시 적는다.
        if ( kindText.empty() == false && Internal::kPoolKindTable.parse( kindText, pool._kind ) == false )
        {
            SW_LOG_ERROR( "%#: pool '%#' has an unknown kind '%#' (%#)", sourceName, pool._id.c_str(), kindText, Internal::kPoolKindTable.joinNames().c_str() );
            bValid = false;
        }
        const string_view triggerText = node.getAttributeText( "trigger" );
        if ( Internal::kPoolTriggerTable.parse( triggerText, pool._trigger ) == false )
        {
            SW_LOG_ERROR_ID( "AI.DirectorPoolTriggerUnknown", "%#: pool '%#' has an unknown trigger '%#' (%#)", sourceName, pool._id.c_str(), triggerText, Internal::kPoolTriggerTable.joinNames().c_str() );
            bValid = false;
        }
        ScheduleCondition::parseNameList( node.getAttributeText( "pacing" ), pool._listPacing );
        if ( pool._trigger == AIDirectorPoolTrigger::PhaseEnter && pool._phase.empty() )
        {
            SW_LOG_ERROR( "%#: pool '%#' is triggered on phase enter but names no phase", sourceName, pool._id.c_str() );
            bValid = false;
        }

        for ( XMLNode child = node.findChild( "Encounter" ); child; child = child.findNextSibling( "Encounter" ) )
        {
            AIDirectorEncounterDef encounter;
            if ( GameDataXML::readFields( child, encounter, sourceName, { "areas", "pacing" }, &ScheduleCondition::isConditionAttribute ) == false )
            {
                bValid = false;
                continue;
            }
            ScheduleCondition::parseNameList( child.getAttributeText( "areas" ), encounter._listArea );
            ScheduleCondition::parseNameList( child.getAttributeText( "pacing" ), encounter._listPacing );
            bValid = encounter._condition.readFromNode( child, vocabulary, sourceName, encounter._id.c_str() ) && bValid;
            for ( const AIDirectorEncounterDef& other : pool._listEncounter )
            {
                if ( other._id == encounter._id )
                {
                    SW_LOG_ERROR( "%#: pool '%#' lists '%#' twice", sourceName, pool._id.c_str(), encounter._id.c_str() );
                    bValid = false;
                }
            }
            pool._listEncounter.push_back( encounter );
        }
        if ( pool._listEncounter.empty() )
        {
            SW_LOG_ERROR( "%#: pool '%#' has no <Encounter>", sourceName, pool._id.c_str() );
            bValid = false;
        }
        for ( const AIDirectorPoolDef& other : _listPool )
        {
            if ( other._id == pool._id )
            {
                SW_LOG_ERROR( "%#: pool '%#' is declared twice", sourceName, pool._id.c_str() );
                bValid = false;
            }
        }
        _listPool.push_back( pool );
        return bValid;
    }

    bool AIDirectorProfile::validatePacingNames( const vector<hashed_string>& listPacing, const hashed_string& ownerID, string_view sourceName ) const
    {
        bool bValid = true;
        for ( const hashed_string& phaseID : listPacing )
        {
            if ( findPhaseIndex( phaseID ) >= 0 )
                continue;
            SW_LOG_ERROR_ID( "AI.DirectorPacingPhaseUnknown", "%#: '%#' names an unknown pacing phase '%#'", sourceName, ownerID.c_str(), phaseID.c_str() );
            bValid = false;
        }
        return bValid;
    }

    bool AIDirectorProfile::resolveReferences( string_view sourceName )
    {
        bool bValid = true;
        if ( _listPhase.empty() )
        {
            SW_LOG_ERROR_ID( "AI.DirectorNoPhase", "%#: no <Phase>", sourceName );
            return false;
        }
        _startPhaseIndex = _startPhase.empty() ? 0 : findPhaseIndex( _startPhase );
        if ( _startPhaseIndex < 0 )
        {
            SW_LOG_ERROR_ID( "AI.DirectorStartPhaseUnknown", "%#: start phase '%#' is not a <Phase>", sourceName, _startPhase.c_str() );
            bValid = false;
        }
        // 단계 이름은 나가는 길 · 풀 · 항목이 가리킨다 — 모르는 이름은 쓰는 쪽에서 조용히 "늘 거짓" 이 되므로 여기서 막는다.
        for ( AIDirectorPhaseDef& phase : _listPhase )
        {
            for ( AIDirectorExitDef& exit : phase._listExit )
            {
                exit._toIndex = findPhaseIndex( exit._to );
                if ( exit._toIndex >= 0 )
                    continue;
                SW_LOG_ERROR_ID( "AI.DirectorExitPhaseUnknown", "%#: phase '%#' exits to an unknown phase '%#'", sourceName, phase._id.c_str(), exit._to.c_str() );
                bValid = false;
            }
        }
        for ( AIDirectorPoolDef& pool : _listPool )
        {
            bValid = validatePacingNames( pool._listPacing, pool._id, sourceName ) && bValid;
            for ( const AIDirectorEncounterDef& encounter : pool._listEncounter )
            {
                bValid = validatePacingNames( encounter._listPacing, encounter._id, sourceName ) && bValid;
            }
            if ( pool._trigger == AIDirectorPoolTrigger::PhaseEnter )
            {
                pool._phaseIndex = findPhaseIndex( pool._phase );
                if ( pool._phaseIndex < 0 )
                {
                    SW_LOG_ERROR( "%#: pool '%#' waits for an unknown phase '%#'", sourceName, pool._id.c_str(), pool._phase.c_str() );
                    bValid = false;
                }
            }
            if ( pool._needSignal.empty() == false && findSignalIndex( pool._needSignal ) < 0 )
            {
                SW_LOG_ERROR_ID( "AI.DirectorPoolSignalUnknown", "%#: pool '%#' reads an unknown signal '%#'", sourceName, pool._id.c_str(), pool._needSignal.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }
} // namespace sw
