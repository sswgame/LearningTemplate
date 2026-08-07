#pragma once
/**
 * @file ResourceUtil.h
 * @brief 엔진/공통/게임 리소스 루트 경로 해석 및 캐시
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	class SW_API ResourceUtil
	{
	public:
		/** @brief 실행 파일 기준 리소스 루트·하위 폴더 경로를 초기화합니다. */
		static bool initialize();
		/**
		 * @brief 상대 리소스 경로를 절대 경로로 해석합니다.
		 * @param filePath 파일 상대 경로
		 * @param folderName 검색을 한정할 하위 폴더(비우면 전체 리소스 루트)
		 */
		static std::string getResourcePath( const std::string_view filePath, const std::string_view folderName = "" );
		/** @brief 경로 해석 캐시를 비웁니다. */
		static void clearCache();

		/** @brief Engine 리소스 폴더 절대 경로 */
		static const std::string& getEngineFolderPath();
		/** @brief Common 리소스 폴더 절대 경로 */
		static const std::string& getCommonFolderPath();
		/** @brief Game 리소스 폴더 절대 경로 */
		static const std::string& getGameFolderPath();
		/** @brief 리소스 루트(상위) 절대 경로 */
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
} // namespace sw
