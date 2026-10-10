/**
 * @file AnimGraphAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 애니메이션 그래프 JSON 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class JSONValue;

    /** @brief 애니메이션 그래프 노드입니다. */
    struct AnimGraphNode
    {
        string _name;    /**< 노드 이름. 클립을 묶는 열쇠입니다. */
        int32  _id{ 0 }; /**< 그래프 안에서 유일한 노드 id. 1 이상이어야 합니다. */
        /** @brief 그래프 에디터에서의 노드 위치입니다. JSON 키는 그대로 "x"/"y" 라 파일 형식은 바뀌지 않습니다. */
        float2 _position{ 40.0f, 40.0f };
        /**
         * @brief 이 상태의 반복 여부입니다(JSON "loop", 선택). -1 은 적지 않음 — 직접 재생은 부른 쪽이, "끝나면 다음" 으로 들어오면 반복하지 않습니다.
         */
        int8 _loopOverride{ -1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimConditionOp
     * @brief 전이 조건의 비교입니다(JSON "op": ">" · "<" · ">=" · "<=" · "==" · "!=" · "trigger"). 모르는 표기는 로드 오류입니다.
     */
    enum class AnimConditionOp : uint8
    {
        None = 0,     ///< 조건 없음 — 지금 상태가 반복 없이 끝나면 넘어갑니다("끝나면 다음").
        Greater,      ///< 파라미터 > 값
        Less,         ///< 파라미터 < 값
        GreaterEqual, ///< 파라미터 >= 값
        LessEqual,    ///< 파라미터 <= 값
        Equal,        ///< 파라미터 == 값
        NotEqual,     ///< 파라미터 != 값
        Trigger,      ///< 트리거가 켜져 있음 — 전이가 일어나면 끕니다
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 링크(상태 전이)입니다. */
    struct SW_API AnimGraphLink
    {
        int32           _id{ 0 };               /**< 그래프 안에서 유일한 링크 id. 1 이상이어야 합니다. */
        int32           _fromNode{ 0 };         /**< 출발 노드 id 입니다. */
        int32           _toNode{ 0 };           /**< 도착 노드 id 입니다. */
        float32         _blendSeconds{ -1.0f }; /**< 크로스페이드 길이(JSON "blend", 선택). 음수면 플레이어 기본값입니다. */
        hashed_string   _parameter;             /**< 조건 파라미터 이름(JSON "condition"."param")입니다. */
        float32         _threshold{ 0.0f };     /**< 조건 비교값(JSON "condition"."value")입니다. */
        AnimConditionOp _op{ AnimConditionOp::None };

        /** @brief 조건이 지금 파라미터 값으로 참인지 봅니다. 조건이 없으면 false 입니다(끝나면 다음은 따로 봅니다). */
        bool isConditionMet( float32 parameterValue ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimGraphAsset
     * @brief BlendSpace · SpriteAnimator 와 노드 이름을 함께 쓰는 JSON 그래프입니다.
     */
    class SW_API AnimGraphAsset
    {
    public:
        /** @brief 빈 그래프를 만듭니다. */
        AnimGraphAsset() = default;

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
        const AnimGraphNode* findNode( int32 nodeID ) const;
        /** @brief 이름으로 노드를 찾습니다. */
        const AnimGraphNode* findNodeByName( string_view name ) const;
        /** @brief 진입 노드(들어오는 링크가 없는 첫 노드, 없으면 목록 앞)를 반환합니다. */
        const AnimGraphNode* findEntryNode() const;
        /** @brief 그 노드에서 나가는 첫 링크의 대상 id 입니다. 없으면 0 입니다. */
        int32 findFirstOutgoingNodeID( int32 fromNodeID ) const;
        /** @brief 조건 없는 첫 나가는 링크("끝나면 다음")입니다. 없으면 nullptr 입니다. */
        const AnimGraphLink* findFinishLink( int32 fromNodeID ) const;
        /** @brief 조건 표기를 읽습니다. 모르는 표기면 false 입니다. */
        [[nodiscard]] static bool parseConditionOp( string_view text, AnimConditionOp& outOp );
        /** @brief 조건 표기를 씁니다. */
        static const utf8* getConditionOpText( AnimConditionOp op );

        vector<AnimGraphNode> _listNode; /**< 노드 목록입니다. */
        vector<AnimGraphLink> _listLink; /**< 링크 목록입니다. */

    private:
        /** @brief 이미 파싱된 JSON 루트에서 노드 · 링크를 읽습니다. 모르는 조건 표기면 false 입니다. */
        [[nodiscard]] bool parseRoot( const JSONValue& root );
    };
} // namespace sw
