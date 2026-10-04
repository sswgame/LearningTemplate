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

namespace sw
{
    class JsonValue;

    /** @brief 애니메이션 그래프 노드입니다. */
    struct AnimGraphNode
    {
        string _name;    /**< 노드 이름. 클립을 묶는 열쇠입니다. */
        int32  _id{ 0 }; /**< 그래프 안에서 유일한 노드 id. 1 이상이어야 합니다. */
        /** @brief 그래프 에디터에서의 노드 위치입니다. JSON 키는 그대로 "x"/"y" 라 파일 형식은 바뀌지 않습니다. */
        float2 _position{ 40.0f, 40.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 그래프 링크입니다. */
    struct AnimGraphLink
    {
        int32 _id{ 0 };       /**< 그래프 안에서 유일한 링크 id. 1 이상이어야 합니다. */
        int32 _fromNode{ 0 }; /**< 출발 노드 id 입니다. */
        int32 _toNode{ 0 };   /**< 도착 노드 id 입니다. */
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
        [[nodiscard]] bool parseJson( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJson() const;
        /** @brief 노드 이름 목록을 채웁니다. */
        void collectNodeNames( vector<string>& outListName ) const;
        /** @brief id 로 노드를 찾습니다. */
        const AnimGraphNode* findNode( int32 nodeId ) const;
        /** @brief 이름으로 노드를 찾습니다. */
        const AnimGraphNode* findNodeByName( string_view name ) const;
        /** @brief 진입 노드(들어오는 링크가 없는 첫 노드, 없으면 목록 앞)를 반환합니다. */
        const AnimGraphNode* findEntryNode() const;
        /** @brief 그 노드에서 나가는 첫 링크의 대상 id 입니다. 없으면 0 입니다. */
        int32 findFirstOutgoingNodeId( int32 fromNodeId ) const;

        vector<AnimGraphNode> _listNode; /**< 노드 목록입니다. */
        vector<AnimGraphLink> _listLink; /**< 링크 목록입니다. */

    private:
        /** @brief 이미 파싱된 JSON 루트에서 노드 · 링크를 읽습니다. */
        void parseRoot( const JsonValue& root );
    };
} // namespace sw
