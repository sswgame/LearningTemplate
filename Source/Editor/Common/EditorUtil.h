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

        /**
         * @brief 계층 라벨에 붙일 `[Category]` 뱃지를 덧붙입니다.
         * @param category 컴포넌트 타입의 리플렉션 Category (`TypeInfo::getCategory`).
         * @param inoutBadge 누적 중인 뱃지 문자열. 비어 있지 않으면 앞에 공백이 붙습니다.
         * @details 예전에는 Hierarchy 패널이 타입 **이름** 7개를 if/else 로 비교해 뱃지를 골랐다.
         *          게임이 자기 컴포넌트를 넣으면 뱃지가 없었고, 엔진이 컴포넌트를 늘릴 때마다 그
         *          패널을 같이 고쳐야 했다. 같은 파일이 "컴포넌트 추가" 메뉴에서는 이미
         *          `getCategory()` 로 묶고 있었다 — 데이터는 있었는데 한쪽만 안 쓰고 있었다.
         *          같은 Category 는 한 번만 넣는다.
         */
        static void appendCategoryBadge( string_view category, string& inoutBadge );
    };
} // namespace sw::editor
