/**
 * @file EditorTransformCommands.h
 * @brief 컴포넌트 붙여넣기/프리셋 및 다중 선택 정렬·스냅 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	class Component;
	class GameObject;
} // namespace sw

namespace sw::editor
{
	/** @brief 정렬 축 */
	enum class AlignAxis : uint8
	{
		X = 0,
		Y,
		Z
	};

	/** @brief 정렬 기준 */
	enum class AlignType : uint8
	{
		Min = 0,
		Center,
		Max
	};

	/**
	 * @class EditorTransformCommands
	 * @brief 워크스페이스 클립보드 바이너리/XML을 받아 붙여넣기·정렬을 수행합니다.
	 */
	class EditorTransformCommands
	{
	public:
		/** @brief 복사한 컴포넌트 바이너리/XML 값을 대상에 덮어씁니다. */
		static bool pasteComponentValues( Component* pTargetComp, const vector<uint8>& bytes, string_view xmlFallback = {} );
		static bool pasteComponentValues( Component* pTargetComp, string_view xml );
		/** @brief 복사한 타입으로 새 컴포넌트를 붙이고 바이너리/XML을 적용합니다. */
		static Component* pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, const vector<uint8>& bytes, string_view xmlFallback = {} );
		static Component* pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml );
		/** @brief 컴포넌트 프리셋을 Resource 프리셋 폴더에 저장합니다. */
		static bool saveComponentPreset( const Component* pComp, string_view presetName );
		/** @brief 프리셋 XML을 컴포넌트에 적용합니다. */
		static bool loadComponentPreset( Component* pComp, string_view presetFilePath );
		/** @brief 선택 오브젝트를 지면(Y)에 맞춥니다. */
		static void snapSelectedToGround();
		/** @brief 선택 오브젝트를 축 기준으로 정렬합니다. */
		static void alignSelectedObjects( AlignAxis axis, AlignType type );
		/** @brief 선택 오브젝트를 축 방향으로 균등 배치합니다. */
		static void distributeSelectedObjects( AlignAxis axis );
	};
} // namespace sw::editor
