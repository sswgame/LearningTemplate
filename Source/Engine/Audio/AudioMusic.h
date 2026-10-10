/**
 * @file AudioMusic.h
 * @brief 적응형 음악 데이터(`*.music.xml`) — 템포 · 박자, 구간(세그먼트)마다 세로 레이어(파라미터로 페이드), 구간 사이 전환 규칙(박 · 마디 · 구간 끝에 맞춤, 스팅어)입니다.
 * @details Wwise Interactive Music(Music Segment · Music Playlist · Transition Matrix · Stinger) · FMOD 의 timeline + transition marker 의 자리입니다.
 *          **세로(vertical)**: 한 구간의 레이어들은 함께 돌고, 레이어마다 게임 파라미터 곡선이 볼륨을 정합니다(긴장도가 오르면 드럼이 들어온다).
 *          **가로(horizontal)**: `AudioEngine::setMusicSegment` 가 전환 규칙의 맞춤 지점(다음 박 · 다음 마디 · 구간 끝)까지 기다렸다가 샘플 단위로 정확히
 *          바꾸고, 그 자리에서 스팅어를 냅니다. 클립은 구간 길이(마디 × 박자 × 박 길이)에 맞춰 만듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 전환이 기다리는 맞춤 지점입니다. */
    ENUM()
    enum class AudioMusicSync : uint8
    {
        Immediate = 0, ///< 다음 블록에 바로
        NextBeat,      ///< 다음 박
        NextBar,       ///< 다음 마디
        SegmentEnd,    ///< 지금 구간(루프 한 바퀴)의 끝
    };
} // namespace sw

namespace sw
{
    /** @brief 구간의 레이어 하나(세로 재배치)입니다. */
    REFLECT()
    struct SW_API AudioMusicLayerDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Clip of this layer; as long as the segment" )
        hashed_string _path{};
        PROPERTY( Tooltip = "Game parameter that drives this layer's volume; empty plays it always" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "(parameter value, volume dB) points" )
        vector<AudioCurvePoint> _listPoint{};
        PROPERTY( Tooltip = "Layer trim", Meta = "Units=dB" )
        float32 _volumeDb{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 음악 구간 하나입니다. */
    REFLECT()
    struct SW_API AudioMusicSegmentDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Segment name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Segment the music moves to at the end when it does not loop" )
        hashed_string _next{};
        PROPERTY( Tooltip = "Layers played together" )
        vector<AudioMusicLayerDesc> _listLayer{};
        PROPERTY( Min = 0.0, Tooltip = "Tempo; 0 uses the music's", Meta = "Units=BPM" )
        float32 _tempo{ 0.0f };
        PROPERTY( Tooltip = "Beats per bar; 0 uses the music's" )
        uint32 _beatsPerBar{ 0 };
        PROPERTY( Min = 1, Tooltip = "Length in bars" )
        uint32 _bars{ 4 };
        PROPERTY( Tooltip = "Loops until a transition; otherwise moves to _next at the end" )
        bool _bLoop{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 구간 사이 전환 규칙 하나입니다. `_from` 이 비었거나 `*` 면 어느 구간에서든입니다. */
    REFLECT()
    struct SW_API AudioMusicTransitionDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Source segment; empty or * means any" )
        hashed_string _from{};
        PROPERTY( Tooltip = "Destination segment" )
        hashed_string _to{};
        PROPERTY( Tooltip = "Clip played on the switch point (one shot)" )
        hashed_string _stinger{};
        PROPERTY( Min = 0.0, Tooltip = "Fade out of the old segment from the switch point", Units = s )
        float32 _fadeOutSeconds{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Fade in of the new segment from the switch point", Units = s )
        float32 _fadeInSeconds{ 0.0f };
        PROPERTY( Tooltip = "Switch point" )
        AudioMusicSync _sync{ AudioMusicSync::NextBar };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 적응형 음악 하나입니다.
     * @code
     *     <AudioMusicDesc _bus="music" _tempo="120" _beatsPerBar="4" _startSegment="Explore" _layerFadeSeconds="1.5">
     *         <_listSegment>
     *             <AudioMusicSegmentDesc _name="Explore" _bars="8">
     *                 <_listLayer>
     *                     <AudioMusicLayerDesc _path="game/x/music/explore_pad.ogg" />
     *                     <AudioMusicLayerDesc _path="game/x/music/explore_drums.ogg" _parameter="Danger">
     *                         <_listPoint><AudioCurvePoint _x="0" _y="-96" /><AudioCurvePoint _x="1" _y="0" /></_listPoint>
     *                     </AudioMusicLayerDesc>
     *                 </_listLayer>
     *             </AudioMusicSegmentDesc>
     *         </_listSegment>
     *         <_listTransition><AudioMusicTransitionDesc _from="*" _to="Combat" _sync="NextBar" _stinger="game/x/music/hit.ogg" /></_listTransition>
     *     </AudioMusicDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioMusicDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus the music goes to" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Segment that starts first" )
        hashed_string _startSegment{};
        PROPERTY( Tooltip = "Segments" )
        vector<AudioMusicSegmentDesc> _listSegment{};
        PROPERTY( Tooltip = "Transition rules (first match wins; a rule from the current segment beats a * rule)" )
        vector<AudioMusicTransitionDesc> _listTransition{};
        PROPERTY( Min = 1.0, Tooltip = "Tempo", Meta = "Units=BPM" )
        float32 _tempo{ 120.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time a parameter-driven layer takes to move between its old and new volume", Units = s )
        float32 _layerFadeSeconds{ 1.0f };
        PROPERTY( Min = 1, Tooltip = "Beats per bar" )
        uint32 _beatsPerBar{ 4 };
        PROPERTY( Tooltip = "Switch point when no rule matches" )
        AudioMusicSync _defaultSync{ AudioMusicSync::NextBar };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );
        /** @brief 이름 · 레이어 · 길이 · 템포 · 시작 구간 · 다음 구간 · 전환 이름 · 곡선 순서를 검사합니다. */
        [[nodiscard]] bool validate( string_view sourceName ) const;

        /** @brief 구간 번호입니다. 없으면 -1 입니다. */
        int32 findSegmentIndex( const hashed_string& name ) const;
        /** @brief @p from → @p to 전환 규칙입니다(구간을 적은 규칙이 `*` 규칙보다 먼저). 없으면 nullptr 입니다. */
        const AudioMusicTransitionDesc* findTransition( const hashed_string& from, const hashed_string& to ) const;
        /** @brief 구간의 박 길이(프레임)입니다. */
        float64 computeFramesPerBeat( uint32 segmentIndex ) const;
        /** @brief 구간의 마디당 박 수입니다. */
        uint32 getBeatsPerBar( uint32 segmentIndex ) const;
        /** @brief 구간 길이(프레임)입니다. */
        uint64 computeSegmentFrames( uint32 segmentIndex ) const;
    };
} // namespace sw
