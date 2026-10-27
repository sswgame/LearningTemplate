#include "pch.h"

#include "Engine/Config/NetEmulationPresetTable.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Network/Transport/NetEmulation.h"

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

    bool NetEmulationPresetTable::loadFromResource( const ResourceID& resourceID )
    {
        const string_view path = resourceID.view();
        _listPreset.clear();
        bool bOk = ReflectedXMLFile::loadDesc( resourceID, *this );
        if ( bOk && _listPreset.empty() )
        {
            SW_LOG_ERROR_ID( "Net.PresetTableEmpty", "%#: no presets in <_listPreset>", path );
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
            // 원소의 `Min` · `Max` 는 `loadDesc` 가 본다(`SerializerUtil::collectRangeErrors` 는 시퀀스 원소까지 본다).
            for ( size_t otherIndex = 0; otherIndex < presetIndex; ++otherIndex )
            {
                if ( StringUtil::equals( _listPreset[otherIndex]._name, preset._name, true ) )
                {
                    SW_LOG_ERROR_ID( "Net.PresetDuplicate", "%#: preset '%#' appears twice", path, preset._name.c_str() );
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
        return VectorUtil::findIf( _listPreset, [&]( const NetEmulationPresetDef& preset )
        { return StringUtil::equals( preset._name, name, true ); } );
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
