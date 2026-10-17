/**
 * @file AnimationGraphAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 애니메이션 그래프 JSON 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/BlendSpace.h"

namespace sw
{
    class JSONValue;

    /**
     * @struct AnimationGraphBlendSpaceDef
     * @brief 블렌드 스페이스 상태의 데이터입니다(JSON 노드의 "blend_space"). 축 파라미터 이름과 표본(클립 이름, 위치)을 가집니다.
     * @details JSON 형식은 `{ "x": "Speed", "y": "Direction", "samples": [ { "clip": "Idle", "x": 0, "y": 0 }, … ] }` 이다. "y" 를 적지 않으면 1D 다.
     *          표본이 없거나, 클립 이름이나 "x" 파라미터가 비면 로드 오류다.
     */
    struct AnimationGraphBlendSpaceDef
    {
        BlendSpace    _space;      /**< 표본과 축 수입니다. 표본이 없으면 블렌드 스페이스 상태가 아닙니다. */
        hashed_string _parameterX; /**< x 축 파라미터 이름(`AnimationParameterSet`)입니다. */
        hashed_string _parameterY; /**< y 축 파라미터 이름입니다. 1D 면 비어 있습니다. */
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 노드입니다. */
    struct AnimationGraphNode
    {
        string _name;    /**< 노드 이름. 클립을 묶는 열쇠입니다. */
        int32  _id{ 0 }; /**< 그래프 안에서 유일한 노드 id 입니다. 1 이상이어야 합니다. */
        /** @brief 그래프 에디터에서의 노드 위치입니다. JSON 키는 "x", "y" 그대로라 파일 형식은 바뀌지 않습니다. */
        float2 _position{ 40.0f, 40.0f };
        /**
         * @brief 이 상태에 있는 동안 들어갈 동기 그룹 이름입니다(JSON "sync_group", 선택). 같은 그룹의 재생은 가장 무거운 쪽의 위상을 따릅니다.
         * @details 언리얼 애님 그래프의 Sync Group, 유니티 블렌드 트리의 동기화에 해당한다. 비어 있으면 컴포넌트가 정한 그룹을 그대로 쓴다.
         */
        hashed_string _syncGroup{};
        /** @brief 블렌드 스페이스 상태의 데이터입니다(JSON "blend_space", 선택). 표본이 없으면 이름이 곧 클립인 보통 상태입니다. */
        AnimationGraphBlendSpaceDef _blendSpace{};
        /**
         * @brief 이 상태의 반복 여부입니다(JSON "loop", 선택). -1 은 적지 않았다는 뜻입니다. 직접 재생은 호출한 쪽이 정하고, "끝나면 다음"으로 들어오면 반복하지 않습니다.
         */
        int8 _loopOverride{ -1 };

        /** @brief 블렌드 스페이스 상태인지 반환합니다. */
        bool hasBlendSpace() const { return _blendSpace._space.getSampleCount() > 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimationConditionOp
     * @brief 전이 조건의 비교입니다(JSON "op": ">", "<", ">=", "<=", "==", "!=", "trigger"). 모르는 표기는 로드 오류입니다.
     */
    enum class AnimationConditionOp : uint8
    {
        None = 0,     ///< 조건이 없습니다. 현재 상태가 반복 없이 끝나면 넘어갑니다("끝나면 다음").
        Greater,      ///< 파라미터가 값보다 크다.
        Less,         ///< 파라미터가 값보다 작다.
        GreaterEqual, ///< 파라미터가 값보다 크거나 같다.
        LessEqual,    ///< 파라미터가 값보다 작거나 같다.
        Equal,        ///< 파라미터가 값과 같다.
        NotEqual,     ///< 파라미터가 값과 다르다.
        Trigger,      ///< 트리거가 켜져 있습니다. 전이가 일어나면 끕니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimationBlendMode
     * @brief 전이가 포즈를 잇는 방식입니다(JSON 링크의 "blend_mode": "crossfade", "inertialization"). 모르는 표기는 로드 오류입니다.
     * @details 키를 적지 않은 링크는 `Crossfade` 다 — 이 필드의 데이터 기본값이며, 이 필드가 생기기 전 파일의 뜻과 같다.
     */
    enum class AnimationBlendMode : uint8
    {
        Crossfade = 0,   ///< 두 재생을 함께 샘플해 "blend" 길이 동안 섞습니다.
        Inertialization, ///< 새 재생 하나만 샘플하고, 끊긴 순간과의 차이를 "half_life" 반감기로 풀어 냅니다(`PoseInertializer`).
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 링크(상태 전이)입니다. */
    struct SW_API AnimationGraphLink
    {
        int32   _id{ 0 };               /**< 그래프 안에서 유일한 링크 id 입니다. 1 이상이어야 합니다. */
        int32   _fromNode{ 0 };         /**< 출발 노드 id 입니다. */
        int32   _toNode{ 0 };           /**< 도착 노드 id 입니다. */
        float32 _blendSeconds{ -1.0f }; /**< 크로스페이드 길이(JSON "blend", 선택). 음수면 플레이어 기본값입니다. */
        /** @brief inertialization 반감기(초, JSON "half_life")입니다. "blend_mode" 가 "inertialization" 이면 반드시 0 보다 큰 값을 적어야 합니다(없으면 로드 오류). */
        float32              _halfLifeSeconds{ 0.0f };
        hashed_string        _parameter;         /**< 조건 파라미터 이름(JSON "condition"."param")입니다. */
        float32              _threshold{ 0.0f }; /**< 조건 비교값(JSON "condition"."value")입니다. */
        AnimationConditionOp _op{ AnimationConditionOp::None };
        AnimationBlendMode   _blendMode{ AnimationBlendMode::Crossfade }; /**< 포즈를 잇는 방식(JSON "blend_mode", 선택)입니다. */

        /** @brief 조건이 지금 파라미터 값으로 참인지 봅니다. 조건이 없으면 false 입니다(끝나면 다음은 따로 봅니다). */
        bool isConditionMet( float32 parameterValue ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationGraphAsset
     * @brief BlendSpace 와 SpriteAnimator 가 노드 이름을 함께 쓰는 JSON 그래프입니다.
     */
    class SW_API AnimationGraphAsset
    {
    public:
        /** @brief 애니메이션 그래프 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".animgraph.json";

        /** @brief 빈 그래프를 만듭니다. */
        AnimationGraphAsset() = default;

        /** @brief JSON 파일을 읽습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief JSON 본문을 파싱합니다. */
        [[nodiscard]] bool parseJSON( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJSON() const;
        /** @brief 노드 이름 목록을 채웁니다. */
        void collectNodeNames( vector<string>& outListName ) const;
        /** @brief id 로 노드를 찾습니다. */
        const AnimationGraphNode* findNode( int32 nodeID ) const;
        /** @brief 이름으로 노드를 찾습니다. */
        const AnimationGraphNode* findNodeByName( string_view name ) const;
        /** @brief 진입 노드(들어오는 링크가 없는 첫 노드, 없으면 목록 앞)를 반환합니다. */
        const AnimationGraphNode* findEntryNode() const;
        /** @brief 그 노드에서 나가는 첫 링크의 대상 id 입니다. 없으면 0 입니다. */
        int32 findFirstOutgoingNodeID( int32 fromNodeID ) const;
        /** @brief 조건 없는 첫 나가는 링크("끝나면 다음")입니다. 없으면 nullptr 입니다. */
        const AnimationGraphLink* findFinishLink( int32 fromNodeID ) const;
        /** @brief 조건 표기를 읽습니다. 모르는 표기면 false 입니다. */
        [[nodiscard]] static bool parseConditionOp( string_view text, AnimationConditionOp& outOp );
        /** @brief 조건 표기를 씁니다. */
        static const utf8* getConditionOpText( AnimationConditionOp op );
        /** @brief 전이 방식 표기("crossfade", "inertialization")를 읽습니다. 모르는 표기면 false 입니다. */
        [[nodiscard]] static bool parseBlendMode( string_view text, AnimationBlendMode& outMode );
        /** @brief 전이 방식 표기를 씁니다. */
        static const utf8* getBlendModeText( AnimationBlendMode mode );

        vector<AnimationGraphNode> _listNode; /**< 노드 목록입니다. */
        vector<AnimationGraphLink> _listLink; /**< 링크 목록입니다. */

    private:
        /** @brief 이미 파싱된 JSON 루트에서 노드와 링크를 읽습니다. 모르는 조건 표기나 잘못된 블렌드 스페이스면 false 를 반환합니다. */
        [[nodiscard]] bool parseRoot( const JSONValue& root );
        /** @brief 노드의 "blend_space" 객체를 읽습니다. 잘못되었으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool parseBlendSpace( const JSONValue& blendSpaceJSON, AnimationGraphNode& inoutNode );
    };
} // namespace sw
