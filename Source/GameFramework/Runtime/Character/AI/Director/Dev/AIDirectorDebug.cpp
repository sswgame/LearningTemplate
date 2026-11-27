/**
 * @file AIDirectorDebug.cpp
 * @brief `AIDirector` 의 디버그 설명(`explain` · `dumpTrace`)입니다. 개발 도구 · 테스트만 불러 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 언리얼 Visual Logger 가 `ENABLE_VISUAL_LOG` 로 빠지는 것과 같다. 멤버라 GameFrameworkDev 로 옮기지 못한다(다른 모듈은 내보낸 클래스의 멤버를 정의하지 못한다).
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirector.h"
#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"

namespace sw
{
    void AIDirector::explain( string& outText ) const
    {
        outText.clear();
        if ( _pProfile == nullptr || _phaseIndex < 0 )
        {
            outText = "director: not initialized\n";
            return;
        }
        const AIDirectorPhaseDef&                 phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        StringBuilder<constant::buffer::kMax4096> text;
        text.appendFormat( "director t=%.2fs phase '%#' (%.2fs) intensity %.2f calm %.2fs cycle %#\n", _time, phase._id.c_str(), _phaseTime, _intensity,
                           getIntensityModel().getCalmSeconds(), _cycle );
        if ( _pSpawnTable != nullptr )
        {
            text.appendFormat( "  spawn: budget %.2f scale %.2f alive %# pending '%#'\n", _spawnDirector.getBudget(), _spawnDirector.getBudgetScale(),
                               _spawnDirector.getTotalAliveCount(), _spawnDirector.getPendingEntry().c_str() );
        }
        const float32 calmSeconds = getIntensityModel().getCalmSeconds();
        for ( size_t exitIndex = 0; exitIndex < phase._listExit.size(); ++exitIndex )
        {
            const AIDirectorExitDef& exit = phase._listExit[exitIndex];
            text.appendFormat( "  exit #%# -> '%#':", exitIndex, exit._to.c_str() );
            bool bBlocked = false;
            if ( _phaseTime < exit._minTime )
            {
                text.appendFormat( " [minTime %.2f]", exit._minTime );
                bBlocked = true;
            }
            if ( exit._intensityAbove >= 0.0f && _intensity < exit._intensityAbove )
            {
                text.appendFormat( " [intensityAbove %.2f]", exit._intensityAbove );
                bBlocked = true;
            }
            if ( exit._intensityBelow >= 0.0f && _intensity > exit._intensityBelow )
            {
                text.appendFormat( " [intensityBelow %.2f]", exit._intensityBelow );
                bBlocked = true;
            }
            if ( calmSeconds < exit._calmFor )
            {
                text.appendFormat( " [calmFor %.2f]", exit._calmFor );
                bBlocked = true;
            }
            text.append( bBlocked ? "\n" : " open\n" );
        }
        const vector<AIDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AIDirectorPoolDef& pool  = listPool[poolIndex];
            const PoolState&         state = _listPoolState[poolIndex];
            text.appendFormat( "  pool '%#' (%#, %#)%#%# budget %.2f timer %.2f\n", pool._id.c_str(), getEnumName( pool._kind ), getEnumName( pool._trigger ),
                               isPoolActive( pool ) ? "" : " [pacing]", isPoolCooledDown( pool, state ) ? "" : " [cooldown]", state._budget, state._timer );
            for ( size_t encounterIndex = 0; encounterIndex < pool._listEncounter.size(); ++encounterIndex )
            {
                const AIDirectorEncounterDef& encounter = pool._listEncounter[encounterIndex];
                const uint32                  mask      = computeBlockMask( static_cast<int32>( poolIndex ), static_cast<int32>( encounterIndex ) );
                text.appendFormat( "    %# %# w%.2f picked %#", mask == 0 ? "+" : "-", encounter._id.c_str(), encounter._weight,
                                   state._listEncounter[encounterIndex]._count );
                for ( uint32 bitIndex = 0; bitIndex < AIDirectorBlock::kCount; ++bitIndex )
                {
                    if ( ( mask & ( 1u << bitIndex ) ) != 0 )
                        text.appendFormat( " [%#]", AIDirectorBlock::getName( bitIndex ) );
                }
                text.append( "\n" );
            }
        }
        outText = text.c_str();
    }

    void AIDirector::dumpTrace( string& outText ) const
    {
        outText.clear();
        for ( const AIDirectorEvent& event : _listTrace )
        {
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "[%.2fs] I=%.2f %# '%#'", event._time, event._intensity, getEnumName( event._kind ), event._id.c_str() );
            if ( event._source.empty() == false )
                line.appendFormat( " from '%#'", event._source.c_str() );
            if ( event._kind == AIDirectorEventKind::PhaseChanged )
                line.appendFormat( " exit %# cycle %#", event._detail, event._count );
            else
                line.appendFormat( " count %# scale %.2f", event._count, event._scale );
            if ( event._spawnID != 0 )
                line.appendFormat( " spawn #%#", event._spawnID );
            line.append( "\n" );
            outText += line.c_str();
        }
    }
} // namespace sw
