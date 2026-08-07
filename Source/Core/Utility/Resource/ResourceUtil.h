#pragma once
/**
 * @file ResourceUtil.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	class SW_API ResourceUtil
	{
	public:
		/**
		 * @brief initialize 처리를 수행합니다.
		 */
		static bool		   initialize();
		/**
		 * @brief getResourcePath 처리를 수행합니다.
		 */
		static std::string getResourcePath( const std::string_view filePath, const std::string_view folderName = "" );
		/**
		 * @brief clearCache 처리를 수행합니다.
		 */
		static void		   clearCache();

		static const std::string& getEngineFolderPath();

		static const std::string& getCommonFolderPath();

		static const std::string& getGameFolderPath();

		static const std::string& getRootFolderPath();

		/**
		 * @brief 주어진 폴더 이름이 존재하는 모든 리소스 경로들의 절대 경로 목록을 반환합니다.
		 */
		static std::vector<std::string> getResourceFolders( const std::string_view folderName );

	private:
		static bool											_s_bInitialize;
		static std::string									_s_engineFolderPath;
		static std::string									_s_commonFolderPath;
		static std::string									_s_gameFolderPath;
		static std::vector<std::filesystem::path>			_s_resourceFolderList;
		static std::vector<std::string>						_s_resourceFolderStrList;
		static std::unordered_map<std::string, std::string> _s_resourcePathCache;
	};
}
