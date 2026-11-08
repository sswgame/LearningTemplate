#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct AIDirectorProfileInternal
        {
            static constexpr const utf8* kArrRootAttribute[] = { "startPhase" };

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    AIDirectorProfile::AIDirectorProfile()
        : _intensity{}
        , _listPhase{}
        , _listPool{}
        , _startPhase{}
        , _startPhaseIndex{}
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
        _startPhaseIndex = nullopt;
    }

    optional<int32> AIDirectorProfile::findPhaseIndex( const hashed_string& phaseID ) const
    {
        for ( size_t index = 0; index < _listPhase.size(); ++index )
        {
            if ( _listPhase[index]._id == phaseID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    optional<int32> AIDirectorProfile::findSignalIndex( const hashed_string& signalID ) const
    {
        for ( size_t index = 0; index < _intensity._listSignal.size(); ++index )
        {
            if ( _intensity._listSignal[index]._id == signalID )
                return static_cast<int32>( index );
        }
        return nullopt;
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
        bool bValid = Internal::validateAttributes( root, Internal::kArrRootAttribute, false, sourceName );
        _startPhase = hashed_string( root.getAttributeText( "startPhase" ) );

        /** 풀은 달력 어휘를 받아야 해서 문맥을 든 읽는 쪽을 둔다. 함수 안의 형식이라 이 클래스의 비공개를 볼 수 있다. */
        struct DirectorElementReader
        {
            AIDirectorProfile&      _profile;
            ScheduleConditionSchema _vocabulary;

            bool loadCalendar( const XMLNode& node, string_view sourceName ) { return XMLFields::readFields( node, _vocabulary, sourceName ); }
            bool loadIntensity( const XMLNode& node, string_view sourceName ) { return _profile.readIntensity( node, sourceName ); }
            bool loadPhase( const XMLNode& node, string_view sourceName ) { return _profile.readPhase( node, sourceName ); }
            bool loadPool( const XMLNode& node, string_view sourceName ) { return _profile.readPool( node, _vocabulary, sourceName ); }
        };
        // 조건 이름의 어휘는 풀보다 먼저 읽는다(파일 안 순서와 상관없이).
        using Element                          = XMLCatalogElement<DirectorElementReader>;
        static constexpr Element kArrElement[] = {
            Element::of<&DirectorElementReader::loadCalendar>( "Calendar" ).uncounted(),
            Element::of<&DirectorElementReader::loadIntensity>( "Intensity" ).uncounted(),
            Element::of<&DirectorElementReader::loadPhase>( "Phase" ).uncounted(),
            Element::of<&DirectorElementReader::loadPool>( "Pool" ).uncounted(),
        };
        DirectorElementReader reader{ *this, {} };
        bValid = XMLCatalogReadResult::readInTableOrder( reader, root, sourceName, kArrElement ).isClean() && bValid;
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
        bValid                                            = XMLFields::readFields( node, _intensity, sourceName ) && bValid;
        for ( XMLNode child = node.findChild( "Signal" ); child; child = child.findNextSibling( "Signal" ) )
        {
            AIDirectorSignalDef signal;
            if ( XMLFields::readFields( child, signal, sourceName ) == false ) // 종류는 ENUM() 이름(필수)
            {
                bValid = false;
                continue;
            }
            if ( findSignalIndex( signal._id ).has_value() )
            {
                SW_LOG_ERROR_ID( "AIDirectorProfile.SignalDeclaredTwice", "%#: signal '%#' is declared twice", sourceName, signal._id.c_str() );
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
        if ( XMLFields::readFields( node, phase, sourceName ) == false )
            return false;
        bValid = GameDataXML::readFloatCurve( node, "Curve", 0.0f, phase._spawnCurve, sourceName ) && bValid; // 키가 없으면 곡선이 대체값을 쓴다
        for ( XMLNode child = node.findChild( "Exit" ); child; child = child.findNextSibling( "Exit" ) )
        {
            AIDirectorExitDef exit;
            if ( XMLFields::readFields( child, exit, sourceName ) )
                phase._listExit.push_back( exit );
            else
                bValid = false;
        }
        if ( findPhaseIndex( phase._id ).has_value() )
        {
            SW_LOG_ERROR_ID( "AIDirectorProfile.PhaseDeclaredTwice", "%#: phase '%#' is declared twice", sourceName, phase._id.c_str() );
            return false;
        }
        _listPhase.push_back( phase );
        return bValid;
    }

    bool AIDirectorProfile::readPool( const XMLNode& node, const ScheduleConditionSchema& vocabulary, string_view sourceName )
    {
        using Internal                               = AIDirectorProfileInternal;
        static constexpr const utf8* kArrPoolChild[] = { "Encounter" };
        bool                         bValid          = Internal::validateChildren( node, kArrPoolChild, sourceName );
        AIDirectorPoolDef            pool;
        // 종류는 적지 않으면 조우 풀이다(Def 초기값). 계기는 반드시 적는다 — 둘 다 ENUM() 이름이다.
        if ( XMLFields::readFields( node, pool, sourceName ) == false )
            return false;
        if ( pool._trigger == AIDirectorPoolTrigger::PhaseEnter && pool._phase.empty() )
        {
            SW_LOG_ERROR_ID( "AIDirectorProfile.PoolTriggeredPhaseEnter", "%#: pool '%#' is triggered on phase enter but names no phase", sourceName, pool._id.c_str() );
            bValid = false;
        }

        for ( XMLNode child = node.findChild( "Encounter" ); child; child = child.findNextSibling( "Encounter" ) )
        {
            AIDirectorEncounterDef encounter;
            if ( XMLFields::readFields( child, encounter, sourceName, {}, &ScheduleCondition::isConditionAttribute ) == false )
            {
                bValid = false;
                continue;
            }
            bValid = encounter._condition.loadFromXML( child, vocabulary, sourceName, encounter._id.c_str() ) && bValid;
            for ( const AIDirectorEncounterDef& other : pool._listEncounter )
            {
                if ( other._id == encounter._id )
                {
                    SW_LOG_ERROR_ID( "AIDirectorProfile.PoolListsTwice", "%#: pool '%#' lists '%#' twice", sourceName, pool._id.c_str(), encounter._id.c_str() );
                    bValid = false;
                }
            }
            pool._listEncounter.push_back( encounter );
        }
        if ( pool._listEncounter.empty() )
        {
            SW_LOG_ERROR_ID( "AIDirectorProfile.PoolNoEncounter", "%#: pool '%#' has no <Encounter>", sourceName, pool._id.c_str() );
            bValid = false;
        }
        for ( const AIDirectorPoolDef& other : _listPool )
        {
            if ( other._id == pool._id )
            {
                SW_LOG_ERROR_ID( "AIDirectorProfile.PoolDeclaredTwice", "%#: pool '%#' is declared twice", sourceName, pool._id.c_str() );
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
            if ( findPhaseIndex( phaseID ).has_value() )
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
        _startPhaseIndex = _startPhase.empty() ? optional<int32>{ 0 } : findPhaseIndex( _startPhase );
        if ( _startPhaseIndex.has_value() == false )
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
                if ( exit._toIndex.has_value() )
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
                if ( pool._phaseIndex.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "AIDirectorProfile.PoolWaitsUnknownPhase", "%#: pool '%#' waits for an unknown phase '%#'", sourceName, pool._id.c_str(), pool._phase.c_str() );
                    bValid = false;
                }
            }
            if ( pool._needSignal.empty() == false && findSignalIndex( pool._needSignal ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "AI.DirectorPoolSignalUnknown", "%#: pool '%#' reads an unknown signal '%#'", sourceName, pool._id.c_str(), pool._needSignal.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }
} // namespace sw

namespace sw
{
    /**
     * `AIDirectorProfile` 의 데이터 파일 종류와 그 공유 캐시입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다).
     * 감독이 국면 · 풀 번호로 읽으므로 다시 읽기는 떼어 낸다(`Detach`).
     */
    SW_MODULE_SHARED_ASSET_CACHE( AIDirector, AIDirectorProfile, SW_GF_API,
                                  AssetTypeDef::makeDataFile( "aidirector", AssetPathRule{ {}, {}, ".director.xml" }, AssetTypeLoader::of<AIDirectorProfile>( MemoryTag::Game, SharedAssetReloadMode::Detach ) ) );
} // namespace sw
