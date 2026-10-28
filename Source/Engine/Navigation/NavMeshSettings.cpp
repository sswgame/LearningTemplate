#include "pch.h"

#include "Engine/Navigation/NavMeshSettings.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "NavMeshSettings" );

    namespace
    {
        struct NavMeshSettingsInternal
        {
            static uint64 mixFloat( uint64 hash, float32 value )
            {
                return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &value ), sizeof( value ), false, hash );
            }

            static uint64 mixUint( uint64 hash, uint32 value )
            {
                return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &value ), sizeof( value ), false, hash );
            }

            static bool isAgentTypeInRange( const NavAgentTypeDef& agentType )
            {
                const bool bBody   = agentType._radius >= 0.0f && agentType._height > 0.0f && agentType._maxClimb >= 0.0f;
                const bool bSlope  = 0.0f <= agentType._maxSlope && agentType._maxSlope < MathUtil::kHalfPi;
                const bool bVoxel  = agentType._cellSize >= 0.01f && agentType._cellHeight >= 0.01f;
                const bool bTile   = 8u <= agentType._tileSize && agentType._tileSize <= 256u;
                const bool bDetail = agentType._maxEdgeError >= 0.1f && agentType._detailSampleDistance >= 0.0f && agentType._detailSampleMaxError >= 0.0f;
                const bool bCache  = 1u <= agentType._maxCacheObstacleCount && agentType._maxCacheObstacleCount <= 65535u;
                return bBody && bSlope && bVoxel && bTile && bDetail && bCache && agentType._maxCrowdAgentCount > 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NavMeshSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        *this                       = NavMeshSettings{};
        if ( XMLSerializer::loadFile( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "Navigation settings could not be read or hold unknown keys: %#", assetPath );
            return false;
        }
        return validate();
    }

    bool NavMeshSettings::loadFromXMLText( string_view xmlText )
    {
        *this = NavMeshSettings{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "Navigation settings text could not be read or holds unknown keys" );
            return false;
        }
        return validate();
    }

    bool NavMeshSettings::validate() const
    {
        using Internal = NavMeshSettingsInternal;
        bool bValid    = true;
        // 영역과 에이전트 종류는 데이터가 선언한다 — 비었다고 코드가 "Default" 를 지어 채우지 않는다.
        if ( _listArea.empty() || _listAgentType.empty() )
        {
            SW_LOG_ERROR( "Navigation settings need at least one area and one agent type (%# areas, %# agent types declared)", _listArea.size(), _listAgentType.size() );
            bValid = false;
        }
        if ( _listArea.size() > NavigationConstant::kMaxAreaCount )
        {
            SW_LOG_ERROR( "Navigation settings declare %# areas - at most %# are supported", _listArea.size(), NavigationConstant::kMaxAreaCount );
            bValid = false;
        }
        if ( _maxConcurrentTileBakeCount == 0 || _obstacleMoveThreshold < 0.0f )
        {
            SW_LOG_ERROR( "Navigation settings need at least one concurrent tile build and a non-negative obstacle move threshold" );
            bValid = false;
        }
        for ( size_t areaIndex = 0; areaIndex < _listArea.size(); ++areaIndex )
        {
            const NavAreaDef& area = _listArea[areaIndex];
            if ( area._name.empty() || area._cost < 1.0f )
            {
                SW_LOG_ERROR( "Navigation area %# needs a name and a cost of at least 1", areaIndex );
                bValid = false;
            }
            for ( size_t otherIndex = areaIndex + 1; otherIndex < _listArea.size(); ++otherIndex )
            {
                if ( _listArea[otherIndex]._name == area._name )
                {
                    SW_LOG_ERROR( "Navigation area '%#' is declared twice", area._name.c_str() );
                    bValid = false;
                }
            }
        }
        for ( size_t typeIndex = 0; typeIndex < _listAgentType.size(); ++typeIndex )
        {
            const NavAgentTypeDef& agentType = _listAgentType[typeIndex];
            if ( agentType._name.empty() )
            {
                SW_LOG_ERROR( "Navigation agent type %# has no name", typeIndex );
                bValid = false;
            }
            if ( Internal::isAgentTypeInRange( agentType ) == false )
            {
                SW_LOG_ERROR( "Navigation agent type '%#' has a value out of range (radius, height, climb, slope, cell, tile, detail or crowd size)",
                              agentType._name.c_str() );
                bValid = false;
            }
            for ( size_t otherIndex = typeIndex + 1; otherIndex < _listAgentType.size(); ++otherIndex )
            {
                if ( _listAgentType[otherIndex]._name == agentType._name )
                {
                    SW_LOG_ERROR( "Navigation agent type '%#' is declared twice", agentType._name.c_str() );
                    bValid = false;
                }
            }
        }
        if ( _defaultAgentType.empty() == false && findAgentType( _defaultAgentType ) == nullptr )
        {
            SW_LOG_ERROR( "Navigation default agent type '%#' is not declared", _defaultAgentType.c_str() );
            bValid = false;
        }
        return bValid;
    }

    const NavAgentTypeDef* NavMeshSettings::findAgentType( const hashed_string& name ) const
    {
        const hashed_string& wanted = name.empty() ? _defaultAgentType : name;
        if ( wanted.empty() )
            return _listAgentType.empty() ? nullptr : &_listAgentType.front();
        return VectorUtil::findBy<&NavAgentTypeDef::_name>( _listAgentType, wanted );
    }

    bool NavMeshSettings::findAreaIndex( const hashed_string& name, uint8& outIndex ) const
    {
        const size_t count = _listArea.size() < NavigationConstant::kMaxAreaCount ? _listArea.size() : NavigationConstant::kMaxAreaCount;
        for ( size_t areaIndex = 0; areaIndex < count; ++areaIndex )
        {
            if ( _listArea[areaIndex]._name == name )
            {
                outIndex = static_cast<uint8>( areaIndex );
                return true;
            }
        }
        return false;
    }

    NavQueryFilter NavMeshSettings::makeDefaultFilter() const
    {
        NavQueryFilter filter;
        const size_t   count = _listArea.size() < NavigationConstant::kMaxAreaCount ? _listArea.size() : NavigationConstant::kMaxAreaCount;
        for ( size_t areaIndex = 0; areaIndex < count; ++areaIndex )
        {
            filter._arrAreaCost[areaIndex] = _listArea[areaIndex]._cost;
        }
        return filter;
    }

    uint64 NavMeshSettings::computeAgentTypeHash( const NavAgentTypeDef& agentType )
    {
        using Internal = NavMeshSettingsInternal;
        uint64 hash    = HashUtil::computeStringHash64( agentType._name.c_str(), agentType._name.size(), true );
        hash           = Internal::mixFloat( hash, agentType._radius );
        hash           = Internal::mixFloat( hash, agentType._height );
        hash           = Internal::mixFloat( hash, agentType._maxClimb );
        hash           = Internal::mixFloat( hash, agentType._maxSlope );
        hash           = Internal::mixFloat( hash, agentType._cellSize );
        hash           = Internal::mixFloat( hash, agentType._cellHeight );
        hash           = Internal::mixUint( hash, agentType._tileSize );
        hash           = Internal::mixUint( hash, agentType._minRegionSize );
        hash           = Internal::mixUint( hash, agentType._mergeRegionSize );
        hash           = Internal::mixFloat( hash, agentType._maxEdgeLength );
        hash           = Internal::mixFloat( hash, agentType._maxEdgeError );
        hash           = Internal::mixFloat( hash, agentType._detailSampleDistance );
        hash           = Internal::mixFloat( hash, agentType._detailSampleMaxError );
        // 장애물 방식이 바뀌면 쿠킹본의 바이트 종류(타일 · 높이 층)가 바뀐다 — 낡은 쿠킹본으로 읽지 않는다. 기본(다시 베이크)은 섞지 않아 있던 쿠킹본이 그대로 맞는다.
        if ( agentType._obstacleMode != NavObstacleMode::RebakeTiles )
            hash = Internal::mixUint( hash, static_cast<uint32>( agentType._obstacleMode ) );
        return hash;
    }

    /** @brief 내비메시 설정 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( NavMeshSettings, "navmeshsettings", nullptr, nullptr, "navmeshsettings.xml", &AssetDataKindUtil::loadCatalog<NavMeshSettings> );
} // namespace sw
