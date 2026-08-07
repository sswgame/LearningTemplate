#pragma once
/**
 * @file ObjectStateSerializer.h
 * @brief Auto-generated documentation header
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
		 * @brief saveToXmlString 처리를 수행합니다.
		 */
		static std::string saveToXmlString( const GameObject* gameObject );

		/**
		 * @brief loadFromXmlString 처리를 수행합니다.
		 */
		static bool loadFromXmlString( GameObject* gameObject, std::string_view xmlString );

		/**
		 * @brief saveToXmlFile 처리를 수행합니다.
		 */
		static bool saveToXmlFile( const GameObject* gameObject, const std::string_view filePath );

		/**
		 * @brief loadFromXmlFile 처리를 수행합니다.
		 */
		static bool loadFromXmlFile( GameObject* gameObject, const std::string_view filePath );

		/**
		 * @brief openSaveFileDialog 처리를 수행합니다.
		 */
		static void openSaveFileDialog( const GameObject* gameObject, FileDialogDelegate onSaveDone = {} );

		/**
		 * @brief openLoadFileDialog 처리를 수행합니다.
		 */
		static void openLoadFileDialog( GameObject* gameObject, FileDialogDelegate onLoadDone = {} );
	};
}
