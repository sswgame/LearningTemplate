#pragma once
/**
 * @file ResourceUtil.h
 * @brief 엔진/공통/게임 리소스 루트 경로 해석
 * @note
 *   - 리소스 루트 절대경로는 FS 실제 대소문자를 유지한다.
 *   - 루트 아래 상대경로(폴더·파일)는 소문자를 정규형으로 쓴다.
 *   - 비교/맵 키는 FileUtil::normalizePath(전체 경로)를 사용한다.
 */

#include "Core/Common/Types.h"
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
		 * @param filePath 파일 상대 경로 (소문자 정규형 우선, 없으면 원본 case fallback)
		 * @param folderName 검색을 한정할 하위 폴더(비우면 전체 리소스 루트)
		 * @return I/O에 쓸 수 있는 절대 경로 (루트 case 유지 + 구분자 `/`)
		 */
		static std::string getResourcePath( const std::string_view filePath, const std::string_view folderName = "" );
		/** @brief Engine 리소스 폴더 절대 경로 */
		static const std::string& getEngineFolderPath();
		/** @brief Common 리소스 폴더 절대 경로 */
		static const std::string& getCommonFolderPath();
		/** @brief Game 리소스 폴더 절대 경로 */
		static const std::string& getGameFolderPath();
		/** @brief 리소스 루트(상위) 절대 경로 */
		static const std::string& getRootFolderPath();
		/** @brief 주어진 폴더 이름이 존재하는 모든 리소스 경로들의 절대 경로 목록을 반환합니다.*/
		static std::vector<std::string> getResourceFolders( const std::string_view folderName );

		/**
		 * @brief 저장/임포트용 절대 경로를 만듭니다.
		 * @details absoluteFolder가 속한 리소스 루트는 FS 대소문자를 유지하고,
		 *          루트 아래 상대 폴더·fileName은 소문자로 강제합니다.
		 */
		static std::string makeSavePath( const std::string_view absoluteFolder, const std::string_view fileName );
		/**
		 * @brief 저장용 폴더 절대 경로 (루트 아래 상대 폴더만 소문자).
		 */
		static std::string makeSaveFolderPath( const std::string_view absoluteFolder );

	private:
		static bool								  _s_bInitialize;
		static std::string						  _s_engineFolderPath;
		static std::string						  _s_commonFolderPath;
		static std::string						  _s_gameFolderPath;
		static std::vector<std::filesystem::path> _s_resourceFolderList;
		static std::vector<std::string>			  _s_resourceFolderStrList;
	};
} // namespace sw
