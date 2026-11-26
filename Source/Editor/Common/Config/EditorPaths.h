/**
 * @file EditorPaths.h
 * @brief 에디터의 프로젝트 루트, 상태 폴더(Saved/Editor), 상태 파일 이름을 정의합니다. UI 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    class EditorPaths
    {
    public:
        // ------------------------------------------------------------------------------
        // 프로젝트 루트와 에디터 상태 폴더(Saved/Editor)
        //    Resource 의 부모가 프로젝트 루트다. 상태 폴더는 없으면 만든다. 사람이 쓰는 설정(Config/Editor)은 config::kDirConfigEditor 를 바로 쓴다
        // ------------------------------------------------------------------------------
        /**
         * @brief 에디터가 쓰는 파일 이름입니다. 이름은 코드가 정하고 설정 파일이 바꾸지 않는다. 설정 파일이 자기 위치와
         *        이웃 파일 이름을 정하면 그 설정 파일을 찾는 경로가 순환한다. 임포트 설정 둘만 `Config/Editor`(사람이 쓴다)에 있고, 나머지는 `Saved/Editor`(앱이 쓴다)에 있다.
         */
        static constexpr const utf8* kImguiINIFileName               = "imgui.ini";
        static constexpr const utf8* kPanelVisibilityFileName        = "windows.json";             ///< 패널 가시성(`EditorPanelVisibility`, 리플렉션 JSON)
        static constexpr const utf8* kAnimationGraphCanvasFileName   = "AnimationGraph.json";      ///< 노드 에디터 캔버스 상태
        static constexpr const utf8* kDialogueGraphCanvasFileName    = "DialogueGraphEditor.json"; ///< 노드 에디터 캔버스 상태
        static constexpr const utf8* kAudioGraphCanvasFileName       = "AudioGraphEditor.json";    ///< 소리 그래프 캔버스 상태(노드 위치는 그래프 데이터에 있다)
        static constexpr const utf8* kMaterialGraphCanvasFileName    = "MaterialGraphEditor.json"; ///< 머티리얼 그래프 캔버스 상태(노드 위치는 그래프 데이터에 있다)
        static constexpr const utf8* kAnimationGraphDocumentFileName = "AnimationGraphData.xml";   ///< 경로가 없는 애니메이션 그래프 문서
        static constexpr const utf8* kDialogueGraphDocumentFileName  = "DialogueGraphData.xml";    ///< 경로가 없는 대화 그래프 문서
        static constexpr const utf8* kSpriteClipDocumentFileName     = "SpriteClip.xml";           ///< 경로가 없는 스프라이트 클립 문서
        static constexpr const utf8* kTextureImportConfigFileName    = "TextureImportConfig.json";
        static constexpr const utf8* kModelImportConfigFileName      = "ModelImportConfig.json";
        /** @brief 에디터 팩(`constant::path::kEditorPack`) 안의 글꼴 폴더 이름입니다. */
        static constexpr const utf8* kFontsFolderName = "fonts";
        /** @brief 에디터 팩의 `fonts/` 안에 있는 아이콘 폰트 파일 이름입니다(`EditorDefaultAssets::_iconGlyphTable` 과 짝). */
        static constexpr const utf8* kIconFontFileName = "sweditoricons.ttf";

        /**
         * @brief 리소스 폴더 id(`game/playground/maps` · `editor/theme`)의 디스크 폴더를 반환합니다. 폴더가 없으면 빈 문자열입니다.
         * @details 해석은 `AssetPaths::toDiskPath` 하나다 - 예전에는 `AssetPaths::findLooseDomainFolder` 가 `Content/<도메인>` 을 따로 이어 붙였다(붙인 디스크 도메인을 몰랐다).
         */
        static DiskPath findContentFolder( string_view folderID );
        /**
         * @brief 에디터 범위의 리소스 경로 해석(`AssetPaths` — id ↔ 디스크, 붙인 도메인)입니다. 없으면 멈춘다.
         * @details 패널 View 가 서비스 창구(`EditorService.h`)를 보지 않고 경로를 풀게 여기를 지난다(`docs/agents/EditorUI.md` MVVM 계약). 경로 해석은 읽기만 한다.
         */
        static const AssetPaths& getAssetPaths();
        /** @brief 씬 파일 대화상자가 처음 여는 폴더(활성 게임 팩의 `maps`)입니다. 없으면 빈 문자열이고 대화상자가 정한다. */
        static string getSceneDialogFolderPath();

        /**
         * @brief 에디터가 쓰는 상태 폴더 <Project>/Saved/Editor 를 반환합니다. 없으면 만듭니다. `-gv_editorStateDir=<폴더>` 를 주면 그 폴더를 씁니다.
         * @details 도킹(`imgui.ini`, `windows.json`), 이름 붙인 레이아웃, 노드 캔버스, 경로가 없는 도구 문서, 환경설정(테마 포함 `EditorPreferences.json`), gv 프리셋이
         *          여기에 있다. 사람이 고쳐서 커밋하는 `Config/` 와 나눈다. 언리얼의 `Saved/`, 유니티의 `UserSettings/` 와 같다.
         * @return 디스크 경로 형식입니다(I/O 에 그대로 넘기고 `join` 으로 잇는다).
         */
        [[nodiscard]] static AbsolutePath getEditorStateDirectory();

        /**
         * @brief Saved/Editor 아래 상태 파일의 절대 경로를 반환합니다.
         * @return 디스크 경로 형식입니다. @p pFileName 이 비면 빈 경로입니다(그 경로의 I/O 는 실패한다).
         */
        [[nodiscard]] static AbsolutePath resolveEditorStateFile( const utf8* pFileName );
    };
} // namespace sw::editor
