/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸리티입니다(프로젝트 경로, 프리팹 스폰, 씬 편집 허용 여부 등). UI 에 의존하지 않습니다.
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
    /** @brief 에디터 설정 경로 해석과 공통 유틸리티입니다. ImGui 에 의존하지 않습니다(폰트는 Gui/EditorFontSetup). */
    class EditorUtil
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 프로젝트 루트 · Config/Editor
        //    Resource의 부모가 프로젝트 루트. 설정 폴더는 없으면 생성
        // ------------------------------------------------------------------------------
        /**
         * @brief 프로젝트 루트(<Project>, Resource 의 부모)를 반환합니다.
         * @return 해석에 실패하면 빈 문자열
         */
        static string getProjectRootPath();

        /**
         * @brief <Project>/Config/Editor 디렉터리를 반환합니다(없으면 만듭니다).
         * @return 해석에 실패하면 빈 문자열
         */
        static string getEditorConfigDirectory();

        /**
         * @brief Config/Editor 아래 사용자 설정 파일의 절대 경로를 반환합니다.
         * @return 해석에 실패하면 빈 문자열
         */
        static string resolveEditorConfigFile( const utf8* pFileName );

        /**
         * @brief 호스트 상대 경로를 프로젝트 루트 기준 절대 경로로 만듭니다. 이미 절대 경로면 그대로 둡니다.
         * @details 이 다섯 줄이 설정 파일을 다루는 **세 곳에 복사**되어 있었고(`EditorConfig::loadFromHost` · `saveToHost` ·
         *          `EditorData::loadFromHostPath`), 셋 다 "절대 경로인가" 를 손으로 다시 적고 있었습니다. 게다가 그 손 판정은
         *          `FileUtil::isAbsolutePath` 와 **달랐습니다**(드라이브 문자가 글자인지 보지 않았습니다). 여기에 한 번 두고
         *          제대로 된 판정을 씁니다.
         * @return 프로젝트 루트를 찾지 못하면 구분자만 정규화한 입력을 그대로 반환합니다.
         */
        static string resolveProjectRelativePath( string_view hostRelativePath );

        // ------------------------------------------------------------------------------
        // 2) 프리팹 스폰 · 편집 허용
        //    애셋 종류 판별은 EditorAssetTypeRegistry::matches 가 정본입니다
        // ------------------------------------------------------------------------------
        /** @brief 프리팹을 스폰합니다(부모가 있으면 그 아래에). 실패하면 로그를 남기고 nullptr 을 반환합니다. */
        static GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent = nullptr );

        /** @brief 플레이 세션이 정지(Stop) 상태일 때만 씬 오브젝트를 편집할 수 있습니다. */
        static bool areSceneEditsAllowed();

        /**
         * @brief 계층 라벨에 붙일 `[Category]` 뱃지를 덧붙입니다.
         * @param category 컴포넌트 타입의 리플렉션 Category (`TypeInfo::getCategory`).
         * @param inoutBadge 누적 중인 뱃지 문자열. 비어 있지 않으면 앞에 공백이 붙습니다.
         * @details 예전에는 Hierarchy 패널이 타입 **이름** 7개를 if/else 로 비교해 뱃지를 골랐습니다. 게임이 자기 컴포넌트를
         *          넣으면 뱃지가 없었고, 엔진이 컴포넌트를 늘릴 때마다 그 패널을 같이 고쳐야 했습니다. 같은 파일의 "컴포넌트
         *          추가" 메뉴는 이미 `getCategory()` 로 묶고 있었습니다. 데이터는 있었는데 한쪽만 쓰지 않고 있었던 것입니다.
         *          같은 Category 는 한 번만 넣습니다.
         */
        static void appendCategoryBadge( string_view category, string& inoutBadge );
    };
} // namespace sw::editor
