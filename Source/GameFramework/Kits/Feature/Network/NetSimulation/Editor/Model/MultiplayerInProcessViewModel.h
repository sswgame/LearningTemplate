/**
 * @file MultiplayerInProcessViewModel.h
 * @brief Multiplayer 창 In process 탭의 ViewModel — 한 프로세스 재생(`EditorNetSimSession`)의 게임 목록 · 진행 상태 · 월드를 모으고(`gather`), 시작 · 멈춤 · 진행 · 입력 초점 명령을 받습니다. ImGui 를 모릅니다.
 * @details 세션은 이 확장의 에디터 서브시스템이고 탭이 그리는 프레임에 진행한다(`advance` — 하니스는 탭이 보이는 프레임에만 돈다, ServerDebug S8).
 *          재생 세션이지 씬 문서가 아니라 되돌리기에 남지 않는다(`docs/agents/EditorUI.md` MVVM 계약).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    class EditorPreviewViewport;

    /** @brief In process 탭이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerInProcessViewState
    {
        vector<string> _listGameName{};   ///< 키트 세션을 구현한 게임(`NetSimGameRegistry`). 비면 시작할 수 없다
        vector<int32>  _listWorldIndex{}; ///< 그릴 월드(0 = 서버)
        string         _lastError{};      ///< 마지막 시작 실패의 이유
        uint64         _tick{ 0 };        ///< 하니스 틱
        int32          _inputFocus{ 0 };  ///< 입력을 받는 월드
        uint8          _bRunning{ SW_FALSE };
    };

    /**
     * @class MultiplayerInProcessViewModel
     * @brief In process 탭의 ViewModel 입니다. 파일 머리말 참고.
     */
    class MultiplayerInProcessViewModel
    {
    public:
        /** @brief 처음 붙일 클라이언트 수의 상한입니다. `EditorNetSimSession::kMaxClients` 와 같다(.cpp 의 static_assert 가 맞춘다). */
        static constexpr int32 kMaxClients = 4;

        MultiplayerInProcessViewModel();

        /** @brief 탭이 그릴 값을 채웁니다. 월드 목록은 `advance` 뒤에 `gatherWorlds` 로 다시 모읍니다. */
        void gather( MultiplayerInProcessViewState& outState ) const;
        /** @brief 진행한 뒤의 월드 목록과 입력 초점만 다시 모읍니다. */
        void gatherWorlds( MultiplayerInProcessViewState& outState ) const;

        /** @brief 게임 @p gameIndex 를 클라이언트 @p clientCount 개로 시작하고 첫 클라이언트에 입력 초점을 줍니다. 실패하면 이유를 `_lastError` 로 보입니다. */
        void start( int32 gameIndex, int32 clientCount );
        void stop();
        /** @brief 프레임 시간 @p frameSeconds 만큼 하니스를 진행합니다(고정 스텝). */
        void advance( float64 frameSeconds );
        /** @brief 이 창이 게임 입력 표면으로 포커스를 받았음을 알립니다(키보드를 초점 월드가 받는다 — 게임 뷰 패널과 같다). */
        void recordGameInputFocused();
        /** @brief 월드 @p worldIndex 에 입력 초점을 옮깁니다. */
        void setInputFocus( int32 worldIndex );
        /** @brief 월드 @p worldIndex 의 미리보기 뷰포트(GUI 위젯 — 세션이 들고 있다)입니다. 아직 없으면 nullptr 입니다. */
        EditorPreviewViewport* findViewport( int32 worldIndex );

    private:
        string _lastError;
    };
} // namespace sw::editor
