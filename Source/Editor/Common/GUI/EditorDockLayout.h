/**
 * @file EditorDockLayout.h
 * @brief 에디터의 도크스페이스, 기본 레이아웃, imgui.ini 와 windows.ini 의 저장과 복원을 담당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/File/KeyValueFile.h"

namespace sw::editor
{
    /** @brief 메인 도크 레이아웃과 패널 가시성을 저장합니다. */
    class EditorDockLayout
    {
    public:
        EditorDockLayout();

        /** @brief Saved/Editor 아래의 imgui.ini 와 windows.ini 경로를 정합니다. */
        void initializePersistencePaths();
        /** @brief 정한 imgui.ini 경로를 ImGui IO 에 연결합니다. */
        void applyIniFilename() const;
        /** @brief windows.ini 에서 패널 열림 상태를 복원합니다. */
        void loadPanelVisibility();
        /** @brief 패널 가시성과 ImGui 도크 레이아웃을 저장합니다. */
        void save();
        /** @brief 메인 도크스페이스를 열고, 비어 있으면 기본 레이아웃을 적용합니다. */
        void beginDockspace();
        /** @brief 다음 프레임에 기본 도크 레이아웃을 다시 적용합니다. */
        void requestResetDefault();
        /**
         * @brief 기본 배치를 막 만들었으면 가운데 영역의 Scene 탭을 앞으로 가져옵니다. 패널을 모두 그린 뒤 프레임마다 호출합니다.
         * @details ImGui 는 새로 붙은 탭 중 마지막 것을 고르고, 처음 나타난 창에 포커스를 준다. 그래서 열린 채 시작하는 도구 패널(Prefab Overrides)이
         *          같은 영역에 붙으면 그 탭이 앞에 서고 씬 뷰는 그려지지 않는다. Scene 탭이 실제로 선택될 때까지 최대 몇 프레임 동안 포커스를 준다.
         */
        void updateDefaultTabSelection();

        /**
         * @brief 현재 도킹 배치와 패널 가시성을 이름 붙인 레이아웃으로 저장합니다(`EditorLayoutStore`).
         * @param folder 비우면 기본 폴더(`Saved/Editor/Layouts`)를 씁니다.
         */
        [[nodiscard]] bool saveNamedLayout( string_view name, string_view folder = {} );
        /**
         * @brief 이름 붙인 레이아웃을 다음 프레임 시작에 적용하도록 요청합니다. 파일이 없으면 false 입니다.
         * @details ImGui 설정은 프레임 밖(`NewFrame` 앞)에서 읽어야 이미 있는 창과 도킹 노드에 적용되므로 `applyPendingNamedLayout` 이 한다.
         */
        [[nodiscard]] bool requestLoadNamedLayout( string_view name, string_view folder = {} );
        /** @brief 요청된 레이아웃을 적용합니다. `ImGui::NewFrame` 앞에서 호출합니다. */
        void applyPendingNamedLayout();

        /**
         * @brief `-gv_editorOpenPanel` 이 시작할 때 열 패널을 정하고 있거나 셸이 덮어쓰기를 요청했으면 true 입니다. 값은 `all` 이거나 패널 id 입니다.
         * @details 그동안은 저장된 레이아웃을 읽지도 쓰지도 않는다. 한 번 준 스위치가 다음 실행의 레이아웃으로 굳지 않게 하기 위해서다.
         */
        static bool isPanelOverrideActive();
        /** @brief 셸이 저장된 레이아웃을 쓰지 않도록 요청합니다. 자체 테스트를 실행할 때 씁니다. 도킹은 자체 테스트를 모르므로 셸이 시작할 때 알립니다. */
        static void setPanelOverrideRequested( bool bRequested );
        /** @brief `-gv_editorOpenPanel=all` 로 등록된 패널을 전부 떠 있는 창으로 여는 중이면 true 입니다. */
        static bool isOpeningAllPanels();

    private:
        void applyDefaultDockLayout( uint32 dockspaceID );
        /**
         * @brief 메인 뷰포트 크기가 바뀌었으면 도크 트리의 기준 크기(SizeRef)를 같은 비율로 맞춥니다. `DockSpaceOverViewport` 앞에서 호출합니다.
         * @details ImGui 는 중앙 노드 옆 노드에 마지막 픽셀 크기(SizeRef)를 그대로 주고 나머지를 중앙에 준다. 그대로 두면 창을 줄일 때 옆 패널은
         *          그대로이고 중앙(게임 뷰) 쪽이 최소 폭으로 눌린다.
         */
        void scaleDockSizeToViewport( uint32 dockspaceID );

        string                 _imguiIniPath;
        string                 _windowsIniPath;
        string                 _pendingLayoutIni; ///< 다음 프레임 시작에 적용할 레이아웃의 ImGui 설정 문자열
        KeyValueMap            _pendingLayoutVisibility;
        float32                _lastDockspaceWidth;  ///< 마지막으로 비율을 맞춘 도크스페이스 크기다. 0 이면 다음 프레임이 저장된 크기에서 시작한다
        float32                _lastDockspaceHeight; ///< 위와 같다(세로)
        uint8                  _frontTabFrameCount;  ///< 기본 배치 뒤에 Scene 탭을 앞으로 가져오려고 포커스를 준 프레임 수
        uint8                  _bLayoutPending   : 1;
        uint8                  _bApplied         : 1;
        uint8                  _bResetDefault    : 1; ///< 다음 적용이 저장된 배치가 있어도 기본 배치로 다시 만든다(Reset Default Layout)
        uint8                  _bFrontTabPending : 1; ///< 기본 배치를 만들었고 Scene 탭이 아직 선택되지 않았다(`updateDefaultTabSelection`)
        [[maybe_unused]] uint8 _reserved         : 4;
    };
} // namespace sw::editor
