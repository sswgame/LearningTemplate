/**
 * @file MultiplayerSyncViewModel.h
 * @brief Multiplayer 창 Sync 탭의 ViewModel — 상태 해시 이력 두 칸(A · B)과 비교 결과 · 클라이언트 원천을 모으고(`gather`), 읽기 · 저장 · 비교 · 되감기 명령을 받습니다. ImGui 를 모릅니다.
 * @details 계산은 `EditorStateHashCompare`(에디터 범위 상태 저장소가 소유 — 창을 닫아도 남는다), 클라이언트 답 기다리기는 `MultiplayerSyncModel` 이 한다(서버 디버깅 S7).
 *          읽기는 누를 때만 한다(채널 요청은 답을 기다린다). 비교 기록 · 파일 · 서버 진행이지 씬 문서가 아니라 되돌리기에 남지 않는다.
 *          감시 값(`Editor.NetPlaySync*`) · 개발 명령 `netplay.sync` · 환경설정 섹션 `EditorSyncPreferences` 도 이 .cpp 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    class MultiplayerSyncModel;
    struct MultiplayerSyncViewState;

    /**
     * @class MultiplayerSyncViewModel
     * @brief Sync 탭의 ViewModel 입니다. 파일 머리말 참고. 실패는 경고 로그로 알리거나(읽기 · 쓰기) 탭의 오류 줄(`_error`)로 보입니다.
     */
    class MultiplayerSyncViewModel
    {
    public:
        /** @brief 이력 칸 수(A · B)입니다. `EditorStateHashCompare::kSourceCount` 와 같다(.cpp 의 static_assert 가 맞춘다). */
        static constexpr int32 kSourceCount = 2;

        MultiplayerSyncViewModel();
        ~MultiplayerSyncViewModel();

        MultiplayerSyncViewModel( const MultiplayerSyncViewModel& )            = delete;
        MultiplayerSyncViewModel& operator=( const MultiplayerSyncViewModel& ) = delete;

        /** @brief 온 클라이언트 답을 칸에 담고, 시한을 넘긴 질의를 오류로 끝냅니다. 탭이 보이는 프레임마다 `gather` 앞에 부릅니다. */
        void poll();
        void gather( MultiplayerSyncViewState& outState ) const;

        /** @brief 읽을 해시 줄 이름을 정합니다(씬 줄 `scene`, 또는 키트가 더한 줄). 형식이 틀리면 오류 줄에 이유가 보입니다. */
        void setHashChannel( string_view channelName );
        /** @brief 세션이 띄운 전용 서버의 이력을 칸에 담습니다. */
        void loadSessionServer( int32 sourceIndex );
        /** @brief 세션 클라이언트 @p processIndex 에 이력을 묻습니다. 답이 오면 `poll` 이 칸에 담습니다. */
        void requestClientHistory( int32 sourceIndex, int32 processIndex );
        /** @brief @p address(`127.0.0.1:<포트>` 만) · @p token 으로 붙은 다른 서버의 이력을 칸에 담습니다. */
        void readServer( int32 sourceIndex, string_view address, string_view token );
        /** @brief 사람이 친 경로 @p typedPath(상대면 이 실행의 산출물 폴더 아래)의 이력 파일을 칸에 담습니다. */
        void loadFile( int32 sourceIndex, string_view typedPath );
        /** @brief 칸의 이력을 @p typedPath 에 씁니다. */
        void saveFile( int32 sourceIndex, string_view typedPath );
        void clearSource( int32 sourceIndex );
        /** @brief 두 칸을 비교합니다. 한 칸이라도 비어 있으면 경고로 알립니다. */
        void compare();
        /** @brief "Which objects differ" — 두 칸의 현재 오브젝트 해시를 다시 받아 다른 오브젝트를 짚습니다(클라이언트 칸은 답을 기다린다). */
        void startObjectCompare();
        /** @brief `.swintent` @p intentPath 로 멈춘 재생 서버를 띄웁니다(세션이 없을 때). */
        void startReplayServer( const string& intentPath );
        /** @brief 떠 있는 서버를 @p tickCount 틱 진행합니다. */
        void stepServer( int32 tickCount );

    private:
        unique_ptr<MultiplayerSyncModel> _pModel; ///< 클라이언트 답 기다리기 · 질의 오류
    };

    /** @brief 비교할 이력 한 칸의 값입니다. */
    struct MultiplayerSyncSourceRow
    {
        string _label{};        ///< 어디서 읽었는가(주소 · 파일 경로)
        uint64 _firstTick{ 0 }; ///< `_sampleCount` 가 0 이면 뜻이 없다
        uint64 _lastTick{ 0 };
        uint32 _sampleCount{ 0 }; ///< 0 이면 빈 칸
    };

    /** @brief 세션 클라이언트 원천 하나(서버 0 번은 빠진다)입니다. */
    struct MultiplayerSyncClientRow
    {
        string _label{};
        int32  _processIndex{ 0 };
        uint8  _bRunning{ SW_FALSE };
        uint8  _arrWaiting[MultiplayerSyncViewModel::kSourceCount]{}; ///< 칸(A · B)마다 이 프로세스의 이력 답을 기다리는 중이다
    };

    /** @brief Sync 탭이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerSyncViewState
    {
        MultiplayerSyncSourceRow         _arrSource[MultiplayerSyncViewModel::kSourceCount]{}; ///< 칸 A · B
        vector<MultiplayerSyncClientRow> _listClient{};                                        ///< 세션이 돌 때만 채운다
        vector<string>                   _listDifferingObject{};                               ///< 마지막 "Which objects differ" 가 짚은 오브젝트(이름 순)
        string                           _hashChannel{};                                       ///< 읽을 해시 줄 이름
        string                           _comparisonText{};                                    ///< 지금 칸으로 한 비교 결과 한 줄. 낡았거나 없으면 비어 있다
        string                           _error{};                                             ///< 마지막 질의 오류(시간 초과 포함)
        uint64                           _objectTick{ 0 };                                     ///< 마지막으로 견준 오브젝트 해시의 틱
        uint8                            _bSessionRunning{ SW_FALSE };
        uint8                            _bObjectComparison{ SW_FALSE }; ///< 마지막 "Which objects differ" 가 끝까지 갔다
    };
} // namespace sw::editor
