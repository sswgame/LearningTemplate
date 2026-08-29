/**
 * @file EditorAssetType.h
 * @brief 에디터 애셋 종류·패널 매핑 SSOT
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
	/** @brief 에디터가 구분하는 애셋 종류. 한 경로가 여러 종류에 걸릴 수 있습니다. */
	enum class EditorAssetKind : uint8
	{
		Unknown = 0,
		Scene,
		Prefab,
		Texture,
		Material,
		Shader,
		Audio,
		Data,
		AnimationGraph,
		DialogueGraph,
		SpriteClip,
		TileMap,
		Sequence
	};

	/** @brief 확장자/접미사 → 도구 패널 제목 */
	struct EditorAssetPanelMapping
	{
		string_view _suffix;
		string_view _title;
	};

	/**
	 * @class EditorAssetTypeRegistry
	 * @brief 종류 판별과 애셋 에디터 패널 매핑의 단일 정의
	 */
	class EditorAssetTypeRegistry
	{
	public:
		/** @brief 경로가 지정 종류와 맞는지 여부를 반환합니다. */
		static bool matches( EditorAssetKind kind, string_view path );
		/** @brief nullptr이면 false입니다. */
		static bool matches( EditorAssetKind kind, const utf8* pPath );

		/** @brief 도구 패널을 여는 접미사 목록입니다. outCount에 개수를 씁니다. */
		static const EditorAssetPanelMapping* getPanelMappings( uint32& outCount );
	};
} // namespace sw::editor
