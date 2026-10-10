/**
 * @file AudioMixerDesc.h
 * @brief 믹서 그래프 데이터 — 버스(서브믹스)의 계층 · 볼륨 · 음소거 · 솔로 · 센드입니다(`*.audiomixer.xml`).
 * @details 언리얼 서브믹스 · Wwise 버스 계층 · FMOD 그룹 버스의 자리입니다. 루트는 `master` 하나이고, 모든 버스는 부모를 이름으로 적습니다.
 *          센드는 같은 신호를 다른 버스(리버브 같은 이펙트 리턴 버스)에도 보냅니다. 부모 · 센드가 고리를 만들면 읽을 때 오류입니다.
 *          엔진의 기본 그래프는 `engine/audio/default.audiomixer.xml` — 사용자 설정의 버스 볼륨(`audio.busVolume`)은 그 이름으로 찾아갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 버스 하나에서 다른 버스로 보내는 센드입니다(Wwise aux send · 언리얼 submix send).
     * @code
     *     <AudioSendDesc _bus="reverb" _levelDb="-12" />
     * @endcode
     */
    REFLECT()
    struct SW_API AudioSendDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus that receives the send" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Send level", Meta = "Units=dB" )
        float32 _levelDb{ 0.0f };
        PROPERTY( Tooltip = "Send before the bus fader (volume, mute) instead of after it" )
        bool _bPreFader{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 이펙트 파라미터 값 하나입니다(이름은 이펙트 종류가 서술한 이름). */
    REFLECT()
    struct SW_API AudioEffectParameterDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Parameter name the effect type declares (frequencyHz, thresholdDb, roomSize ...)" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Value; clamped to the parameter range" )
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 버스의 이펙트 하나입니다. 종류는 `AudioEffectRegistry` 의 이름이고, 적지 않은 파라미터는 종류의 기본값입니다.
     * @code
     *     <AudioEffectDesc _type="Compressor" _name="Glue">
     *         <_listParameter><AudioEffectParameterDesc _name="thresholdDb" _value="-18" /></_listParameter>
     *     </AudioEffectDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioEffectDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Effect type: LowPass, HighPass, BandPass, Peaking, LowShelf, HighShelf, Compressor, Limiter, Reverb, Delay" )
        hashed_string _type{};
        PROPERTY( Tooltip = "Name snapshots use to reach this effect; empty means the type name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Parameter values" )
        vector<AudioEffectParameterDesc> _listParameter{};

        /** @brief 스냅샷이 찾는 이름입니다(비었으면 종류 이름). */
        const hashed_string& getEffectName() const { return _name.empty() ? _type : _name; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 버스(서브믹스) 하나입니다. 입력은 이 버스로 보낸 보이스 · 자식 버스 · 센드의 합이고, 이펙트 체인을 지나 페이더 뒤 부모로 갑니다.
     * @code
     *     <AudioBusDesc _name="sfx" _parent="master" _volumeDb="0">
     *         <_listSend><AudioSendDesc _bus="reverb" _levelDb="-12" /></_listSend>
     *     </AudioBusDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioBusDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus name (sounds and settings pick the bus by it)" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Parent bus; only the master bus has none" )
        hashed_string _parent{};
        PROPERTY( Tooltip = "Sends to other buses (effect returns)" )
        vector<AudioSendDesc> _listSend{};
        PROPERTY( Tooltip = "Insert effects, in order, before the sends and the fader" )
        vector<AudioEffectDesc> _listEffect{};
        PROPERTY( Tooltip = "Bus fader", Meta = "Units=dB" )
        float32 _volumeDb{ 0.0f };
        PROPERTY( Tooltip = "Starts muted" )
        bool _bMuted{ false };
        PROPERTY( Tooltip = "Starts soloed (every bus that is not on a soloed path goes silent)" )
        bool _bSolo{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 스냅샷이 버스 페이더에 더하는 오프셋입니다. */
    REFLECT()
    struct SW_API AudioSnapshotBusDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus name" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Fader offset at full intensity (intensities add in dB)", Meta = "Units=dB" )
        float32 _volumeDb{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 스냅샷이 바꾸는 센드 레벨입니다. */
    REFLECT()
    struct SW_API AudioSnapshotSendDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Sending bus" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Receiving bus (the send must exist in the graph)" )
        hashed_string _target{};
        PROPERTY( Tooltip = "Send level at full intensity", Meta = "Units=dB" )
        float32 _levelDb{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 스냅샷이 바꾸는 이펙트 파라미터입니다. */
    REFLECT()
    struct SW_API AudioSnapshotEffectDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus that holds the effect" )
        hashed_string _bus{};
        PROPERTY( Tooltip = "Effect name on that bus (its _name, or its type when unnamed)" )
        hashed_string _effect{};
        PROPERTY( Tooltip = "Parameter name" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "Value at full intensity (frequencies blend on a log scale)" )
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 믹스 스냅샷 하나입니다(Wwise State/Mix · FMOD Snapshot · 언리얼 Sound Mix). 켜면 세기 0 → 1 로 들어오고, 끄면 1 → 0 으로 빠집니다.
     * @details 세기는 시간(`startSnapshot` · `stopSnapshot`)이나 게임 값(`setSnapshotIntensity` — 리버브 존의 경계 블렌드)이 정합니다. 버스 오프셋은 세기 × dB 를
     *          더하고, 센드 · 이펙트 파라미터는 켠 순서대로 지금 값 → 스냅샷 값을 세기만큼 보간합니다.
     * @code
     *     <AudioSnapshotDesc _name="Underwater" _fadeInSeconds="0.3" _fadeOutSeconds="0.6">
     *         <_listEffectParameter><AudioSnapshotEffectDesc _bus="master" _effect="Muffle" _parameter="frequencyHz" _value="600" /></_listEffectParameter>
     *     </AudioSnapshotDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioSnapshotDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Snapshot name" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Bus fader offsets" )
        vector<AudioSnapshotBusDesc> _listBusVolume{};
        PROPERTY( Tooltip = "Send levels" )
        vector<AudioSnapshotSendDesc> _listSend{};
        PROPERTY( Tooltip = "Effect parameters" )
        vector<AudioSnapshotEffectDesc> _listEffectParameter{};
        PROPERTY( Min = 0.0, Tooltip = "Time to reach full intensity", Units = s )
        float32 _fadeInSeconds{ 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Time to fall back to zero", Units = s )
        float32 _fadeOutSeconds{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 믹서 그래프 하나입니다. 버스 목록과 보이스 상한입니다.
     * @details 버스 이름은 겹치지 않고, `master` 가 유일한 루트이며, 부모 · 센드 대상은 표에 있는 이름이고, 부모 · 센드가 고리를 만들지 않아야 합니다.
     *          어긋나면 오류를 남기고 읽기가 실패합니다.
     */
    REFLECT()
    struct SW_API AudioMixerDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Buses (submixes); the master bus is the only root" )
        vector<AudioBusDesc> _listBus{};
        PROPERTY( Tooltip = "Distance attenuation presets every sound can pick by name" )
        vector<AudioAttenuationDesc> _listAttenuation{};
        PROPERTY( Tooltip = "How occlusion (0..1) turns into volume and low-pass" )
        AudioOcclusionDesc _occlusion{};
        PROPERTY( Tooltip = "Mix snapshots (bus, send and effect parameter sets blended over time)" )
        vector<AudioSnapshotDesc> _listSnapshot{};
        PROPERTY( Min = 1, Tooltip = "Most voices alive at once (real and virtual)" )
        uint32 _maxVoiceCount{ 256 };
        PROPERTY( Min = 1, Tooltip = "Most voices mixed at once; the quietest lowest-priority rest go virtual" )
        uint32 _maxRealVoiceCount{ 64 };
        PROPERTY( Tooltip = "A voice quieter than this is inaudible and goes virtual", Meta = "Units=dB" )
        float32 _inaudibleDb{ -60.0f };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다(시험용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );
        /** @brief 이름 · 루트 · 부모 · 센드 · 고리를 검사합니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceName ) const;

        /** @brief 버스 번호입니다. 없으면 -1 입니다. */
        int32 findBusIndex( const hashed_string& name ) const;
        /** @brief 스냅샷입니다. 없으면 nullptr 입니다. */
        const AudioSnapshotDesc* findSnapshot( const hashed_string& name ) const;
        /** @brief 버스의 이펙트 서술입니다. 없으면 nullptr 입니다. */
        const AudioEffectDesc* findEffect( uint32 busIndex, const hashed_string& effectName ) const;
        /** @brief 감쇠 프리셋입니다. 없으면 nullptr 입니다. */
        const AudioAttenuationDesc* findAttenuation( const hashed_string& name ) const;
        /**
         * @brief 처리 순서(보내는 버스가 받는 버스보다 앞)를 만듭니다 — 부모와 센드를 간선으로 본 위상 정렬입니다.
         * @return 고리가 있으면 false 입니다.
         */
        [[nodiscard]] bool makeProcessingOrder( vector<uint32>& outListBusIndex ) const;

        /** @brief 감쇠 프리셋 목록을 검사합니다(이름 · 거리 · 곡선 점 순서). 이벤트 라이브러리도 같은 검사를 씁니다. */
        [[nodiscard]] static bool validateAttenuations( const vector<AudioAttenuationDesc>& listAttenuation, string_view sourceName );
        /** @brief 스냅샷이 가리키는 버스 · 센드 · 이펙트 · 파라미터가 그래프에 있는지 검사합니다. */
        [[nodiscard]] bool validateSnapshots( string_view sourceName ) const;
        /** @brief 곡선 점이 입력 오름차순인지입니다. */
        static bool isCurveSorted( const vector<AudioCurvePoint>& listPoint );

        /** @brief 파일 없이 쓰는 기본 그래프입니다 — master 아래 music · sfx · voice · ambient · ui, 센드 없음. */
        static AudioMixerDesc makeDefault();
    };
} // namespace sw
