/**
 * @file AudioGraph.h
 * @brief 소리 그래프의 실행 형식입니다. 노드 종류 표(`AudioGraphRegistry`), 읽은 데이터를 굳힌 그래프(`AudioGraph`, 바뀌지 않아 보이스들이 나눠 읽는다),
 *        보이스마다의 실행 상태(`AudioGraphInstance`)를 담습니다.
 * @details **오디오 스레드 실행기.** 인스턴스는 스트림 디코더(`IAudioStreamDecoder`)다. 보이스가 스트리밍 클립과 같은 창(`AudioStreamWindow`, 덩어리는 블록 길이)으로
 *          읽으므로 피치 리샘플 · 루프 · 페이드 · 가상 진행이 클립과 같은 길을 탄다. 블록마다 노드를 처리 순서대로 돌며 노드마다 모노 블록 하나를 채운다.
 *          - 할당은 인스턴스를 만들 때(보이스 시작, 스트리밍 클립이 디코더를 여는 것과 같은 자리) 한 번이고 렌더 중에는 없다.
 *          - 결정적이다. 잡음 · 그래뉼러의 난수는 인스턴스의 시드(엔진의 난수에서 뽑는다)로 정해지고, 처음으로 되감으면(루프) 같은 시드로 다시 시작한다.
 *          - 되감기(`seekToFrame`)는 상태를 비우고 시각만 옮긴다(따라 렌더하지 않는다). 가상 보이스가 다시 들릴 때 오실레이터 위상 · 필터가 새로 시작하는 대신 비용이 튀지 않는다.
 *          - 게임 파라미터 값은 블록마다 엔진이 넣는다(`setParameterValue`). 묶인 노드 값은 그 블록 처음에 곡선으로 바뀌고, 게인은 블록 안에서 램프한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioStream.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/DSP/AudioBiquad.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw
{
    struct AudioClipData;

    /** @brief 노드 값 하나의 서술입니다(종류 표의 한 칸). */
    struct AudioGraphParameterInfo
    {
        const utf8* _pName{ nullptr }; ///< 데이터가 쓰는 이름이다.
        float32     _defaultValue{ 0.0f };
        float32     _minValue{ 0.0f };
        float32     _maxValue{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 종류 하나입니다(종류 표의 한 줄). */
    struct AudioGraphNodeTypeInfo
    {
        const utf8*                    _pName{ nullptr };
        const AudioGraphParameterInfo* _pParameter{ nullptr };
        uint32                         _parameterCount{ 0 };
        bool                           _bSource{ false };    ///< 입력을 받지 않고 소리를 만든다(오실레이터 · 노이즈 · 그래뉼러).
        bool                           _bNeedsClip{ false }; ///< 클립(`AudioGraphNodeDesc::_clip`)이 필수다(그래뉼러).
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 종류 표입니다. 종류(`AudioGraphNodeType`)마다 한 줄이고, 값 이름 · 범위 · 기본값을 담습니다. */
    struct SW_API AudioGraphRegistry
    {
        /** @brief 노드 하나가 가질 수 있는 값의 최대 수입니다. */
        static constexpr uint32 kMaxParameterCount = 8;

        /** @brief 종류의 서술입니다. */
        static const AudioGraphNodeTypeInfo& getNodeType( AudioGraphNodeType type );
        /** @brief 종류의 값 이름의 번호입니다. 없으면 -1 입니다. */
        static int32 findParameterIndex( const AudioGraphNodeTypeInfo& typeInfo, const hashed_string& name );
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioGraph
     * @brief 검사를 통과한 그래프 데이터를 처리 순서 · 값 배열 · 파라미터 연결로 굳힌 것입니다. 만든 뒤 바뀌지 않습니다.
     */
    class SW_API AudioGraph
    {
    public:
        /** @brief 굳힌 노드 하나입니다(처리 순서). */
        struct Node
        {
            float32            _arrParameter[AudioGraphRegistry::kMaxParameterCount]{}; ///< 상수 값(종류 표 순서, 범위로 묶었다)이다.
            vector<uint32>     _listInput{};                                            ///< 입력 노드의 번호(처리 순서)다.
            vector<float32>    _listSourceSample{};                                     ///< 그래뉼러 소스의 모노 샘플이다(다른 종류는 비어 있다).
            float64            _sourceRate{ 1.0 };                                      ///< 그래뉼러 소스의 샘플레이트 / 출력 샘플레이트다.
            AudioGraphNodeType _type{ AudioGraphNodeType::Sine };
        };

        /** @brief 게임 파라미터가 움직이는 노드 값 하나입니다. */
        struct Binding
        {
            vector<AudioCurvePoint> _listPoint{};         ///< 게임 파라미터 값 → 노드 값 곡선이다.
            uint32                  _nodeIndex{ 0 };      ///< 노드 번호(처리 순서)다.
            uint32                  _parameterIndex{ 0 }; ///< 노드의 값 번호(종류 표 순서)다.
            uint32                  _inputIndex{ 0 };     ///< `getParameterNames` 의 번호다.
        };

        /**
         * @brief 검사를 통과한 @p def 를 굳힙니다.
         * @param listNodeClip 데이터 노드 순서대로의 그래뉼러 소스 클립입니다(그래뉼러가 아닌 노드는 nullptr). 메모리에 푼 클립이어야 합니다.
         * @return 검사 실패 · 클립이 없거나 스트리밍 클립이면 오류를 남기고 nullptr 입니다.
         */
        static shared_ptr<const AudioGraph> create( const AudioGraphDef& def, const vector<shared_ptr<const AudioClipData>>& listNodeClip, string_view sourceName );

        /** @brief 노드입니다(처리 순서). */
        const vector<Node>& getNodes() const { return _listNode; }
        /** @brief 출력 노드의 번호(처리 순서)입니다. */
        uint32 getOutputIndex() const { return _outputIndex; }
        /** @brief 소리 하나의 길이(48 kHz 프레임)입니다. */
        uint32 getFrameCount() const { return _frameCount; }
        /** @brief 게임 파라미터 연결입니다. */
        const vector<Binding>& getBindings() const { return _listBinding; }
        /** @brief 그래프가 읽는 게임 파라미터 이름입니다(겹치지 않음). 엔진이 블록마다 이 순서로 값을 넣습니다. */
        const vector<hashed_string>& getParameterNames() const { return _listParameterName; }

    private:
        vector<Node>          _listNode{};
        vector<Binding>       _listBinding{};
        vector<hashed_string> _listParameterName{};
        uint32                _outputIndex{ 0 };
        uint32                _frameCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioGraphInstance
     * @brief 보이스 하나가 재생하는 그래프의 실행 상태입니다. 오디오 스레드 하나만 부릅니다.
     */
    class SW_API AudioGraphInstance final : public IAudioStreamDecoder
    {
    public:
        /** @brief 그래뉼러 노드 하나가 한꺼번에 내는 알갱이의 최대 수입니다. 넘는 알갱이는 건너뜁니다. */
        static constexpr uint32 kMaxGrainCount = 16;

        /** @brief @p pGraph 를 @p randomSeed 로 처음부터 재생할 상태를 잡습니다(노드 수 × 블록 길이의 버퍼). */
        AudioGraphInstance( shared_ptr<const AudioGraph> pGraph, uint64 randomSeed );

        /** @brief 다음 @p frameCount 프레임(모노)을 렌더합니다. 그래프 길이의 끝에서 멈추고 실제로 낸 프레임 수를 돌려줍니다. */
        uint32 readFrames( float32* pOut, uint32 frameCount ) override;
        /** @brief 상태를 비우고 다음 렌더 시각을 @p frame 으로 옮깁니다(위 파일 설명). 길이 밖이면 false 입니다. */
        [[nodiscard]] bool seekToFrame( uint32 frame ) override;

        /** @brief 게임 파라미터 @p inputIndex(`AudioGraph::getParameterNames` 의 번호)의 이번 블록 값을 넣습니다. */
        void setParameterValue( uint32 inputIndex, float32 value );
        /** @brief 그래프입니다. */
        const AudioGraph& getGraph() const { return *_pGraph; }

    private:
        /** @brief 알갱이 하나입니다. */
        struct Grain
        {
            float64 _position{ 0.0 };   ///< 소스 안의 위치(프레임, 소수)다.
            float64 _step{ 1.0 };       ///< 출력 프레임마다 나아가는 소스 프레임이다(피치).
            uint32  _ageFrames{ 0 };    ///< 시작부터 지난 출력 프레임이다.
            uint32  _lengthFrames{ 0 }; ///< 알갱이 길이(출력 프레임)다. 0 이면 비어 있는 칸이다.
        };

        /** @brief 노드 하나의 실행 상태입니다. */
        struct NodeState
        {
            AudioBiquadFilter _filter{};
            Grain             _arrGrain[kMaxGrainCount]{};
            float64           _phase{ 0.0 };             ///< 오실레이터 위상([0, 1))이다.
            float64           _framesToNextGrain{ 0.0 }; ///< 다음 알갱이까지 남은 출력 프레임이다.
            float32           _gain{ 0.0f };             ///< 지난 블록 끝의 게인(램프 시작점)이다.
            float32           _filterHz{ 0.0f };         ///< 지금 계수를 구한 주파수다(0 = 아직 없음).
            float32           _filterQ{ 0.0f };          ///< 지금 계수를 구한 Q 다.
            bool              _bFirstBlock{ true };      ///< 처음 처리하는 블록이다. 게인을 램프하지 않는다.
        };

        /** @brief 처리 순서대로 노드를 돌며 @p frameCount(블록 길이 이하) 프레임을 렌더해 @p pOut 에 씁니다. */
        void renderBlock( float32* pOut, uint32 frameCount );
        /** @brief 노드 @p nodeIndex 의 입력 합을 @p pOut 에 씁니다(입력이 없으면 0). */
        void sumInputs( uint32 nodeIndex, float32* pOut, uint32 frameCount ) const;
        /** @brief 그래뉼러 노드 하나를 렌더합니다. */
        void renderGranular( uint32 nodeIndex, const float32* pParameter, float32* pOut, uint32 frameCount );
        /** @brief 이번 블록 값을 정합니다 — 노드 상수에 게임 파라미터 곡선을 덮고 종류 표의 범위로 묶습니다. */
        void updateBlockParameters();
        /** @brief 오실레이터 노드(사인 · 톱니 · 사각 · 삼각) 하나를 렌더합니다. */
        void renderOscillator( const AudioGraph::Node& node, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount );
        /** @brief 엔벨로프 노드 하나를 렌더합니다 — 입력 합에 시간에 따른 곡선을 곱합니다. */
        void renderEnvelope( uint32 nodeIndex, const float32* pParameter, float32* pOut, uint32 frameCount );
        /** @brief 필터 노드(저역 · 고역 · 대역) 하나를 렌더합니다. 주파수나 Q 가 바뀐 블록에서만 계수를 다시 만듭니다. */
        void renderFilter( uint32 nodeIndex, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount );
        /** @brief 게인 노드 하나를 렌더합니다 — 블록 안에서 이전 게인에서 목표 게인으로 선형으로 옮깁니다. */
        void renderGain( uint32 nodeIndex, NodeState& inoutState, const float32* pParameter, float32* pOut, uint32 frameCount );
        /** @brief 노드 상태와 난수를 처음으로 되돌립니다. */
        void resetState();
        /** @brief 노드 @p nodeIndex 의 이번 블록 값 배열입니다. */
        float32* getBlockParameters( uint32 nodeIndex ) { return _listBlockParameter.data() + static_cast<size_t>( nodeIndex ) * AudioGraphRegistry::kMaxParameterCount; }
        /** @brief 노드 @p nodeIndex 의 출력 블록입니다. */
        float32* getNodeBuffer( uint32 nodeIndex ) { return _listBuffer.data() + static_cast<size_t>( nodeIndex ) * audio::kBlockFrameCount; }

    private:
        shared_ptr<const AudioGraph> _pGraph;             /**< 재생하는 그래프입니다. */
        vector<float32>              _listBuffer;         /**< 노드마다 블록 하나의 출력입니다(노드 수 × 블록 길이). */
        vector<float32>              _listBlockParameter; /**< 노드마다 이번 블록 값입니다(노드 수 × `kMaxParameterCount`). */
        vector<float32>              _listParameterValue; /**< 게임 파라미터마다 엔진이 넣은 값입니다. */
        vector<NodeState>            _listState;          /**< 노드마다의 실행 상태입니다. */
        AudioRandom                  _random;             /**< 잡음 · 그래뉼러가 쓰는 난수입니다. */
        uint64                       _randomSeed;         /**< 처음으로 되감을 때 다시 쓰는 시드입니다. */
        uint32                       _frame;              /**< 다음에 렌더할 프레임(그래프 시각)입니다. */
    };
} // namespace sw
