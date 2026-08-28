/**
 * @file ObjectStateSerializer.h
 * @brief GameObject 상태 저장/로드. 저작 기본 포맷은 XML입니다.
 * @details JSON은 도구 interchange용입니다. Shipping 쿠킹 결과는 Prefab/Scene binary입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"

namespace sw
{
	class GameObject;

	/// @brief GameObject/Component 리플렉션 스키마 버전 (XmlSerializer::_schemaVersion).
	inline constexpr uint32 kObjectReflectedSchemaVersion = 0;

	/**
	 * @class ObjectStateSerializer
	 * @brief GameObject 상태를 XML로 저장·로드하고 파일 다이얼로그를 엽니다
	 */
	class SW_API ObjectStateSerializer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 문자열 — PROPERTY 기반 GameObject XML. 부모는 SceneComponent `_pParent`.
		// ------------------------------------------------------------------------------
		/**
		 * @brief GameObject 상태를 XML 문자열로 직렬화합니다.
		 * @details 루트는 TypeInfo 이름 `GameObject`. 스칼라는 attribute, `_listComponent`는
		 *          `<vector _name="_listComponent">` 아래 런타임 타입 노드. 로컬 TRS와 Attach는 SceneComponent PROPERTY.
		 *          GameObject 부모는 `getParent()`가 SceneComponent `_pParent`에서 유도하므로
		 *          별도 `_parentGO` 필드/속성을 쓰지 않습니다.
		 */
		static string saveToXmlString( const GameObject* pGameObject );
		/** @brief GameObject 상태를 JSON으로 직렬화합니다. XmlSerializer와 같은 PROPERTY 그래프입니다. */
		[[maybe_unused]] static string saveToJsonString( const GameObject* pGameObject );

		/** @brief GameObject 상태를 바이너리 버퍼로 고속 직렬화합니다 (핫리로드/프리팹용). */
		static bool saveToBinaryBuffer( const GameObject* pGameObject, vector<uint8>& outBuffer );

		/**
		 * @brief XML 문자열에서 GameObject 상태를 복원합니다 (ObjectId 제외).
		 * @details 적용 전에 기존 컴포넌트를 clear합니다. 부모 GO가 아직 없으면
		 *          SceneComponent Attach는 실패할 수 있으므로 이후 rebindSceneHierarchy로 확정합니다.
		 */
		static bool					 loadFromXmlString( GameObject* pGameObject, string_view xmlString );
		[[maybe_unused]] static bool loadFromJsonString( GameObject* pGameObject, string_view jsonString );

		/** @brief 바이너리 버퍼에서 GameObject 상태를 복원하고 읽은 바이트 수를 반환합니다 (실패 시 0). */
		static size_t loadFromBinaryBuffer( GameObject* pGameObject, const uint8* pData, size_t size, string& outParentName );

		/**
		 * @brief SceneComponent Attach 필드로 계층을 다시 해석합니다.
		 * @details 다중 GO 복원 후 호출 (모든 GO가 존재하는 전제).
		 */
		static bool					 rebindSceneHierarchy( GameObject* pGameObject, string_view xmlString );
		[[maybe_unused]] static bool rebindSceneHierarchyFromJson( GameObject* pGameObject, string_view jsonString );

		// ------------------------------------------------------------------------------
		// 2) 파일 · 다이얼로그
		// ------------------------------------------------------------------------------
		/** @brief GameObject 상태를 XML 파일로 저장합니다. */
		static bool saveToXmlFile( const GameObject* pGameObject, string_view filePath );

		/** @brief XML 파일에서 GameObject 상태를 로드합니다. */
		static bool loadFromXmlFile( GameObject* pGameObject, string_view filePath );

		/** @brief 저장용 파일 다이얼로그를 엽니다. */
		[[maybe_unused]] static void openSaveFileDialog( const GameObject* pGameObject, FileDialogDelegate onSaveDone = {} );

		/** @brief 로드용 파일 다이얼로그를 엽니다. */
		[[maybe_unused]] static void openLoadFileDialog( GameObject* pGameObject, FileDialogDelegate onLoadDone = {} );
	};
} // namespace sw
