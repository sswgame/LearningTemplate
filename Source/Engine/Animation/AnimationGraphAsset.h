/**
 * @file AnimationGraphAsset.h
 * @brief 에디터/런타임이 공유하는 애니메이션 그래프 JSON 애셋
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 애니메이션 그래프 노드 */
	struct AnimationGraphNode
	{
		string	_name;
		int32	_id{ 0 };
		float32 _x{ 40.0f };
		float32 _y{ 40.0f };
	};

	/** @brief 애니메이션 그래프 링크 */
	struct AnimationGraphLink
	{
		int32 _id{ 0 };
		int32 _fromNode{ 0 };
		int32 _toNode{ 0 };
	};

	/**
	 * @class AnimationGraphAsset
	 * @brief BlendSpace / SpriteAnimator와 노드 이름을 공유하는 JSON 그래프
	 */
	class SW_API AnimationGraphAsset
	{
	public:
		/** @brief 빈 그래프를 만듭니다. */
		AnimationGraphAsset() = default;

		/** @brief JSON 파일을 읽습니다. */
		bool loadFromFile( string_view path );
		/** @brief JSON 파일을 씁니다. */
		bool saveToFile( string_view path ) const;
		/** @brief JSON 본문을 파싱합니다. */
		bool parseJson( string_view json );
		/** @brief JSON 본문을 만듭니다. */
		string toJson() const;
		/** @brief 노드 이름 목록을 채웁니다. */
		void collectNodeNames( vector<string>& outNameList ) const;
		/** @brief id로 노드를 찾습니다. */
		const AnimationGraphNode* findNode( int32 nodeId ) const;
		/** @brief 이름으로 노드를 찾습니다. */
		const AnimationGraphNode* findNodeByName( string_view name ) const;
		/** @brief 진입 노드(들어오는 링크가 없는 첫 노드, 없으면 목록 앞)를 반환합니다. */
		const AnimationGraphNode* findEntryNode() const;
		/** @brief 해당 노드에서 나가는 첫 링크의 대상 id입니다. 없으면 0입니다. */
		int32 findFirstOutgoingNodeId( int32 fromNodeId ) const;

		vector<AnimationGraphNode> _listNode;
		vector<AnimationGraphLink> _listLink;
	};
} // namespace sw
