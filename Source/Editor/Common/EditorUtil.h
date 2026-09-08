/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸 (프로젝트 경로, 애셋 판별, 씬 편집 정책 등 — UI 비의존)
 */
#pragma once
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
    /** @brief 에디터 설정 경로 해석 및 공통 유틸리티. ImGui 에 의존하지 않는다(폰트는 Gui/EditorFontSetup). */
    class EditorUtil
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 프로젝트 루트 · Config/Editor
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
        // 3) 프리팹 스폰 · 편집 허용
        //    애셋 종류 판별은 EditorAssetTypeRegistry::matches 가 정본입니다
        // ------------------------------------------------------------------------------
        /** @brief 선택적 부모 아래 프리팹을 스폰합니다. 실패 시 로그 후 nullptr을 반환합니다. */
        static GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent = nullptr );

        /** @brief Play가 정지 상태이면 씬 오브젝트 편집이 허용됩니다. */
        static bool areSceneEditsAllowed();
    };
} // namespace sw::editor
