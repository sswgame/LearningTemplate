/**
 * @file DialogueGraphAsset.h
 * @brief 에디터/런타임이 공유하는 대화 그래프 JSON 애셋
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 대화 노드 타입 */
	enum class DialogueAssetNodeType : uint8
	{
		Start = 0,
		Dialogue,
		Choice,
		Branch,
		Action,
		End
	};

	/** @brief 대화 그래프 노드 */
	struct DialogueAssetNode
	{
		int32				  _id{ 0 };
		DialogueAssetNodeType _type{ DialogueAssetNodeType::Dialogue };
		string				  _speaker;
		string				  _text;
		string				  _condition;
		string				  _actionCommand;
		vector<string>		  _listChoice;
		float32				  _x{ 40.0f };
		float32				  _y{ 40.0f };
	};

	/** @brief 대화 그래프 링크 */
	struct DialogueAssetLink
	{
		int32 _id{ 0 };
		int32 _fromPin{ 0 };
		int32 _toPin{ 0 };
	};

	/**
	 * @class DialogueGraphAsset
	 * @brief 대화 그래프 JSON. 노드 text는 로컬라이즈 키로 해석합니다.
	 */
	class SW_API DialogueGraphAsset
	{
	public:
		/** @brief 빈 그래프를 만듭니다. */
		DialogueGraphAsset() = default;

		/** @brief JSON 파일을 읽습니다. */
		bool loadFromFile( string_view path );
		/** @brief JSON 파일을 씁니다. */
		bool saveToFile( string_view path ) const;
		/** @brief JSON 본문을 파싱합니다. */
		bool parseJson( string_view json );
		/** @brief JSON 본문을 만듭니다. */
		string toJson() const;
		/** @brief Start 노드를 반환합니다. 없으면 nullptr입니다. */
		const DialogueAssetNode* findStartNode() const;
		/** @brief id로 노드를 찾습니다. */
		const DialogueAssetNode* findNode( int32 nodeId ) const;
		/** @brief text 필드를 로컬라이즈 키로 해석합니다. 키가 없으면 원문을 반환합니다. */
		static string resolveLocalizedText( string_view textOrKey );

		vector<DialogueAssetNode> _listNode;
		vector<DialogueAssetLink> _listLink;
	};
} // namespace sw
