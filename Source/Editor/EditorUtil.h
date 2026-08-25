/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸 (폰트 경로 등 플랫폼 의존 처리)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 에디터 폰트·설정 경로 해석 */
	class EditorUtil
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 폰트 — 시스템 디렉터리 · 후보 파일 탐색
		//    에디터 리소스 Fonts를 먼저, 없으면 시스템 트리를 재귀 검색
		// ------------------------------------------------------------------------------
		/** @brief 플랫폼별 시스템 폰트 디렉터리 목록 (존재하는 경로만). */
		static vector<string> getSystemFontsDirectories();

		/**
		 * @brief 에디터 리소스 Fonts → 시스템 Fonts 순으로 폰트 파일을 찾습니다.
		 * @return 존재하면 절대 경로, 없으면 빈 문자열
		 */
		static string resolveFontFile( const utf8* pFileName );

		/**
		 * @brief 여러 파일명 후보를 순서대로 탐색합니다 (시스템 폰트 트리는 재귀 검색).
		 * @return 첫 번째 존재하는 절대 경로, 없으면 빈 문자열
		 */
		static string resolveFontFile( const vector<string>& fileNames );

		// ------------------------------------------------------------------------------
		// 2) 프로젝트 루트 · Config/Editor
		//    Resource의 부모가 프로젝트 루트. 설정 폴더는 없으면 생성
		// ------------------------------------------------------------------------------
		/**
		 * @brief 프로젝트 루트 (<Project>, Resource의 부모).
		 * @return 해석 실패 시 빈 문자열
		 */
		static string getProjectRootPath();

		/**
		 * @brief <Project>/Config/Editor 디렉터리 (없으면 생성).
		 * @return 해석 실패 시 빈 문자열
		 */
		static string getEditorConfigDirectory();

		/**
		 * @brief Config/Editor 아래 유저 설정 파일 절대 경로.
		 * @return 해석 실패 시 빈 문자열
		 */
		static string resolveEditorConfigFile( const utf8* pFileName );
	};
} // namespace sw
