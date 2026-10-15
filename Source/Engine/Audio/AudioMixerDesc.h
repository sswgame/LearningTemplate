/**
 * @file AudioMixerDesc.h
 * @brief 믹서 그래프 데이터입니다(`*.audiomixer.xml`). 버스(서브믹스)의 계층, 볼륨, 음소거, 솔로, 센드를 담습니다.
 * @details 언리얼의 서브믹스, Wwise 의 버스 계층, FMOD 의 그룹 버스에 해당한다. 루트는 `master` 하나이고, 모든 버스는 부모를 이름으로 적는다.
 *          센드는 같은 신호를 다른 버스(리버브 같은 이펙트 리턴 버스)에도 보낸다. 부모나 센드가 고리를 만들면 읽을 때 오류다.
 *          엔진의 기본 그래프는 `engine/audio/default.audiomixer.xml` 이다. 사용자 설정의 버스 볼륨(`audio.busVolume`)은 그 이름으로 버스를 찾는다.
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
     * @brief 버스 하나에서 다른 버스로 보내는 센드입니다. Wwise 의 aux send, 언리얼의 submix send 에 해당합니다.
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
     * @brief 사이드체인 덕킹 하나입니다. 다른 버스의 출력이 임계값을 넘으면 이 버스를 눌러 줍니다(대사가 나올 때 음악 낮추기).
     *        Wwise 의 Auto-Ducking, 언리얼 서브믹스의 Ducking 에 해당합니다.
     * @details 눌림은 **눌리는 버스**(이 버스)에 적고, 신호는 지난 블록의 소스 버스 피크(페이더 뒤)입니다. 한 블록(약 5 ms) 늦지만 처리 순서에 제약이 없습니다.
     *          눌림 양은 임계값을 넘으면 `_depthDb`, 아니면 0 으로 정하고 `_attackSeconds` · `_releaseSeconds` 로 dB 축에서 따라갑니다.
     *          덕이 여럿이면 가장 깊은 것을 씁니다(합치지 않음).
     * @code
     *     <AudioBusDesc _name="music" _parent="master">
     *         <_listDuck><AudioDuckDesc _sourceBus="voice" _thresholdDb="-40" _depthDb="-9" /></_listDuck>
     *     </AudioBusDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioDuckDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bus whose output level ducks this bus (dialogue ducks music)" )
        hashed_string _sourceBus{};
        PROPERTY( Tooltip = "Source level above which the duck engages", Meta = "Units=dB" )
        float32 _thresholdDb{ -40.0f };
        PROPERTY( Max = 0.0, Tooltip = "Fader offset of this bus at full duck", Meta = "Units=dB" )
        float32 _depthDb{ -9.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time to reach the full duck", Units = s )
        float32 _attackSeconds{ 0.05f };
        PROPERTY( Min = 0.0, Tooltip = "Time to come back once the source falls quiet", Units = s )
        float32 _releaseSeconds{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 버스(서브믹스) 하나입니다. 입력은 이 버스로 보낸 보이스, 자식 버스, 센드의 합이며, 이펙트 체인을 지나 페이더를 거친 뒤 부모로 갑니다.
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
        PROPERTY( Tooltip = "Sidechain ducks: other buses whose output presses this bus down" )
        vector<AudioDuckDesc> _listDuck{};
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
     * @brief 믹스 스냅샷 하나입니다. Wwise 의 State/Mix, FMOD 의 Snapshot, 언리얼의 Sound Mix 에 해당합니다. 켜면 세기가 0 에서 1 로 올라오고, 끄면 1 에서 0 으로 내려갑니다.
     * @details 세기는 시간(`startSnapshot`, `stopSnapshot`)이나 게임 값(`setSnapshotIntensity`, 예: 리버브 존의 경계 블렌드)이 정한다.
     *          버스 오프셋은 세기 × dB 를 더하고, 센드와 이펙트 파라미터는 켠 순서대로 현재 값에서 스냅샷 값으로 세기만큼 보간한다.
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
     * @brief 믹서 그래프 하나입니다. 버스 목록과 보이스 상한을 담습니다.
     * @details 버스 이름은 겹치지 않아야 하고, `master` 가 유일한 루트여야 한다. 부모와 센드 대상은 테이블에 있는 이름이어야 하며, 부모와 센드가 고리를 만들면 안 된다.
     *          어긋나면 오류를 남기고 읽기가 실패한다.
     */
    REFLECT()
    struct SW_API AudioMixerDesc
    {
        REFLECT_BODY();
        /** @brief 믹서 그래프 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".audiomixer.xml";

        PROPERTY( Tooltip = "Buses (submixes); the master bus is the only root" )
        vector<AudioBusDesc> _listBus{};
        PROPERTY( Tooltip = "Distance attenuation presets every sound can pick by name" )
        vector<AudioAttenuationDesc> _listAttenuation{};
        PROPERTY( Tooltip = "How occlusion (0..1) turns into volume and low-pass" )
        AudioOcclusionDesc _occlusion{};
        PROPERTY( Tooltip = "Split screen: measure each sound from the loudest listener, or sum every listener by power" )
        AudioListenerMixMode _listenerMix{ AudioListenerMixMode::Loudest };
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
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );
        /** @brief 이름, 루트, 부모, 센드, 고리를 검사합니다. 어긋나면 오류를 남기고 false 를 반환합니다. */
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
         * @brief 처리 순서(보내는 버스가 받는 버스보다 앞)를 만듭니다. 부모와 센드를 간선으로 본 위상 정렬입니다.
         * @return 고리가 있으면 false 를 반환합니다.
         */
        [[nodiscard]] bool makeProcessingOrder( vector<uint32>& outListBusIndex ) const;

        /** @brief 감쇠 프리셋 목록을 검사합니다(이름, 거리, 곡선 점 순서). 이벤트 라이브러리도 같은 검사를 씁니다. */
        [[nodiscard]] static bool validateAttenuations( const vector<AudioAttenuationDesc>& listAttenuation, string_view sourceName );
        /** @brief 스냅샷이 가리키는 버스, 센드, 이펙트, 파라미터가 그래프에 있는지 검사합니다. */
        [[nodiscard]] bool validateSnapshots( string_view sourceName ) const;
        /** @brief 곡선 점이 입력 오름차순인지입니다. */
        static bool isCurveSorted( const vector<AudioCurvePoint>& listPoint );

        /** @brief 파일 없이 쓰는 기본 그래프입니다. master 아래에 music, sfx, voice, ambient, ui 버스가 있고 센드는 없습니다. */
        static AudioMixerDesc makeDefault();
    };
} // namespace sw
