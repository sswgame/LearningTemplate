#pragma once
/**
 * @file ObjectStateSerializer.h
 * @brief GameObject 상태 XML 저장/로드 및 파일 다이얼로그
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/File/FileUtil.h"

namespace sw
{
	class GameObject;

	class SW_API ObjectStateSerializer
	{
	public:
		/**
		 * @brief GameObject 상태를 XML 문자열로 직렬화합니다.
		 * @details Name / IsActive / Tags / Components(+ReflectedXml) /
		 *          SceneTransforms(local TRS + parent as ownerName/stableKey).
		 *          SceneTransforms keys are stable (componentName|typeName + occurrence).
		 *          ObjectId는 디버그용으로만 기록하며 런타임 발급 ID이므로 로드 시 복원하지 않습니다.
		 */
		static std::string saveToXmlString( const GameObject* gameObject );

		/**
		 * @brief XML 문자열에서 GameObject 상태를 복원합니다 (ObjectId 제외).
		 * @details 적용 전에 기존 Components/Tags를 clear하여 중복·잔존 상태를 방지합니다.
		 *          SceneTransforms 개수와 SceneComponent 개수가 다르면 ERROR 로그를 남깁니다.
		 */
		static bool loadFromXmlString( GameObject* gameObject, std::string_view xmlString );

		/**
		 * @brief SceneTransforms의 부모 attach만 다시 해석합니다 (다중 GO 복원 후 cross-GO용).
		 */
		static bool rebindSceneHierarchy( GameObject* gameObject, std::string_view xmlString );

		/** @brief GameObject 상태를 XML 파일로 저장합니다. */
		static bool saveToXmlFile( const GameObject* gameObject, const std::string_view filePath );

		/** @brief XML 파일에서 GameObject 상태를 로드합니다. */
		static bool loadFromXmlFile( GameObject* gameObject, const std::string_view filePath );

		/** @brief 저장용 파일 다이얼로그를 엽니다. */
		static void openSaveFileDialog( const GameObject* gameObject, FileDialogDelegate onSaveDone = {} );

		/** @brief 로드용 파일 다이얼로그를 엽니다. */
		static void openLoadFileDialog( GameObject* gameObject, FileDialogDelegate onLoadDone = {} );
	};
} // namespace sw
