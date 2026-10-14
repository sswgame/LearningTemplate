#include "pch.h"

#include "Editor/Common/Commands/EditorNetConditions.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/NetEmulation.h"

namespace sw::editor
{
    namespace
    {
        struct EditorNetConditionsInternal
        {
            /** @brief 비율(0~1) ↔ 백분율입니다. */
            static constexpr float32 kPercent = 100.0f;
            /** @brief 클라이언트 대상 글의 머리입니다(`client2`). */
            static constexpr string_view kClientPrefix = "client";
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorNetLinkSettings EditorNetConditionsUtil::makeLinkSettings( const NetEmulationConditions& conditions )
    {
        using Internal = EditorNetConditionsInternal;
        EditorNetLinkSettings settings;
        settings._latencyMilliseconds         = static_cast<float32>( conditions._latency * constant::kMillisecondsPerSecond );
        settings._jitterMilliseconds          = static_cast<float32>( conditions._jitter * constant::kMillisecondsPerSecond );
        settings._lossPercent                 = conditions._lossRate * Internal::kPercent;
        settings._bandwidthKilobytesPerSecond = conditions._bandwidthBytesPerSecond / static_cast<int32>( constant::kBytesPerKilobyte );
        return settings;
    }

    NetEmulationConditions EditorNetConditionsUtil::makeConditions( const EditorNetLinkSettings& settings )
    {
        using Internal = EditorNetConditionsInternal;
        NetEmulationConditions conditions;
        conditions._latency                 = static_cast<float64>( MathUtil::max( 0.0f, settings._latencyMilliseconds ) ) / constant::kMillisecondsPerSecond;
        conditions._jitter                  = static_cast<float64>( MathUtil::max( 0.0f, settings._jitterMilliseconds ) ) / constant::kMillisecondsPerSecond;
        conditions._lossRate                = MathUtil::clamp( settings._lossPercent, 0.0f, Internal::kPercent ) / Internal::kPercent;
        conditions._bandwidthBytesPerSecond = MathUtil::max( 0, settings._bandwidthKilobytesPerSecond ) * static_cast<int32>( constant::kBytesPerKilobyte );
        return conditions;
    }

    const utf8* EditorNetConditionsUtil::toString( EditorNetLinkDirection direction )
    {
        switch ( direction )
        {
            case EditorNetLinkDirection::Up:
                return "up";
            case EditorNetLinkDirection::Down:
                return "down";
            case EditorNetLinkDirection::Both:
                return "both";
        }
        return "both";
    }

    string EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection direction, const EditorNetLinkSettings& settings )
    {
        return string( "net.emulation " ) + toString( direction ) + " " + makeConditions( settings ).makeOverrideText();
    }

    string EditorNetConditionsUtil::makePresetCommand( string_view presetName )
    {
        return "net.emulation both " + string( presetName );
    }

    string EditorNetConditionsUtil::makeSpikeCommand( float32 seconds )
    {
        utf8 arrBuffer[constant::kMaxBuffer32];
        (void)StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, MathUtil::max( 0.0f, seconds ) );
        return string( "net.emulation spike " ) + arrBuffer;
    }

    bool EditorNetConditionsUtil::tryParseTarget( string_view text, int32 processCount, int32& outFirstIndex, int32& outLastIndex )
    {
        using Internal         = EditorNetConditionsInternal;
        int32      firstIndex  = 0;
        int32      lastIndex   = -1;
        int32      number      = 0;
        const bool bClientName = text.size() > Internal::kClientPrefix.size() && text.substr( 0, Internal::kClientPrefix.size() ) == Internal::kClientPrefix &&
                                 StringUtil::parseInt( text.substr( Internal::kClientPrefix.size() ), number ) && number >= 1;
        const bool bProcessNumber = bClientName == false && StringUtil::parseInt( text, number ) && number >= 0;
        if ( text == "all" )
        {
            lastIndex = processCount - 1;
        }
        else if ( text == "server" )
        {
            lastIndex = 0;
        }
        else if ( text == "clients" )
        {
            firstIndex = 1;
            lastIndex  = processCount - 1;
        }
        else if ( bClientName || bProcessNumber )
        {
            firstIndex = number;
            lastIndex  = number;
        }
        else
        {
            return false;
        }
        if ( firstIndex > lastIndex || lastIndex >= processCount )
            return false;
        outFirstIndex = firstIndex;
        outLastIndex  = lastIndex;
        return true;
    }
} // namespace sw::editor
