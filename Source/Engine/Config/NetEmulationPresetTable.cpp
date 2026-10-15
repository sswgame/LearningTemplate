#include "pch.h"

#include "Engine/Config/NetEmulationPresetTable.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    SW_LOG_CALLER( "NetEmulationPresetTable" );

    namespace
    {
        struct NetEmulationPresetTableInternal
        {
            static constexpr float64 kSecondsPerMillisecond = 1.0 / static_cast<float64>( constant::kMillisecondsPerSecond );
            static constexpr float32 kRatePerPercent        = 0.01f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetEmulationConditions NetEmulationPresetDef::makeConditions() const
    {
        using Internal = NetEmulationPresetTableInternal;
        NetEmulationConditions conditions;
        conditions._latency                 = static_cast<float64>( _latencyMs ) * Internal::kSecondsPerMillisecond;
        conditions._jitter                  = static_cast<float64>( _jitterMs ) * Internal::kSecondsPerMillisecond;
        conditions._lossRate                = _lossPercent * Internal::kRatePerPercent;
        conditions._duplicateRate           = _duplicatePercent * Internal::kRatePerPercent;
        conditions._reorderRate             = _reorderPercent * Internal::kRatePerPercent;
        conditions._bandwidthBytesPerSecond = _bandwidthKilobytesPerSecond * static_cast<int32>( constant::kBytesPerKilobyte );
        return conditions;
    }

    bool NetEmulationPresetTable::loadFromResource( string_view path )
    {
        _listPreset.clear();
        bool bOk = ReflectedXMLFile::loadDesc( path, *this );
        if ( bOk && _listPreset.empty() )
        {
            SW_LOG_ERROR( "%#: no presets in <_listPreset>", path );
            bOk = false;
        }
        for ( size_t presetIndex = 0; presetIndex < _listPreset.size(); ++presetIndex )
        {
            const NetEmulationPresetDef& preset = _listPreset[presetIndex];
            if ( preset._name.empty() )
            {
                SW_LOG_ERROR( "%#: preset %# has no _name", path, static_cast<int32>( presetIndex ) );
                bOk = false;
                continue;
            }
            vector<string> listRangeError;
            SerializerUtil::collectRangeErrors( *NetEmulationPresetDef::StaticType(), &preset, preset._name, listRangeError );
            for ( const string& rangeError : listRangeError )
            {
                SW_LOG_ERROR( "%#: %#", path, rangeError.c_str() );
                bOk = false;
            }
            for ( size_t otherIndex = 0; otherIndex < presetIndex; ++otherIndex )
            {
                if ( StringUtil::equals( _listPreset[otherIndex]._name, preset._name, true ) )
                {
                    SW_LOG_ERROR( "%#: preset '%#' appears twice", path, preset._name.c_str() );
                    bOk = false;
                }
            }
        }
        if ( bOk == false )
            _listPreset.clear();
        return bOk;
    }

    const NetEmulationPresetDef* NetEmulationPresetTable::findPreset( string_view name ) const
    {
        for ( const NetEmulationPresetDef& preset : _listPreset )
        {
            if ( StringUtil::equals( preset._name, name, true ) )
                return &preset;
        }
        return nullptr;
    }

    bool NetEmulationPresetTable::tryGetConditions( string_view name, NetEmulationConditions& outConditions ) const
    {
        const NetEmulationPresetDef* pPreset = findPreset( name );
        if ( pPreset == nullptr )
            return false;
        outConditions = pPreset->makeConditions();
        return true;
    }

    string NetEmulationPresetTable::makePresetNames() const
    {
        string names;
        for ( const NetEmulationPresetDef& preset : _listPreset )
        {
            if ( names.empty() == false )
                names += ", ";
            names += preset._name;
        }
        return names;
    }
} // namespace sw
