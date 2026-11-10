/**
 * @file MultiplayerProfilerViewModel.h
 * @brief Multiplayer 창 Profiler 탭의 ViewModel — 대상 프로세스의 시계열 · 메시지 종류별 비중 · 복제 셈을 모으고(`gather`), 수집(`update`) · 지우기 명령을 받습니다. ImGui 를 모릅니다.
 * @details 시계열(`EditorNetProfilerHistory`)은 에디터 범위 상태 저장소가 소유해 창을 닫았다 열어도 남는다. 수집은 탭이 보일 때(또는 "Record while hidden")만 0.5 초마다
 *          자식에 `net.stats` 를 묻는다(서버 디버깅 S5). 수집 기록이지 씬 문서가 아니라 되돌리기에 남지 않는다.
 *          감시 값(`Editor.NetPlayProfiler*`)과 개발 명령 `netplay.profile` 도 이 .cpp 에 있다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetStatsReport.h"

#include "Editor/Common/Workspace/NetPlay/EditorNetProfilerSample.h"

namespace sw::editor
{
    /** @brief 그래프 한 장(채널 하나)의 값입니다. */
    struct MultiplayerProfilerGraph
    {
        vector<float32> _listValue{}; ///< 오래된 것부터
        float32         _latest{ 0.0f };
        float32         _peak{ 0.0f };
    };

    /** @brief Profiler 탭이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerProfilerViewState
    {
        MultiplayerProfilerGraph     _arrGraph[static_cast<size_t>( EditorNetProfilerChannel::Count )]{}; ///< 채널 순서
        vector<EditorNetKindShare>   _listKindShare{};                                                    ///< 바이트가 큰 순서
        vector<EditorNetReplicaRate> _listReplicaRate{};                                                  ///< 초당 바이트가 큰 순서
        int32                        _connectionCount{ 0 };                                               ///< 대상의 마지막 보고의 연결 수
        int32                        _sampleCount{ 0 };                                                   ///< 대상 시계열의 표본 수
        uint8                        _bHasData{ SW_FALSE };                                               ///< 대상의 표본이 있다 — 아니면 안내 한 줄만 그린다
    };

    /**
     * @class MultiplayerProfilerViewModel
     * @brief Profiler 탭의 ViewModel 입니다. 파일 머리말 참고.
     */
    class MultiplayerProfilerViewModel
    {
    public:
        MultiplayerProfilerViewModel();

        /** @brief 간격이 지났으면 돌고 있는 프로세스마다 `net.stats` 를 묻고, 새로 온 보고를 표본으로 넣습니다. 탭이 보이는 프레임(또는 숨김 기록)마다 부릅니다. */
        void update();
        /**
         * @brief 대상 @p targetIndex 의 값을 채웁니다. 비중은 @p bAllProcessKinds 면 모든 프로세스의 합, @p bSentKinds 면 보낸 메시지(아니면 받은 메시지)입니다.
         */
        void gather( optional<int32> targetIndex, bool bAllProcessKinds, bool bSentKinds, MultiplayerProfilerViewState& outState );
        /** @brief 모든 프로세스의 시계열을 지웁니다. */
        void clear();

    private:
        vector<NetStatsKindEntry> _listKindEntryScratch;
    };
} // namespace sw::editor
