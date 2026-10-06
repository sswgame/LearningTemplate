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
         * @brief 에디터가 쓰는 파일 이름입니다. 이름은 코드가 정하고 설정 파일이 바꾸지 않는다 — 설정 파일이 제 위치 ·
         *        이웃 파일 이름을 정하면 그 설정 파일을 찾는 길이 순환한다.
         */
        static constexpr const utf8* kImguiIniFileName              = "imgui.ini";
        static constexpr const utf8* kWindowsIniFileName            = "windows.ini";
        static constexpr const utf8* kAnimGraphCanvasFileName       = "AnimGraph.json";           ///< 노드 에디터 캔버스 상태
        static constexpr const utf8* kDialogueGraphCanvasFileName   = "DialogueGraphEditor.json"; ///< 노드 에디터 캔버스 상태
        static constexpr const utf8* kAnimGraphDocumentFileName     = "AnimGraphData.json";       ///< 경로 없는 애니 그래프 문서
        static constexpr const utf8* kDialogueGraphDocumentFileName = "DialogueGraphData.json";   ///< 경로 없는 대화 그래프 문서
        static constexpr const utf8* kSpriteClipDocumentFileName    = "SpriteClip.json";          ///< 경로 없는 스프라이트 클립 문서
        static constexpr const utf8* kTextureImportConfigFileName   = "TextureImportConfig.json";
        static constexpr const utf8* kModelImportConfigFileName     = "ModelImportConfig.json";
        /** @brief 에디터 팩(`path::kEditorPack`) 안의 글꼴 폴더 이름입니다. */
        static constexpr const utf8* kFontsFolderName = "fonts";

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
         * @details 설정 파일을 다루는 곳(`EditorConfig::loadFromHost` · `saveToHost` · `EditorToolDefaults::loadFromHostPath`)이 함께 씁니다.
         *          "절대 경로인가" 는 `FileUtil::isAbsolutePath` 로 판정합니다(손 판정은 드라이브 문자 검사 같은 것을 빠뜨리기 쉽습니다).
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
         * @details 타입 이름이 아니라 Category 로 고르므로 게임이 넣은 컴포넌트도 뱃지가 붙고, 엔진이 컴포넌트를 늘려도 패널을
         *          고칠 필요가 없습니다("컴포넌트 추가" 메뉴와 같은 기준). 같은 Category 는 한 번만 넣습니다.
         */
        static void appendCategoryBadge( string_view category, string& inoutBadge );
    };
} // namespace sw::editor
