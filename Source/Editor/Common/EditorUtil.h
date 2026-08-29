/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸 (폰트 경로, 프로젝트 경로, 애셋 판별 등)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
} // namespace sw

namespace sw::editor
{
	/** @brief 에디터 폰트·설정 경로 해석 및 공통 유틸리티 */
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
		/** @brief EditorData 후보로 ImGui 본문·한글·아이콘 폰트를 구성합니다. */
		static void setupFonts();

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

		// ------------------------------------------------------------------------------
		// 3) 애셋 판별 및 프리팹 스폰 유틸리티
		// ------------------------------------------------------------------------------
		/** @brief 경로가 프리팹 애셋인지 여부를 반환합니다. */
		static bool isPrefabAssetPath( const utf8* pPath );

		/** @brief 경로가 텍스처 애셋인지 여부를 반환합니다. */
		static bool isTextureAssetPath( const utf8* pPath );

		/** @brief 경로가 머티리얼 애셋인지 여부를 반환합니다. */
		static bool isMaterialAssetPath( const utf8* pPath );

		/** @brief 경로가 씬 애셋인지 여부를 반환합니다. */
		static bool isSceneAssetPath( const utf8* pPath );

		/** @brief 경로가 셰이더 소스/바이너리인지 여부를 반환합니다. */
		static bool isShaderAssetPath( const utf8* pPath );

		/** @brief 선택적 부모 아래 프리팹을 스폰합니다. 실패 시 로그 후 nullptr을 반환합니다. */
		static GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent = nullptr );
	};
} // namespace sw::editor
