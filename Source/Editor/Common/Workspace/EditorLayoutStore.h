/**
 * @file EditorLayoutStore.h
 * @brief 이름 붙인 에디터 레이아웃(도킹 배치 + 패널 가시성)을 파일로 두는 곳입니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Utility/KeyValueFile.h"

namespace sw::editor
{
    /**
     * @class EditorLayoutStore
     * @brief 레이아웃 하나는 폴더 안의 파일 둘입니다 — `<이름>.imgui.ini`(ImGui 도킹 · 창 배치)와 `<이름>.windows.ini`(패널 가시성).
     * @details 기본 폴더는 `Config/Editor/Layouts/`(사용자 파일이라 git 이 무시합니다). 이름은 영숫자 · 공백 · `_` · `-` 만 받습니다 —
     *          파일 이름이 되므로 경로 문자(`/` · `..`)가 섞이면 안 됩니다. 도킹 배치를 읽고 쓰는 일(ImGui)은 `EditorDockLayout` 이 합니다.
     */
    class EditorLayoutStore
    {
    public:
        /** @brief 에디터 설정 폴더 아래 레이아웃 폴더 이름입니다. */
        static constexpr const utf8* kFolderName = "Layouts";
        /** @brief 도킹 배치 파일의 접미사입니다. */
        static constexpr const utf8* kImguiSuffix = ".imgui.ini";
        /** @brief 패널 가시성 파일의 접미사입니다. */
        static constexpr const utf8* kVisibilitySuffix = ".windows.ini";
        /** @brief 이름의 최대 길이(바이트)입니다. */
        static constexpr uint32 kMaxNameLength = 64;

        /**
         * @brief 사람이 적은 이름을 파일 이름으로 쓸 수 있게 다듬습니다. 앞뒤 공백을 떼고, 받지 않는 문자가 있거나 비었거나 너무 길면 false 입니다.
         */
        [[nodiscard]] static bool sanitizeName( string_view name, string& outName );
        /** @brief 기본 레이아웃 폴더(`Config/Editor/Layouts`)입니다. 에디터 설정 폴더를 찾지 못하면 빈 문자열입니다. */
        static string getDefaultFolder();
        /** @brief 레이아웃의 도킹 배치 파일 경로입니다. */
        static string makeImguiIniPath( string_view folder, string_view name );
        /** @brief 레이아웃의 패널 가시성 파일 경로입니다. */
        static string makeVisibilityPath( string_view folder, string_view name );
        /** @brief 폴더의 레이아웃 이름을 사전순으로 채웁니다(도킹 배치 파일이 있는 것만). */
        static void collectNames( string_view folder, vector<string>& outListName );
        /** @brief 레이아웃 파일 둘을 씁니다. 폴더가 없으면 만듭니다. */
        [[nodiscard]] static bool save( string_view folder, string_view name, string_view imguiIniText, const KeyValueMap& panelVisibility );
        /** @brief 레이아웃 파일 둘을 읽습니다. 가시성 파일은 없어도 됩니다(빈 표). */
        [[nodiscard]] static bool load( string_view folder, string_view name, string& outImguiIniText, KeyValueMap& outPanelVisibility );
        /** @brief 레이아웃 파일 둘을 지웁니다. */
        [[nodiscard]] static bool remove( string_view folder, string_view name );
    };
} // namespace sw::editor
