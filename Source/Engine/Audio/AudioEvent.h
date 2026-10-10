/**
 * @file AudioEvent.h
 * @brief 사운드 이벤트 데이터(`*.audioevents.xml`) — 컨테이너(랜덤 · 순서 · 레이어)의 클립, 볼륨 · 피치 범위, 쿨다운, 동시 재생 상한과 뺏기 규칙,
 *        우선순위, 들리지 않을 때의 가상화, 파라미터(RTPC) → 볼륨 · 피치 · 로우패스 곡선입니다.
 * @details Wwise 의 Event + Random/Sequence/Blend Container + Playback Limit + RTPC, FMOD 의 Event + Multi Instrument + Parameter,
 *          언리얼의 Sound Cue(Random · Concatenator 노드) + Sound Concurrency 의 자리입니다. 게임 코드는 이벤트 이름 하나만 압니다
 *          (`AudioEngine::postEvent( "Footstep", emitter )`) — 무슨 클립을 어떻게 낼지는 이 데이터가 정합니다.
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
    /** @brief 이벤트가 클립을 고르는 방식입니다. */
    ENUM()
    enum class AudioContainerType : uint8
    {
        Random = 0, ///< 가중치로 하나(바로 앞의 것은 피함 — `_bAvoidRepeat`)
        Sequence,   ///< 적은 순서대로 하나씩, 끝나면 처음으로(이벤트마다 커서)
        Layer,      ///< 전부 함께(Wwise Blend · FMOD 멀티 레이어)
    };

    /** @brief 동시 재생 상한에 닿았을 때 새 재생을 어떻게 할지입니다. */
    ENUM()
    enum class AudioStealPolicy : uint8
    {
        Reject = 0, ///< 새 재생을 버린다
        Oldest,     ///< 가장 먼저 시작한 것을 멈추고 새것을 낸다
        Quietest,   ///< 가장 작게 들리는 것을 멈춘다
        Farthest,   ///< 리스너에서 가장 먼 것을 멈춘다
    };

    /** @brief 들리지 않거나 실제 보이스 상한 밖일 때 보이스를 어떻게 할지입니다. */
    ENUM()
    enum class AudioVirtualMode : uint8
    {
        Virtualize = 0, ///< 섞지 않고 시간만 흐르게 둔다 — 다시 들리면 그 자리에서 이어진다
        Stop,           ///< 멈춘다(짧은 원샷)
        KeepReal,       ///< 언제나 섞는다(음악 · 중요한 대사)
    };

    /** @brief 파라미터가 바꾸는 것입니다. 곡선의 y 단위가 정해집니다. */
    ENUM()
    enum class AudioParameterTarget : uint8
    {
        Volume = 0, ///< dB
        Pitch,      ///< 반음
        LowPass,    ///< Hz
    };
} // namespace sw

namespace sw
{
    /** @brief 컨테이너의 클립 하나입니다. */
    REFLECT()
    struct SW_API AudioClipEntry
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Resource path of the clip (wav, ogg)" )
        hashed_string _path{};
        PROPERTY( Min = 0.0, Tooltip = "Random container weight" )
        float32 _weight{ 1.0f };
        PROPERTY( Tooltip = "Per-clip volume trim", Meta = "Units=dB" )
        float32 _volumeDb{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 게임 파라미터 하나입니다(Wwise RTPC · FMOD parameter). 게임이 `setParameter` 로 넣고, 엔진이 `_seekSpeed` 로 따라갑니다.
     */
    REFLECT()
    struct SW_API AudioParameterDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Parameter name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Lowest value" )
        float32 _minValue{ 0.0f };
        PROPERTY( Tooltip = "Highest value" )
        float32 _maxValue{ 1.0f };
        PROPERTY( Tooltip = "Value before the game sets one" )
        float32 _defaultValue{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Units per second the value moves toward a new target; 0 jumps" )
        float32 _seekSpeed{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 파라미터 하나를 이벤트의 볼륨 · 피치 · 로우패스로 바꾸는 곡선입니다. */
    REFLECT()
    struct SW_API AudioParameterMapping
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Parameter name (declared in the library)" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "(parameter value, output) points; output is dB, semitones or Hz by target" )
        vector<AudioCurvePoint> _listPoint{};
        PROPERTY( Tooltip = "What the curve drives" )
        AudioParameterTarget _target{ AudioParameterTarget::Volume };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 사운드 이벤트 하나입니다.
     * @code
     *     <AudioEventDesc _name="Footstep" _bus="sfx" _attenuation="Small" _container="Random" _maxInstances="4" _steal="Oldest"
     *                     _priority="40" _cooldownSeconds="0.05" _volumeDbMin="-3" _pitchMin="-1" _pitchMax="1">
     *         <_listClip><AudioClipEntry _path="game/x/sounds/step_0.ogg" /><AudioClipEntry _path="game/x/sounds/step_1.ogg" /></_listClip>
     *     </AudioEventDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioEventDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Event name game code posts" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Bus the voices go to" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Attenuation preset; empty plays 2D (no spatialization)" )
        hashed_string _attenuation{};
        PROPERTY( Tooltip = "Clips of the container" )
        vector<AudioClipEntry> _listClip{};
        PROPERTY( Tooltip = "Parameter curves (RTPC)" )
        vector<AudioParameterMapping> _listParameterMap{};
        PROPERTY( Tooltip = "Random volume range low end", Meta = "Units=dB" )
        float32 _volumeDbMin{ 0.0f };
        PROPERTY( Tooltip = "Random volume range high end", Meta = "Units=dB" )
        float32 _volumeDbMax{ 0.0f };
        PROPERTY( Tooltip = "Random pitch range low end", Meta = "Units=semitones" )
        float32 _pitchMin{ 0.0f };
        PROPERTY( Tooltip = "Random pitch range high end", Meta = "Units=semitones" )
        float32 _pitchMax{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Posts closer together than this are dropped", Units = s )
        float32 _cooldownSeconds{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Fade in on start", Units = s )
        float32 _fadeInSeconds{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Fade out on stop and steal", Units = s )
        float32 _fadeOutSeconds{ 0.05f };
        PROPERTY( Tooltip = "Most instances alive at once; 0 is unlimited" )
        uint32 _maxInstances{ 0 };
        PROPERTY( Min = 0, Max = 100, Tooltip = "Higher keeps a real voice when voices run out" )
        int32 _priority{ 50 };
        PROPERTY( Tooltip = "Clip choice" )
        AudioContainerType _container{ AudioContainerType::Random };
        PROPERTY( Tooltip = "What to do when the instance limit is reached" )
        AudioStealPolicy _steal{ AudioStealPolicy::Oldest };
        PROPERTY( Tooltip = "What to do when inaudible or out of real voices" )
        AudioVirtualMode _virtual{ AudioVirtualMode::Virtualize };
        PROPERTY( Tooltip = "Loop until stopped" )
        bool _bLoop{ false };
        PROPERTY( Tooltip = "Random container avoids repeating the previous clip" )
        bool _bAvoidRepeat{ true };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 이벤트 라이브러리 파일 하나입니다 — 파라미터 · 감쇠 프리셋 · 이벤트. 게임마다 하나 이상(`game/<게임>/audio/<이름>.audioevents.xml`).
     * @details 이름이 비었거나 겹치거나, 이벤트가 모르는 파라미터를 쓰거나, 클립이 없거나, 범위가 거꾸로면 읽기 오류입니다. 버스 · 감쇠 이름은
     *          불러 올리는 엔진의 그래프와 대조합니다(`AudioEngine::loadEventLibrary`).
     */
    REFLECT()
    struct SW_API AudioEventLibrary
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Game parameters (RTPC)" )
        vector<AudioParameterDesc> _listParameter{};
        PROPERTY( Tooltip = "Attenuation presets local to this library" )
        vector<AudioAttenuationDesc> _listAttenuation{};
        PROPERTY( Tooltip = "Events" )
        vector<AudioEventDesc> _listEvent{};

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );
        /** @brief 파일 안에서만 정해지는 규칙을 검사합니다(이름 · 클립 · 범위 · 파라미터 이름 · 곡선 순서). */
        [[nodiscard]] bool validate( string_view sourceName ) const;

        /** @brief 이벤트입니다. 없으면 nullptr 입니다. */
        const AudioEventDesc* findEvent( const hashed_string& name ) const;
        /** @brief 파라미터입니다. 없으면 nullptr 입니다. */
        const AudioParameterDesc* findParameter( const hashed_string& name ) const;
    };
} // namespace sw
