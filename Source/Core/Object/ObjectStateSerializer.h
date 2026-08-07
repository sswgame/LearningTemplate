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

		/** @brief GameObject 상태를 XML 문자열로 직렬화합니다. */
		static std::string saveToXmlString( const GameObject* gameObject );

		/** @brief XML 문자열에서 GameObject 상태를 복원합니다. */
		static bool loadFromXmlString( GameObject* gameObject, std::string_view xmlString );

		/** @brief GameObject 상태를 XML 파일로 저장합니다. */
		static bool saveToXmlFile( const GameObject* gameObject, const std::string_view filePath );

		/** @brief XML 파일에서 GameObject 상태를 로드합니다. */
		static bool loadFromXmlFile( GameObject* gameObject, const std::string_view filePath );

		/** @brief 저장용 파일 다이얼로그를 엽니다. */
		static void openSaveFileDialog( const GameObject* gameObject, FileDialogDelegate onSaveDone = {} );

		/** @brief 로드용 파일 다이얼로그를 엽니다. */
		static void openLoadFileDialog( GameObject* gameObject, FileDialogDelegate onLoadDone = {} );
	};
}
