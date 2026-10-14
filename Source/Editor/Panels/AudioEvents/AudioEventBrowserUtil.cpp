#include "pch.h"

#include "Editor/Panels/AudioEvents/AudioEventBrowserUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Localization/LocText.h"

#include <algorithm>
#include <cstdio>

namespace sw::editor
{
    namespace
    {
        struct AudioEventBrowserUtilInternal
        {
            static string formatDistanceRange( float64 minDistance, float64 maxDistance )
            {
                utf8 arrText[96]{};
                (void)std::snprintf( arrText, sizeof( arrText ), "%.2f .. %.2f m", minDistance, maxDistance );
                return string{ arrText };
            }

            static string formatFade( float64 fadeInSeconds, float64 fadeOutSeconds )
            {
                utf8 arrText[96]{};
                (void)std::snprintf( arrText, sizeof( arrText ), "in %.2f s / out %.2f s", fadeInSeconds, fadeOutSeconds );
                return string{ arrText };
            }

            static string formatRange( float32 low, float32 high, const utf8* pUnit )
            {
                utf8 arrText[96]{};
                if ( low == high )
                    (void)std::snprintf( arrText, sizeof( arrText ), "%.2f %s", static_cast<float64>( low ), pUnit );
                else
                    (void)std::snprintf( arrText, sizeof( arrText ), "%.2f .. %.2f %s", static_cast<float64>( low ), static_cast<float64>( high ), pUnit );
                return string{ arrText };
            }

            static const utf8* getContainerName( AudioContainerType container )
            {
                switch ( container )
                {
                    case AudioContainerType::Random:
                        return "Random";
                    case AudioContainerType::Sequence:
                        return "Sequence";
                    case AudioContainerType::Layer:
                        return "Layer";
                }
                return "?";
            }

            static const utf8* getStealName( AudioStealPolicy steal )
            {
                switch ( steal )
                {
                    case AudioStealPolicy::Reject:
                        return "Reject";
                    case AudioStealPolicy::Oldest:
                        return "Oldest";
                    case AudioStealPolicy::Quietest:
                        return "Quietest";
                    case AudioStealPolicy::Farthest:
                        return "Farthest";
                }
                return "?";
            }

            static const utf8* getVirtualName( AudioVirtualMode mode )
            {
                switch ( mode )
                {
                    case AudioVirtualMode::Virtualize:
                        return "Virtualize";
                    case AudioVirtualMode::Stop:
                        return "Stop";
                    case AudioVirtualMode::KeepReal:
                        return "KeepReal";
                }
                return "?";
            }

            static float32 computeUsage( uint32 count, uint32 limit )
            {
                return limit == 0 ? 0.0f : MathUtil::clamp( static_cast<float32>( count ) / static_cast<float32>( limit ), 0.0f, 1.0f );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void AudioEventBrowserUtil::filterEventNames( const vector<hashed_string>& listName, string_view filter, vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const hashed_string& name : listName )
        {
            if ( StringUtil::contains( name.view(), filter, true ) )
                outListName.push_back( name );
        }
        std::sort( outListName.begin(), outListName.end(), HashedStringLexicalLess{} );
    }

    void AudioEventBrowserUtil::makeEventFieldRows( const AudioEventDesc& eventDesc, const AudioAttenuationDesc& attenuation, vector<AudioEventFieldRow>& outListRow )
    {
        using Internal = AudioEventBrowserUtilInternal;
        outListRow.clear();
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldBus" ), string{ eventDesc._bus.c_str() } } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldVolume" ), Internal::formatRange( eventDesc._volumeDbMin, eventDesc._volumeDbMax, "dB" ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldPitch" ), Internal::formatRange( eventDesc._pitchMin, eventDesc._pitchMax, "st" ) } );
        if ( eventDesc._attenuation.empty() )
        {
            outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldRadius" ), string{ "2D" } } );
        }
        else
        {
            string radius = Internal::formatDistanceRange( static_cast<float64>( attenuation._minDistance ), static_cast<float64>( attenuation._maxDistance ) );
            radius += " (";
            radius += eventDesc._attenuation.c_str();
            radius += ")";
            outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldRadius" ), radius } );
        }
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldContainer" ), string{ Internal::getContainerName( eventDesc._container ) } } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldClipCount" ), to_string( static_cast<uint32>( eventDesc._listClip.size() ) ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldMaxInstances" ),
                                                  eventDesc._maxInstances == 0 ? string{ "-" } : to_string( eventDesc._maxInstances ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldSteal" ), string{ Internal::getStealName( eventDesc._steal ) } } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldPriority" ), to_string( eventDesc._priority ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldVirtual" ), string{ Internal::getVirtualName( eventDesc._virtual ) } } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldCooldown" ), Internal::formatRange( eventDesc._cooldownSeconds, eventDesc._cooldownSeconds, "s" ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldFade" ),
                                                  Internal::formatFade( static_cast<float64>( eventDesc._fadeInSeconds ), static_cast<float64>( eventDesc._fadeOutSeconds ) ) } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldLoop" ), string{ eventDesc._bLoop ? "true" : "false" } } );
        outListRow.push_back( AudioEventFieldRow{ SW_LOCKEY( "EditorAudioEvents", "FieldParameterCount" ), to_string( static_cast<uint32>( eventDesc._listParameterMap.size() ) ) } );
    }

    void AudioEventBrowserUtil::makeInstanceRows( const vector<AudioInstanceStatus>& listStatus, const hashed_string& eventFilter, vector<AudioInstanceStatus>& outListRow )
    {
        outListRow.clear();
        for ( const AudioInstanceStatus& status : listStatus )
        {
            if ( eventFilter.empty() || status._event == eventFilter )
                outListRow.push_back( status );
        }
        std::sort( outListRow.begin(), outListRow.end(), []( const AudioInstanceStatus& lhs, const AudioInstanceStatus& rhs )
        {
            const bool bLhsOpen = lhs._remainingSeconds < 0.0f;
            const bool bRhsOpen = rhs._remainingSeconds < 0.0f;
            if ( bLhsOpen != bRhsOpen )
                return bRhsOpen; // 끝을 아는 것이 먼저다
            if ( bLhsOpen == false && lhs._remainingSeconds != rhs._remainingSeconds )
                return lhs._remainingSeconds < rhs._remainingSeconds;
            return lhs._playingID < rhs._playingID;
        } );
    }

    AudioVoiceProfile AudioEventBrowserUtil::makeVoiceProfile( const AudioEngineStats& stats )
    {
        using Internal = AudioEventBrowserUtilInternal;
        AudioVoiceProfile profile;
        profile._realVoiceCount     = stats._realVoiceCount;
        profile._virtualVoiceCount  = stats._virtualVoiceCount;
        profile._voiceCount         = stats._voiceCount;
        profile._peakRealVoiceCount = stats._peakRealVoiceCount;
        profile._maxRealVoiceCount  = stats._maxRealVoiceCount;
        profile._maxVoiceCount      = stats._maxVoiceCount;
        profile._instanceCount      = stats._instanceCount;
        profile._playedEventCount   = stats._playedEventCount;
        profile._droppedEventCount  = stats._droppedEventCount;
        profile._realUsage          = Internal::computeUsage( stats._realVoiceCount, stats._maxRealVoiceCount );
        profile._voiceUsage         = Internal::computeUsage( stats._voiceCount, stats._maxVoiceCount );
        profile._bRealLimitReached  = stats._maxRealVoiceCount != 0 && stats._realVoiceCount >= stats._maxRealVoiceCount;
        profile._bVoiceLimitReached = stats._maxVoiceCount != 0 && stats._voiceCount >= stats._maxVoiceCount;
        return profile;
    }

    string AudioEventBrowserUtil::formatRemainingSeconds( float32 seconds )
    {
        if ( seconds < 0.0f )
            return string{ SW_LOCKEY( "EditorAudioEvents", "RemainingLoop" ) };
        utf8 arrText[32]{};
        (void)std::snprintf( arrText, sizeof( arrText ), "%.2f s", static_cast<float64>( seconds ) );
        return string{ arrText };
    }
} // namespace sw::editor
