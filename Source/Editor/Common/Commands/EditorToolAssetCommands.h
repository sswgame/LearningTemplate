/**
 * @file EditorToolAssetCommands.h
 * @brief 애니메이션/대화 그래프, 타일맵, 스프라이트 클립, 프리팹 오버라이드 파일 IO
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	class GameObject;
	class SequenceAsset;
} // namespace sw

namespace sw::editor
{
	/** @brief 애니메이션 그래프 노드 */
	struct EditorAnimGraphNode
	{
		int32	_id{ 0 };
		string	_name;
		float32 _x{ 40.0f };
		float32 _y{ 40.0f };
	};

	/** @brief 애니메이션 그래프 링크 */
	struct EditorAnimGraphLink
	{
		int32 _id{ 0 };
		int32 _fromNode{ 0 };
		int32 _toNode{ 0 };
	};

	/** @brief 애니메이션 그래프 파일 데이터 */
	struct EditorAnimGraphData
	{
		vector<EditorAnimGraphNode> _listNode;
		vector<EditorAnimGraphLink> _listLink;
	};

	/** @brief 대화 노드 타입 */
	enum class DialogueNodeType : uint8
	{
		Start = 0,
		Dialogue,
		Choice,
		Branch,
		Action,
		End
	};

	/** @brief 대화 그래프 노드 */
	struct EditorDialogueNode
	{
		int32			 _id{ 0 };
		DialogueNodeType _type{ DialogueNodeType::Dialogue };
		string			 _speaker;
		string			 _text;
		string			 _condition;
		string			 _actionCommand;
		vector<string>	 _listChoice;
		float32			 _x{ 40.0f };
		float32			 _y{ 40.0f };
	};

	/** @brief 대화 그래프 링크 */
	struct EditorDialogueLink
	{
		int32 _id{ 0 };
		int32 _fromPin{ 0 };
		int32 _toPin{ 0 };
	};

	/** @brief 대화 그래프 파일 데이터 */
	struct EditorDialogueGraphData
	{
		vector<EditorDialogueNode> _listNode;
		vector<EditorDialogueLink> _listLink;
	};

	/** @brief Visual 레이어 셀 */
	struct EditorTileVisual
	{
		uint8 _height{ 0 };
		uint8 _tintR{ 180 };
		uint8 _tintG{ 200 };
		uint8 _tintB{ 160 };
		uint8 _atlasId{ 0 };
	};

	/** @brief Warp 셀 */
	struct EditorTileWarp
	{
		int32  _tileX{ 0 };
		int32  _tileY{ 0 };
		string _targetMap;
		int32  _targetTileX{ 1 };
		int32  _targetTileY{ 1 };
		string _pairId;
	};

	/** @brief 맵 조우 테이블 행 */
	struct EditorTileEncounterEntry
	{
		string	_speciesId;
		float32 _weight{ 1.0f };
	};

	/** @brief TileMap XML 데이터 */
	struct EditorTileMapData
	{
		string							 _name;
		string							 _scenePath;
		string							 _role;
		int32							 _width{ 8 };
		int32							 _height{ 8 };
		int32							 _spawnX{ 1 };
		int32							 _spawnY{ 1 };
		vector<uint8>					 _listWalkable;
		vector<uint8>					 _listEncounter;
		vector<uint8>					 _listPassThrough;
		vector<EditorTileVisual>		 _listVisual;
		vector<EditorTileWarp>			 _listWarp;
		vector<EditorTileEncounterEntry> _listEncounterEntry;
	};

	/** @brief 스프라이트 클립 프레임 */
	struct EditorSpriteClipFrame
	{
		float32 _u{ 0.0f };
		float32 _v{ 0.0f };
		float32 _w{ 1.0f };
		float32 _h{ 1.0f };
		int32	_durationMs{ 100 };
	};

	/** @brief 스프라이트 클립 트랜스폼 키 */
	struct EditorSpriteClipKey
	{
		float32 _time{ 0.0f };
		float32 _x{ 0.0f };
		float32 _y{ 0.0f };
		float32 _angleDeg{ 0.0f };
	};

	/** @brief SpriteClip.json 데이터 */
	struct EditorSpriteClipData
	{
		string						  _atlasPath;
		vector<EditorSpriteClipFrame> _listFrame;
		vector<EditorSpriteClipKey>	  _listKey;
	};

	/** @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목 */
	struct PrefabOverrideItem
	{
		string _componentName;
		string _propertyName;
		string _defaultValue;
		string _overriddenValue;
		bool   _bModified{ false };
	};

	/**
	 * @class EditorToolAssetCommands
	 * @brief 도구 패널이 쓰던 파일 IO를 ImGui 없이 수행합니다.
	 */
	class EditorToolAssetCommands
	{
	public:
		/** @brief 애니메이션 그래프 JSON을 읽습니다. path가 비면 에디터 설정 기본 파일을 씁니다. */
		static bool loadAnimationGraph( EditorAnimGraphData& outData, string_view path = {} );
		/** @brief 애니메이션 그래프 JSON을 씁니다. */
		static bool saveAnimationGraph( const EditorAnimGraphData& data, string_view path = {} );
		/** @brief 애니메이션 그래프를 JSON 문자열로 직렬화합니다. */
		static string serializeAnimationGraph( const EditorAnimGraphData& data );
		/** @brief JSON 문자열을 애니메이션 그래프로 파싱합니다. */
		static bool parseAnimationGraph( string_view json, EditorAnimGraphData& outData );
		/** @brief 대화 노드 타입 이름을 반환합니다. */
		static const utf8* dialogueNodeTypeName( DialogueNodeType type );
		/** @brief 대화 노드 타입 문자열을 파싱합니다. */
		static DialogueNodeType parseDialogueNodeType( string_view typeStr );
		/** @brief 대화 그래프 JSON을 읽습니다. path가 비면 기본 대화 파일을 씁니다. */
		static bool loadDialogueGraph( EditorDialogueGraphData& outData, string_view path = {} );
		/** @brief 대화 그래프 JSON을 씁니다. */
		static bool saveDialogueGraph( const EditorDialogueGraphData& data, string_view path = {} );
		/** @brief 대화 그래프를 JSON 문자열로 직렬화합니다. */
		static string serializeDialogueGraph( const EditorDialogueGraphData& data );
		/** @brief JSON 문자열을 대화 그래프로 파싱합니다. */
		static bool parseDialogueGraph( string_view json, EditorDialogueGraphData& outData );
		/** @brief Resource 상대 경로의 TileMap XML을 읽습니다. */
		static bool loadTileMap( string_view assetRelativePath, EditorTileMapData& outData, string& outStatus );
		/** @brief Resource 상대 경로로 TileMap XML을 씁니다. */
		static bool saveTileMap( string_view assetRelativePath, const EditorTileMapData& data );
		/** @brief SpriteClip JSON을 읽습니다. path가 비면 에디터 설정 기본 파일을 씁니다. */
		static bool loadSpriteClip( EditorSpriteClipData& outData, string& outStatus, string_view path = {} );
		/** @brief SpriteClip JSON을 씁니다. */
		static bool saveSpriteClip( const EditorSpriteClipData& data, string_view path = {} );
		/** @brief SpriteClip을 JSON 문자열로 직렬화합니다. */
		static string serializeSpriteClip( const EditorSpriteClipData& data );
		/** @brief JSON 문자열을 SpriteClip으로 파싱합니다. */
		static bool parseSpriteClip( string_view json, EditorSpriteClipData& outData );
		/** @brief 시퀀서 JSON을 읽습니다. */
		static bool loadSequence( sw::SequenceAsset& outAsset, string_view path );
		/** @brief 시퀀서 JSON을 씁니다. */
		static bool saveSequence( const sw::SequenceAsset& asset, string_view path );
		/** @brief 선택 인스턴스와 프리팹 CDO를 비교해 오버라이드 목록을 채웁니다. */
		static void collectPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
											string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
											vector<string>& outNestedPrefab );
		/** @brief 한 오버라이드를 인스턴스에 템플릿 기본값으로 되돌립니다. */
		static void revertPrefabOverride( sw::GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath );
		/** @brief 인스턴스 상태를 프리팹 템플릿에 저장합니다. */
		static bool applyPrefabOverridesToTemplate( sw::GameObject* pInstance, string_view prefabPath );
		/** @brief 인스턴스를 프리팹 CDO로 되돌립니다. */
		static bool revertAllPrefabOverrides( sw::GameObject* pInstance, string_view prefabPath );
	};
} // namespace sw::editor
