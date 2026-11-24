#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorStage.h"

#include "Core/Log/Logger.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"

namespace sw
{
    bool AsymmetricHorrorStage::populate( AsymmetricHorrorMatch& inoutMatch ) const
    {
        const bool bMissingPlace = _listSurvivorStart.empty() || _listGenerator.empty() || _listGate.empty();
        if ( bMissingPlace || _halfExtent._x <= 0.0f || _halfExtent._y <= 0.0f )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorStage.Incomplete", "AsymmetricHorrorStage: the stage needs survivors (%#), generators (%#), gates (%#) and a positive half extent (%#, %#)",
                             _listSurvivorStart.size(), _listGenerator.size(), _listGate.size(), _halfExtent._x, _halfExtent._y );
            return false;
        }
        bool bAccepted = true;
        for ( const float3& position : _listSurvivorStart )
        {
            bAccepted = inoutMatch.addSurvivor( position ).has_value() && bAccepted;
        }
        for ( const float3& position : _listGenerator )
        {
            bAccepted = inoutMatch.addGenerator( position ).has_value() && bAccepted;
        }
        for ( const float3& position : _listGate )
        {
            bAccepted = inoutMatch.addGate( position ).has_value() && bAccepted;
        }
        if ( bAccepted == false )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorStage.Refused", "AsymmetricHorrorStage: the match refused a survivor, generator or gate - check the health, repair, heal and gate rules of the catalog" );
            return false;
        }
        for ( const float3& position : _listHook )
        {
            (void)inoutMatch.addHook( position ); // 번호는 목록 순서 그대로다
        }
        for ( const float3& position : _listPallet )
        {
            (void)inoutMatch.addPallet( position );
        }
        for ( const float3& position : _listWindow )
        {
            (void)inoutMatch.addWindow( position );
        }
        for ( const float3& position : _listLocker )
        {
            (void)inoutMatch.addLocker( position );
        }
        inoutMatch.setKillerPosition( _killerStart );
        inoutMatch.setHatchPosition( _hatch );
        return true;
    }

    float3 AsymmetricHorrorStage::limitMove( const float3& position, const float3& direction ) const
    {
        float3     limited{ direction._x, 0.0f, direction._z };
        const bool bLeavesX = ( position._x >= _halfExtent._x && limited._x > 0.0f ) || ( position._x <= -_halfExtent._x && limited._x < 0.0f );
        const bool bLeavesZ = ( position._z >= _halfExtent._y && limited._z > 0.0f ) || ( position._z <= -_halfExtent._y && limited._z < 0.0f );
        if ( bLeavesX )
            limited._x = 0.0f;
        if ( bLeavesZ )
            limited._z = 0.0f;
        return limited;
    }
} // namespace sw
