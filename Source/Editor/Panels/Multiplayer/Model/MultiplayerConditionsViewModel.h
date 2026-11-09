/**
 * @file MultiplayerConditionsViewModel.h
 * @brief Multiplayer 창 Conditions 탭의 ViewModel — 대상 프로세스의 회선 패널 값과 프리셋 단추를 모으고(`gather`), 회선 · 프리셋 · 끊기 · 재접속 · 스파이크 · 통계 명령을 받습니다. ImGui 를 모릅니다.
 * @details 명령은 그 프로세스의 표준 입력에 개발 명령(`net.emulation` …)으로 간다(서버 디버깅 S4, 언리얼 Net Emulation · 유니티 Network Simulator).
 *          패널 값은 프로세스마다 따로 들고, 세션이 새로 서면(포트가 바뀌면) 지운다 — 자식은 `-gv_netEmulation*` 기본값(꺼짐)으로 다시 뜬다.
 *          자식 프로세스의 회선이지 씬 문서가 아니라 되돌리기에 남지 않는다. 감시 값(`Editor.NetPlayResentMessages` …)과 개발 명령(`netplay.command` · `stats` · `waitResent`)도 이 .cpp 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Play/EditorNetConditions.h"

#include "Engine/Config/NetEmulationPresetTable.h"

namespace sw::editor
{
    /** @brief Conditions 탭이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerConditionsViewState
    {
        vector<string>        _listPresetLabel{};              ///< 프리셋 단추 이름(엔진 프리셋 표의 순서 — `applyPreset` 의 번호)
        EditorNetLinkSettings _up{};                           ///< 대상의 보내는 쪽 패널 값
        EditorNetLinkSettings _down{};                         ///< 대상의 받는 쪽 패널 값
        uint8                 _bActive{ SW_FALSE };            ///< 세션이 돌고 대상이 있다 — 아니면 안내 한 줄만 그린다
        uint8                 _bPresetTableFailed{ SW_FALSE }; ///< 프리셋 표를 읽지 못했다 — 단추 자리에 그렇다고 적는다
    };

    /**
     * @class MultiplayerConditionsViewModel
     * @brief Conditions 탭의 ViewModel 입니다. 파일 머리말 참고. 명령의 @p bApplyToAll 이 참이면 대상 대신 모든 프로세스에 보냅니다.
     */
    class MultiplayerConditionsViewModel
    {
    public:
        MultiplayerConditionsViewModel();

        /** @brief 대상 @p targetIndex 의 패널 값을 채웁니다. 세션이 돌지 않거나 대상이 없으면 패널 값을 지우고 `_bActive` 가 거짓입니다. 프리셋 표는 처음 쓸 때 한 번 읽습니다. */
        void gather( int32 targetIndex, MultiplayerConditionsViewState& outState );
        /** @brief 대상의 패널 값을 둡니다(슬라이더를 끄는 동안 — 보내지 않는다). */
        void setLink( int32 targetIndex, const EditorNetLinkSettings& up, const EditorNetLinkSettings& down );
        /** @brief 대상의 한 방향 패널 값을 그 프로세스(들)에 보냅니다(슬라이더를 놓았을 때). */
        void sendLink( EditorNetLinkDirection direction, int32 targetIndex, bool bApplyToAll );
        /** @brief 프리셋 @p presetIndex 를 두 방향 패널 값으로 두고 보냅니다(왕복이 그 회선이 된다). */
        void applyPreset( size_t presetIndex, int32 targetIndex, bool bApplyToAll );
        void disconnect( int32 targetIndex, bool bApplyToAll );
        void reconnect( int32 targetIndex, bool bApplyToAll );
        /** @brief @p seconds 동안 보내고 받는 것을 모두 버립니다. */
        void spike( float32 seconds, int32 targetIndex, bool bApplyToAll );
        /** @brief `net.emulation stats` 를 물어 보고를 새로 받습니다(답은 세션의 프로세스 값에 온다). */
        void refreshStats( int32 targetIndex, bool bApplyToAll );

    private:
        /** @brief 프로세스 하나의 패널 값입니다. */
        struct ProcessLink
        {
            EditorNetLinkSettings _up{};
            EditorNetLinkSettings _down{};
        };

        /** @brief 대상(또는 모두)에 명령 한 줄을 보냅니다. 끝난 프로세스는 경고로 알립니다. */
        void sendToTargets( int32 targetIndex, bool bApplyToAll, const string& command );
        /** @brief 대상의 패널 값입니다. 번호가 없으면 nullptr 입니다(gather 가 세션 프로세스 수로 맞춘다). */
        ProcessLink* findLink( int32 targetIndex );

    private:
        vector<ProcessLink>     _listLink;
        NetEmulationPresetTable _presetTable;
        uint16                  _sessionPort;               ///< `_listLink` 가 맞춰진 세션(포트) — 바뀌면 지운다
        uint8                   _bPresetTableAttempted : 1; ///< 표를 읽어 봤다(실패도 한 번만 알린다)
        uint8                   _bPresetTableFailed    : 1;
    };
} // namespace sw::editor
