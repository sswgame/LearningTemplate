/**
 * @file MultiplayerSessionViewModel.h
 * @brief Multiplayer 창의 세션 ViewModel — 멀티 프로세스 재생 세션(`EditorNetPlaySession`)의 값을 모으고(`gather`) 구성 · 프리셋 · Play · Stop · 서버 명령을 받습니다. ImGui 를 모릅니다.
 * @details 창(`MultiplayerPanel`)이 하나를 들고 프레임 첫머리에 모은 값과 함께 탭 문맥(`MultiplayerTabContext`)으로 모든 탭에 넘긴다(`docs/agents/EditorUI.md` MVVM 계약).
 *          세션은 에디터 서브시스템 하나라 툴바와 같은 세션을 본다. 구성 · 프리셋은 사용자 파일이고 씬 문서가 아니라 되돌리기에 남지 않는다.
 *          세션 감시 값(`Editor.NetPlay*`)과 개발 명령(`netplay.start` · `stop` · `wait` · `server`)도 이 .cpp 에 있다 — 창이 닫혀 있어도 자동화가 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySettings.h"

namespace sw::editor
{
    class EditorNetPlaySession;

    /** @brief 프로세스 하나의 값입니다(대상 고르기 · 프로세스 표 · Conditions 보고). */
    struct MultiplayerProcessRow
    {
        string             _label{};
        string             _lastLine{};
        NetEmulationReport _emulationReport{};         ///< 마지막으로 받은 `net.emulation stats` 의 답
        uint32             _emulationReportCount{ 0 }; ///< 받은 보고 수 — 0 이면 아직 답이 없다
        int32              _processID{ 0 };
        int32              _exitCode{ 0 };
        uint8              _bRunning{ SW_FALSE };
        uint8              _bConnected{ SW_FALSE }; ///< 클라이언트가 서버에 붙어 있다
    };

    /** @brief 모은 로그 한 줄입니다(Session 탭의 출처 거르기를 거친 것). */
    struct MultiplayerLogRow
    {
        string _text{};
        int32  _processIndex{ 0 }; ///< `MultiplayerSessionViewState::_listProcess` 의 번호
    };

    /** @brief 세션 하나가 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerSessionViewState
    {
        vector<MultiplayerProcessRow> _listProcess{};
        vector<string>                _listPresetName{};
        EditorNetPlaySettings         _settings{};           ///< 다음 세션의 구성 사본 — 고친 값은 `setSettings` 로 돌려준다
        string                        _selectedPresetName{}; ///< 고른 프리셋. 비어 있으면 고른 것이 없다
        string                        _mergedTracePath{};    ///< 지난 멈춤이 합친 트레이스. 없으면 비어 있다
        uint16                        _port{ 0 };
        uint16                        _debugChannelPort{ 0 }; ///< 전용 서버의 디버그 채널 포트. 채널이 없으면 0
        uint8                         _bRunning{ SW_FALSE };
        uint8                         _bStopping{ SW_FALSE }; ///< 멈춘 세션이 아직 프로세스를 거두는 중이다(이 동안 Play 는 거절된다)
    };

    /**
     * @class MultiplayerSessionViewModel
     * @brief Multiplayer 창의 세션 ViewModel 입니다. 파일 머리말 참고. 상태가 없고 세션을 부를 때마다 찾습니다.
     * @details 확장 모듈의 탭(`SW_EDITOR_MULTIPLAYER_TAB`)도 탭 문맥으로 이 명령을 받으므로 EditorModule 밖으로 내보낸다.
     */
    class SW_EDITOR_API MultiplayerSessionViewModel
    {
    public:
        /** @brief 서버(전용 서버 · 리슨 호스트)의 프로세스 번호입니다. `EditorNetPlaySession::kServerProcessIndex` 와 같다(.cpp 의 static_assert 가 맞춘다). */
        static constexpr int32 kServerProcessIndex = 0;

        /** @brief 에디터 문맥의 세션입니다. 문맥이 없으면 nullptr 입니다(감시 값 · 개발 명령 — 에디터 밖에서도 불린다). */
        static EditorNetPlaySession* findSession();
        /** @brief 그리기 경로의 세션입니다. 탭은 에디터 안에서만 그려지므로 문맥이 없으면 버그다. */
        static EditorNetPlaySession& getSession();

        /** @brief 창이 그릴 세션 값을 채웁니다(@p outState 의 목록은 저장 공간을 다시 쓴다). */
        void gather( MultiplayerSessionViewState& outState ) const;
        /** @brief 모은 로그를 @p outListRow 에 다시 씁니다. @p processIndex 가 0 이상이면 그 프로세스의 줄만입니다(Session 탭이 보일 때만 부른다). */
        void gatherLog( int32 processIndex, vector<MultiplayerLogRow>& outListRow ) const;

        /** @brief 다음 세션의 구성을 @p settings 로 둡니다(바뀐 구성은 다음 Play 에 쓰인다). */
        void setSettings( const EditorNetPlaySettings& settings );
        /** @brief 프리셋 @p name 을 고르고 그 구성을 다음 세션의 구성으로 복사합니다. 고르기를 파일에 쓰지 못했으면(오류는 저장소가 남겼다) 구성도 그대로 둡니다. */
        void selectPreset( const string& name );
        /** @brief 지금 구성을 @p name 으로 저장합니다. 실패 이유는 저장소가 오류로 남깁니다. 저장했으면 true 입니다. */
        bool savePreset( const string& name );
        /** @brief 프리셋 @p name 을 지웁니다. 실패 이유는 저장소가 오류로 남깁니다. */
        void removePreset( const string& name );

        /** @brief 지금 구성으로 세션을 시작합니다. 실패 이유는 세션이 로그에 남깁니다. */
        void start();
        /** @brief 모두에 정상 종료를 알리고 멈춤을 시작합니다(기다리지 않는다). 합친 트레이스는 Profiler 창의 프레임 단계가 연다. */
        void requestStop();
        /** @brief 서버 표준 입력에 한 줄을 보냅니다(`pause` · `step N` · `resume`). 답은 서버 로그 줄로 온다. 보냈으면 true 입니다. */
        bool sendServerCommand( string_view command );
        /** @brief 프로세스 @p processIndex 의 표준 입력에 한 줄을 보냅니다. 끝났거나 번호가 없으면 false 입니다. */
        bool sendProcessCommand( int32 processIndex, string_view command );
    };
} // namespace sw::editor
