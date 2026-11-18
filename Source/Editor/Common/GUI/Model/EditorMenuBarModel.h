/**
 * @file EditorMenuBarModel.h
 * @brief 메인 메뉴 막대(`EditorMenuBar`)의 ViewModel — 작업 공간 요청(패널 · 문서 열기, 씬 열기 · 미저장 확인), 이름 붙인 레이아웃 파일, 빌드 결과 알림, RHI 이름. ImGui 를 모릅니다.
 * @details 메뉴 막대 View 는 작업 공간 · 씬 헤더를 보지 않는다(`docs/agents/EditorUI.md` MVVM). 메뉴 막대가 프레임 사이에 기억하는 보기 값은 `EditorMenuBarUIState` 로 이 모델이 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorPanelOpenRequest.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "ModuleAPI/Service/IModuleCompiler.h"

namespace sw::editor
{
    /** @brief 메뉴 막대가 프레임 사이에 기억하는 보기 값입니다(기록하지 않는다). */
    struct EditorMenuBarUIState
    {
        fixed_string<constant::buffer::kMax64> _layoutName{};                               ///< 이름 붙인 레이아웃 저장 칸의 글
        BuildState                             _lastObservedBuildState{ BuildState::Idle }; ///< 지난 프레임에 본 빌드 상태(컴파일이 끝나는 순간을 잡는다)
        float32                                _statusAreaWidth{ 0.0f };                    ///< 지난 프레임에 측정한 상태 영역 너비(0 = 아직 모름)
        bool                                   _bShowThemeSettings{ false };                ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };

    /**
     * @struct EditorMenuBarModel
     * @brief 메뉴 막대의 명령과 값 모음입니다.
     */
    struct SW_EDITOR_API EditorMenuBarModel
    {
        /** @brief 지금 에디터 범위의 메뉴 막대 보기 값입니다(상태 저장소가 든다). */
        [[nodiscard]] static EditorMenuBarUIState& editUIState();

        /** @brief 종류 @p kind 의 문서를 여는 도구 패널을 다음 프레임에 엽니다. */
        static void requestOpenDocumentPanel( const EditorAssetType& kind );
        /** @brief 쌓인 패널 열기 요청 하나를 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] static bool consumeOpenPanel( EditorPanelOpenRequest& outRequest );
        /** @brief 패널 @p panelID 를 띄우는 요청을 다음 프레임으로 넘깁니다(도킹을 푸는 일이 다음 NewFrame 에 끝난다). */
        static void requestFloatPanelByID( string_view panelID );

        /** @brief 씬 세대가 바뀐 뒤를 맞춥니다(UI 쪽 선택 · 기록을 버린다). 쌓인 씬 열기 요청은 게임 단계가 연다(`EditorGameStepOrder::kSceneSession`). */
        static void processSceneRequests();
        /** @brief 미저장 확인을 기다리는 씬 동작입니다. 없으면 `None` 입니다. */
        [[nodiscard]] static EditorPendingSceneAction getPendingSceneAction();
        /** @brief 미저장 확인 창의 선택을 요청합니다 — 같은 프레임의 게임 단계(`sceneSession`)가 저장 · 버리기 · 이어서 할 동작을 한다. */
        static void requestUnsavedSceneChoice( EditorUnsavedChoice choice );

        /** @brief 저장한 이름 붙인 레이아웃 이름들입니다(먼저 비운다). */
        static void collectLayoutNames( vector<string>& outListName );
        /** @brief 이름 붙인 레이아웃 파일 하나를 지웁니다. 못 지우면 false 입니다. */
        [[nodiscard]] static bool removeLayout( string_view name );

        /** @brief 모듈 컴파일러입니다. Shipping 처럼 컴파일러가 없는 구성이면 nullptr 입니다. */
        [[nodiscard]] static IModuleCompiler* findModuleCompiler();
        /** @brief 컴파일이 끝나는 순간(Compiling → Success / Failed)을 잡아 알림을 띄웁니다. 프레임마다 부른다. */
        static void notifyLiveCodingResult( IModuleCompiler& compiler );
        /** @brief 쓰는 RHI 의 이름입니다. 장치가 없으면 "n/a" 입니다. */
        [[nodiscard]] static const utf8* getRHIBackendName();
    };
} // namespace sw::editor
