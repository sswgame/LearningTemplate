/**
 * @file EditorDockLayout.h
 * @brief 에디터 도크스페이스 · 기본 레이아웃 · imgui.ini / windows.ini 저장과 복원입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Utility/Format/KeyValueFile.h"

namespace sw::editor
{
    /** @brief 메인 도크 레이아웃과 패널 가시성 저장 */
    class EditorDockLayout
    {
    public:
        EditorDockLayout();

        /** @brief Config/Editor 아래 imgui.ini / windows.ini 경로를 해석합니다. */
        void initializePersistencePaths();
        /** @brief 해석된 imgui.ini 경로를 ImGui IO에 연결합니다. */
        void applyIniFilename() const;
        /** @brief windows.ini에서 패널 열림 상태를 복원합니다. */
        void loadPanelVisibility();
        /** @brief 패널 가시성과 ImGui 도크 레이아웃을 저장합니다. */
        void save();
        /** @brief 메인 도크스페이스를 열고, 비어 있으면 기본 레이아웃을 적용합니다. */
        void beginDockspace();
        /** @brief 다음 프레임에 기본 도크 레이아웃을 다시 적용합니다. */
        void requestResetDefault();

        /**
         * @brief 지금 도킹 배치와 패널 가시성을 이름 붙인 레이아웃으로 저장합니다(`EditorLayoutStore`).
         * @param folder 비우면 기본 폴더(`Config/Editor/Layouts`)
         */
        [[nodiscard]] bool saveNamedLayout( string_view name, string_view folder = {} );
        /**
         * @brief 이름 붙인 레이아웃을 다음 프레임 시작에 적용하도록 요청합니다. 파일이 없으면 false 입니다.
         * @details ImGui 설정은 프레임 밖(`NewFrame` 앞)에서 읽어야 이미 있는 창 · 도킹 노드에 적용되므로 `applyPendingNamedLayout` 이 합니다.
         */
        [[nodiscard]] bool requestLoadNamedLayout( string_view name, string_view folder = {} );
        /** @brief 요청된 레이아웃을 적용합니다. `ImGui::NewFrame` 앞에서 부릅니다. */
        void applyPendingNamedLayout();

        /**
         * @brief `-gv_editorOpenPanel` 이 시작할 때 열 패널을 정하고 있는지 반환합니다(`all` 또는 패널 id).
         * @details 그 동안은 저장된 레이아웃을 읽지도 쓰지도 않습니다. 한 번 준 스위치가 다음 실행의 레이아웃으로 굳지 않게 합니다.
         */
        static bool isPanelOverrideActive();
        /** @brief `-gv_editorOpenPanel=all` 로 등록된 패널을 전부 떠 있는 창으로 여는 중인지 반환합니다. */
        static bool isOpeningAllPanels();

    private:
        void applyDefaultDockLayout( uint32 dockspaceId );

        string                 _imguiIniPath;
        string                 _windowsIniPath;
        string                 _pendingLayoutIni; ///< 다음 프레임 시작에 적용할 레이아웃의 ImGui 설정 글
        KeyValueMap            _pendingLayoutVisibility;
        uint8                  _bLayoutPending : 1;
        uint8                  _bApplied       : 1;
        [[maybe_unused]] uint8 _reserved       : 6;
    };
} // namespace sw::editor
