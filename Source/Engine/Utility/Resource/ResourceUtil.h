/**
 * @file ResourceUtil.h
 * @brief 엔진/공통/게임/에디터 리소스 루트 경로 해석
 * @note
 *   - `Resource/` 는 표시·감시용 최상위일 뿐, getResourcePath 검색 루트가 아니다.
 *   - 검색 루트: `game/<pack>/`, `common/`, `engine/`, `editor/` (파일은 항상 이들 아래).
 *   - 키(소문자 정규형):
 *     - 팩 상대: `pipeline/foo.xml`, `shaders/bar.hlsl` → 검색 루트들을 순회
 *     - 전역 ID: `engine/...`, `common/...`, `game/<pack>/...`, `editor/...` → 해당 도메인 루트만
 *   - 없으면 empty. Resource/ 에 붙여 성공시키는 처리는 하지 않는다.
 *   - 비교/맵 키는 FileUtil::normalizePath(전체 경로)를 사용한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/**
	 * @class ResourceUtil
	 * @brief 리소스 도메인 루트·검색 루트를 해석하고, 논리 경로 ↔ 절대 경로를 변환합니다.
	 * @note 경로 I/O만 담당합니다. 에셋 소유권은 ResourceManager입니다.
	 */
	class SW_API ResourceUtil
	{
	public:
		/**
		 * @brief cwd에서 상위로 올라가 `Resource/` 를 찾고, 도메인/검색 루트를 채웁니다.
		 * @return 프로젝트 루트를 찾으면 true. 못 찾으면 false(어설트도 발생).
		 */
		static bool initialize();

		/**
		 * @brief 상대 리소스 경로를 절대 경로로 해석합니다 (파일이 존재할 때만).
		 * @param filePath 팩 상대 키 또는 `engine/`/`common/`/`game/<pack>/`/`editor/` 전역 ID
		 * @param folderName 각 검색 루트 아래 하위 폴더로 검색을 한정 (비우면 루트 직하부터)
		 * @return I/O에 쓸 수 있는 절대 경로. 못 찾으면 empty.
		 * @note 소문자 키로 먼저 찾고, 실패 시 원본 표기로 한 번 더 시도합니다(대소문자 구분 FS).
		 */
		static string getResourcePath( string_view filePath, string_view folderName = "" );

		/**
		 * @brief 논리 경로 → 절대 경로 (파일이 아직 없어도 도메인 루트 기준으로 조합).
		 * @param relativePath 전역 ID 또는 이미 존재하는 팩 상대 키
		 * @return 절대 경로. 전역 ID가 아니면서 파일도 없으면 empty.
		 * @details 저장·.meta 생성용. 전역 ID(`engine/`/`common/`/`game/<pack>/`/`editor/`…)만 미존재 시에도 확정 가능.
		 *          팩 상대 키는 이미 존재하는 경우에만 getResourcePath 결과를 돌려준다.
		 */
		static string makeAbsolutePath( string_view relativePath );

		/**
		 * @brief 상대 리소스 경로를 해석한 뒤 텍스트로 읽습니다.
		 * @param relativePath 팩 상대 키 또는 전역 ID (해석 실패 시 인자 그대로 open 시도)
		 * @param outText 읽은 UTF-8 본문
		 * @param outAbsPath 실제 사용한 절대 경로 (nullable)
		 * @return 파일 읽기 성공 여부
		 */
		static bool readTextResource( string_view relativePath, string& outText,
									  string* outAbsPath = nullptr );

		/** @brief Engine 리소스 폴더 절대 경로 (`Resource/engine`). 없으면 empty. */
		static const string& getEngineFolderPath();
		/** @brief Common 리소스 폴더 절대 경로 (`Resource/common`). 없으면 empty. */
		static const string& getCommonFolderPath();
		/** @brief Game 컨테이너 절대 경로 (`Resource/game`) — 표시/브라우즈용. 검색 루트는 하위 팩들. */
		static const string& getGameFolderPath();
		/** @brief 활성 게임 팩 폴더 절대 경로 (`Resource/game/<pack>`). GameConfig._packRoot 기준. */
		static string getActivePackFolderPath();
		/** @brief 활성 팩 폴더 아래 상대 경로를 이어 붙입니다. relative가 비면 팩 루트. */
		static string joinActivePackPath( string_view relativeUnderPack );
		/** @brief Editor 리소스 폴더 절대 경로 (`Resource/editor`). 없으면 empty. */
		static const string& getEditorFolderPath();
		/** @brief `Resource/` 폴더 절대 경로 (표시·감시용 최상위, 검색 루트 아님). */
		static const string& getRootFolderPath();
		/** @brief 프로젝트 루트 절대 경로 (`Resource/`·`Config/` 의 부모). */
		static const string& getProjectFolderPath();

		/**
		 * @brief 검색 루트들 아래에서 `folderName` 디렉터리가 존재하는 절대 경로 목록을 반환합니다.
		 * @param folderName 예: `shaders`, `textures` (소문자 우선, 원본 표기 재시도)
		 */
		static vector<string> getResourceFolders( string_view folderName );

		/**
		 * @brief 저장용 폴더 절대 경로를 만듭니다.
		 * @param absoluteFolder 기존에 속한 검색/도메인 루트 아래의 절대 폴더
		 * @return 루트는 FS 대소문자 유지, 루트 아래 상대 구간만 소문자로 강제한 경로
		 */
		static string makeSaveFolderPath( string_view absoluteFolder );

		/**
		 * @brief 저장/임포트용 절대 파일 경로를 만듭니다.
		 * @param absoluteFolder 저장 대상 폴더(절대)
		 * @param fileName 파일명 — 소문자로 강제
		 * @details absoluteFolder가 속한 검색 루트는 FS 대소문자를 유지하고,
		 *          루트 아래 상대 폴더·fileName은 소문자로 강제합니다.
		 */
		static string makeSavePath( string_view absoluteFolder, string_view fileName );

	private:
		static atomic<bool>	  _s_bInitialize;			 ///< initialize() 완료 여부
		static string		  _s_projectFolderPath;		 ///< 프로젝트 루트
		static string		  _s_resourceRootFolderPath; ///< Resource/ (표시용)
		static string		  _s_engineFolderPath;		 ///< Resource/engine
		static string		  _s_commonFolderPath;		 ///< Resource/common
		static string		  _s_gameFolderPath;		 ///< Resource/game
		static string		  _s_editorFolderPath;		 ///< Resource/editor
		static vector<string> _s_resourceFolderList;	 ///< getResourcePath 검색 루트들
	};
} // namespace sw
