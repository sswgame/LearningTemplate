/**
 * @file EditorPaths.h
 * @brief 에디터의 프로젝트 루트 · 상태 폴더(Saved/Editor) · 상태 파일 이름입니다. UI 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    class EditorPaths
    {
    public:
        // ------------------------------------------------------------------------------
        // 프로젝트 루트 · 에디터 상태 폴더(Saved/Editor)
        //    Resource의 부모가 프로젝트 루트. 상태 폴더는 없으면 생성. 사람이 쓰는 설정(Config/Editor)은 config::kDirConfigEditor 를 바로 쓴다
        // ------------------------------------------------------------------------------
        /**
         * @brief 에디터가 쓰는 파일 이름입니다. 이름은 코드가 정하고 설정 파일이 바꾸지 않는다 — 설정 파일이 제 위치 ·
         *        이웃 파일 이름을 정하면 그 설정 파일을 찾는 길이 순환한다. 임포트 설정 둘만 `Config/Editor`(사람이 쓴다), 나머지는 `Saved/Editor`(앱이 쓴다).
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
        /** @brief 에디터 팩의 `fonts/` 안에 있는 아이콘 폰트 파일 이름입니다(`Scripts/generate/GenerateEditorIcons.py` 가 만든다). */
        static constexpr const utf8* kIconFontFileName = "sweditoricons.ttf";

        /**
         * @brief 프로젝트 루트(<Project>, Resource 의 부모)를 반환합니다.
         * @return 해석에 실패하면 빈 문자열
         */
        static string getProjectRootPath();

        /**
         * @brief 에디터가 쓰는 상태 폴더 <Project>/Saved/Editor 를 반환합니다(없으면 만듭니다). `-gv_editorStateDir=<폴더>` 를 주면 그 폴더입니다.
         * @details 도킹(`imgui.ini` · `windows.ini`) · 이름 붙인 레이아웃 · 노드 캔버스 · 경로 없는 도구 문서 · 환경설정(`EditorPreferences.json` — 테마 포함) · gv 프리셋이
         *          여기 산다. 사람이 고쳐 커밋하는 `Config/` 와 나눈다(언리얼 `Saved/` · 유니티 `UserSettings/` 와 같다).
         * @return 해석에 실패하면 빈 문자열
         */
        static string getEditorStateDirectory();

        /**
         * @brief Saved/Editor 아래 상태 파일의 절대 경로를 반환합니다.
         * @return 해석에 실패하면 빈 문자열
         */
        static string resolveEditorStateFile( const utf8* pFileName );

        /**
         * @brief 호스트 상대 경로를 프로젝트 루트 기준 절대 경로로 만듭니다. 이미 절대 경로면 그대로 둡니다.
         * @details 설정 파일을 다루는 곳(`EditorConfig::loadFromHost` · `saveToHost` · `EditorToolDefaults::loadFromHostPath`)이 함께 씁니다.
         *          "절대 경로인가" 는 `FileUtil::isAbsolutePath` 로 판정합니다(손 판정은 드라이브 문자 검사 같은 것을 빠뜨리기 쉽습니다).
         * @return 프로젝트 루트를 찾지 못하면 구분자만 정규화한 입력을 그대로 반환합니다.
         */
        static string resolveProjectRelativePath( string_view hostRelativePath );
    };
} // namespace sw::editor
