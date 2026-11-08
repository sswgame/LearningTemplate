/**
 * @file AudioGraphDef.h
 * @brief 절차적 · 그래뉼러 소리 그래프 데이터입니다(`*.audiograph.xml`). 노드(오실레이터 · 노이즈 · 그래뉼러 · 엔벨로프 · 필터 · 게인), 노드 사이 연결,
 *        게임 파라미터(RTPC)가 움직이는 노드 값을 담습니다.
 * @details 언리얼 MetaSounds 의 소스 그래프, Wwise SoundSeed Grain · Synth One, FMOD 의 그래뉼러 · 오실레이터 인스트루먼트를 한 모양으로 줄인 것이다.
 *          - **이벤트가 고르는 길.** 이벤트 클립(`AudioClipEntry::_path`)에 이 파일을 적으면 클립 대신 그래프가 소리를 낸다. 컨테이너 · 피치 · 감쇠 · 가상화 ·
 *            버스는 클립과 같다(클립 캐시가 그래프를 읽어 `AudioClipData::_pGraph` 를 단 소리로 만든다). 출력은 모노 48 kHz 이고 3D 패닝 · HRTF 가 그대로 걸린다.
 *          - **노드 형식.** 노드는 이름과 종류(`AudioGraphNodeKind`)를 가지고, 신호 입력은 앞 노드 이름의 목록(`_listInput`, 합한다)이다. 값은 이펙트처럼 이름과 값의
 *            목록(`_listParameter`)이며 종류마다 받는 이름 · 범위 · 기본값은 코드의 종류 표(`AudioGraphRegistry`)에 있다. 모르는 이름 · 고리 · 없는 노드는 읽기 오류다.
 *          - **파라미터 갱신 경로.** 값 하나에 게임 파라미터(`_parameter`)와 곡선(`_listPoint`)을 달면 엔진이 블록(5.3 ms)마다 그 에미터에서 본 파라미터 값을 곡선으로 바꿔
 *            넣는다(`AudioEngine::updateVoiceTargets`). 이벤트의 RTPC 곡선과 같은 모양이고, 늦음은 한 블록 안이다. 엔진이 모르는 파라미터를 읽는 그래프는 시작할 때 오류로 멈춘다.
 *          - **길이.** `_durationSeconds` 가 소리 하나의 길이(필수)이고 엔벨로프의 릴리스가 그 끝에 맞춰진다. 반복 이벤트는 끝에서 그래프를 처음부터 다시 시작한다.
 *          - **에디터 편집.** 소리 그래프 창(`AudioGraphPanel`)이 이 형식 위에서 캔버스(노드 · 연결)와 속성 그리드를 함께 고친다. 캔버스 위치는
 *            `_editorPosition` 에 실리고 소리는 읽지 않는다(MetaSounds 도 편집기 좌표를 에셋에 둔다).
 * @code
 *     <AudioGraphDef _output="Amp" _durationSeconds="3">
 *         <_listNode>
 *             <AudioGraphNodeDef _name="Grains" _type="Granular" _clip="game/x/sounds/rain_loop.ogg">
 *                 <_listParameter>
 *                     <AudioGraphParameterDef _name="grainsPerSecond" _value="30" _parameter="RainIntensity">
 *                         <_listPoint><AudioCurvePoint _x="0" _y="5" /><AudioCurvePoint _x="1" _y="80" /></_listPoint>
 *                     </AudioGraphParameterDef>
 *                 </_listParameter>
 *             </AudioGraphNodeDef>
 *             <AudioGraphNodeDef _name="Tone" _type="LowPass"><_listInput><item>Grains</item></_listInput></AudioGraphNodeDef>
 *             <AudioGraphNodeDef _name="Amp" _type="Envelope"><_listInput><item>Tone</item></_listInput></AudioGraphNodeDef>
 *         </_listNode>
 *     </AudioGraphDef>
 * @endcode
 */
#pragma once
#include "Core/Container/DependencyGraph.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/GraphCommentBox.h"
#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    struct ResourceContext;

    /** @brief 그래프 노드의 종류입니다. 종류마다 받는 값은 `AudioGraphRegistry` 의 종류 표가 정합니다. */
    ENUM()
    enum class AudioGraphNodeKind : uint8
    {
        Sine = 0, ///< 사인 오실레이터(frequencyHz, amplitude). 입력이 없다.
        Saw,      ///< 톱니 오실레이터(PolyBLEP 로 앨리어싱을 줄인다).
        Square,   ///< 사각 오실레이터(PolyBLEP).
        Triangle, ///< 삼각 오실레이터.
        Noise,    ///< 흰 잡음(amplitude). 입력이 없다.
        Granular, ///< 클립(`_clip`)에서 짧은 알갱이를 뽑아 겹쳐 낸다(위치 · 길이 · 밀도 · 피치와 그 흔들림). 입력이 없다.
        Envelope, ///< 입력의 합에 ADSR 을 곱한다. 릴리스는 그래프 길이의 끝에 맞춘다.
        LowPass,  ///< 입력의 합을 로우패스로 거른다(frequencyHz, q).
        HighPass, ///< 하이패스.
        BandPass, ///< 밴드패스(중심 0 dB).
        Gain,     ///< 입력의 합에 볼륨(volumeDb)을 곱한다. 여러 입력을 섞는 노드이기도 하다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 노드 값 하나입니다. `_parameter` 가 비면 `_value` 그대로이고, 있으면 그 게임 파라미터를 곡선(`_listPoint`)으로 바꾼 값입니다(종류 표의 범위로 묶는다).
     */
    REFLECT()
    struct SW_API AudioGraphParameterDef
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Tooltip = "Value name the node type declares (frequencyHz, amplitude, grainSeconds ...)" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Game parameter (RTPC) that drives the value through the curve; empty keeps the constant value" )
        hashed_string _parameter{};
        PROPERTY( Tooltip = "(game parameter value, node value) points; required when a game parameter drives the value" )
        vector<AudioCurvePoint> _listPoint{};
        PROPERTY( Tooltip = "Constant value; clamped to the value range" )
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 그래프의 노드 하나입니다. */
    REFLECT()
    struct SW_API AudioGraphNodeDef
    {
        REFLECT_BODY();

        PROPERTY( InheritKey, Tooltip = "Node name other nodes and the graph output refer to" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Earlier nodes whose signals are summed into this node (not for oscillators, noise and granular)" )
        vector<hashed_string> _listInput{};
        PROPERTY( Tooltip = "Node values; values left out keep the type's default" )
        vector<AudioGraphParameterDef> _listParameter{};
        PROPERTY( Tooltip = "Source clip of a Granular node (in memory: shorter than the streaming threshold, not a graph)" )
        DataPath _clip;
        PROPERTY( Tooltip = "Position of the node on the editor canvas; the sound does not read it" )
        float2 _editorPosition{ 0.0f, 0.0f };
        PROPERTY( Tooltip = "Node type" )
        AudioGraphNodeKind _type{ AudioGraphNodeKind::Sine };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 소리 그래프 파일 하나입니다(`*.audiograph.xml`).
     * @details 검사(`validate`)는 이름 · 출력 · 길이 · 연결(없는 노드, 자기 연결, 고리) · 종류마다의 입력 규칙 · 값 이름 · 곡선 · 그래뉼러의 클립을 본다.
     *          노드는 `kMaxNodeCount` 개까지다(보이스마다 노드 수 × 블록 길이의 버퍼를 잡는다).
     */
    REFLECT()
    struct SW_API AudioGraphDef
    {
        REFLECT_BODY();
        /** @brief 소리 그래프 파일의 접미사입니다. 클립 캐시와 에디터 종류 등록(`Editor/AssetActions`)이 이 값을 씁니다. */
        static constexpr string_view kExtension = ".audiograph.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 소리 그래프 에셋 종류입니다(등록은 `AudioGraphDef.cpp` 의 `SW_ASSET_TYPE`). 소리 파일과 같이 전용 서버 패키지가 빼는 묶음 `Audio` 다 — 서버는 그래프를 읽지 않는다. */
        static constexpr AssetTypeDef kAssetType{ "AudioGraph", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, "Audio" };
        /** @brief 노드 수의 상한입니다. */
        static constexpr uint32 kMaxNodeCount = 32;
        /** @brief 길이의 상한(초)입니다. 프레임 수가 32 비트에 들어가야 한다. */
        static constexpr float32 kMaxDurationSeconds = 3600.0f;

        PROPERTY( Tooltip = "Nodes; inputs must name nodes of this graph and must not form a cycle" )
        vector<AudioGraphNodeDef> _listNode{};
        PROPERTY( Tooltip = "Node whose signal is the sound (mono)" )
        hashed_string _output{};
        PROPERTY( Min = 0.0, Tooltip = "Length of one sound; envelopes release at its end and looping events restart the graph", Units = s )
        float32 _durationSeconds{ 0.0f };
        /** @brief 그래프 에디터의 주석 상자입니다(소리는 읽지 않는 배치 데이터 — `_editorPosition` 과 같다). */
        PROPERTY( Tooltip = "Comment boxes of the graph editor; the sound does not read them" )
        vector<GraphCommentBox> _listComment{};
        string                  _baseID{}; ///< 읽은 글의 `_base`(PROPERTY 가 아니다 — 읽기 앞 단계가 지우는 표시다). `saveToAsset` 이 부모와 다른 칸만 쓴다

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 문자열을 읽고 검사합니다(테스트용). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = "<text>" );

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다. `loadFromAsset` 은 리소스 id 를 실은 문맥(`withAsset`)을, 글 입구는 id 없는 문맥을 넘깁니다(덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다). */
        [[nodiscard]] bool loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context );

    public:
        /** @brief 파일 안에서만 정해지는 규칙을 검사합니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceName ) const;
        /** @brief 그래프를 읽기와 같은 XML 형식으로 씁니다(`loadFromXMLText` 가 다시 읽는다). */
        string saveToXMLText() const;
        /**
         * @brief 검사를 통과한 그래프를 리소스 경로의 파일에 씁니다. 에디터의 소리 그래프 창이 저장하는 길이고, 저장한 파일은 핫 리로드가 클립 캐시에 다시 굳힌다.
         * @details 부모(`_base`)를 가리키던 글에서 읽었으면 부모와 다른 칸만 쓴다(`ReflectedXMLFile::saveDescAsChild`) — 변형을 모든 칸을 적은 파일로 끊지 않는다.
         * @return 검사 실패 · 부모를 읽지 못함 · 쓰기 실패면 오류를 남기고 false 입니다(읽을 수 없는 파일을 남기지 않는다).
         */
        [[nodiscard]] bool saveToAsset( const AbsolutePath& absPath ) const;
        /**
         * @brief 처리 순서(입력이 먼저)를 노드 번호로 만듭니다. 입력을 간선으로 본 위상 정렬이고 준비된 노드는 데이터 차례로 꺼낸다(`DependencyGraph::sortInDataOrder` —
         *        잡음 · 그래뉼러가 한 난수를 차례로 쓰므로 같은 파일이면 같은 소리다).
         * @return 없는 노드를 가리키거나 고리가 있으면 false 입니다.
         */
        [[nodiscard]] bool makeProcessingOrder( vector<uint32>& outListNodeIndex ) const;
        /** @brief 입력을 간선으로 본 의존 그래프입니다(노드 번호는 `_listNode` 와 같다). 없는 노드를 가리키는 입력은 빠진다 — 검사(`validate`)가 따로 잡는다. */
        [[nodiscard]] DependencyGraph makeDependencyGraph() const;
        /** @brief 이름의 노드 번호입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<uint32> findNodeIndex( const hashed_string& name ) const;
    };
} // namespace sw
