/**
 * @file MultiplayerSyncModel.h
 * @brief Sync 탭이 클라이언트 프로세스의 답을 기다리는 상태입니다 — 이력 · 오브젝트 해시 질의, 답 모으기, 시한, 모인 뒤 비교(ImGui 없음).
 * @details 클라이언트는 디버그 채널이 없어 표준 입력으로 묻고 로그 한 줄로 답한다. 답은 프레임을 건너 오므로 물을 때의 답 수를 기억해 "늘면 새 답" 으로 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Workspace/EditorStateHashCompare.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"

namespace sw::editor
{
    /** @class MultiplayerSyncModel @brief 파일 머리말을 참고하세요. 세션과 비교는 부를 때마다 받습니다(비교는 창을 닫아도 남는 에디터 상태이고, 세션은 에디터 범위의 것입니다). */
    class MultiplayerSyncModel
    {
    public:
        /** @brief 클라이언트에게 묻는 최근 틱 수입니다. 답은 로그 한 줄이라 한 줄 상한에 드는 수까지만 묻는다(더 많이 물으면 클라이언트가 오류로 답한다). */
        static constexpr int32 kClientHistoryTicks = static_cast<int32>( StateHashHistory::kMaxReportLineSamples );

        MultiplayerSyncModel();

        /** @brief 칸 @p sourceIndex 에 담을 이력을 프로세스 @p processIndex 에 묻습니다. 묻지 못하면(프로세스가 끝남) 경고를 남기고 false 입니다. */
        bool requestClientHistory( IEditorNetPlayProcesses& processes, const EditorStateHashCompare& compare, int32 sourceIndex, int32 processIndex );
        /** @brief 칸이 이 프로세스의 이력 답을 기다리는 중이면 true 입니다. */
        bool isWaitingForHistory( int32 sourceIndex, int32 processIndex ) const { return _arrPendingProcess[sourceIndex] == processIndex; }
        /**
         * @brief "Which objects differ" 를 시작합니다. 채널에서 읽은 칸은 바로 받고, 클라이언트에서 읽은 칸은 표준 입력에 `state.objectline` 을 물어 답을 기다린다.
         * @details 두 칸의 답이 모이면 틱 머리 검증을 거치는 `EditorStateHashCompare::compareObjects` 하나로 견준다(서버 칸과 같은 규칙).
         */
        void startObjectCompare( IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds );
        /** @brief 오브젝트 해시 답을 기다리는 중이면 true 입니다. */
        bool isWaitingForObjects() const { return _bObjectRequest != SW_FALSE; }
        /** @brief 온 답을 칸에 담고, 오브젝트 답이 모였으면 견주고, @p timeoutMilliseconds 를 넘긴 질의는 오류로 끝냅니다. 프레임마다 부릅니다. */
        void poll( IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds );

        /** @brief 마지막 질의 오류입니다(시간 초과 포함). 새 질의가 시작되면 비웁니다. */
        const string& getError() const { return _errorText; }
        void          setError( string_view errorText ) { _errorText = string( errorText ); }
        void          clearError() { _errorText.clear(); }

    private:
        void pollHistory( const IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds );
        void pollObjects( const IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds );

    private:
        int32                   _arrPendingProcess[EditorStateHashCompare::kSourceCount];       ///< 칸마다 답을 기다리는 클라이언트 프로세스 번호(-1 이면 없다)
        uint32                  _arrPendingReportCount[EditorStateHashCompare::kSourceCount];   ///< 물을 때의 그 프로세스 이력 답 수 — 늘면 새 답이다
        int32                   _arrObjectPendingProcess[EditorStateHashCompare::kSourceCount]; ///< 칸마다 오브젝트 해시 답을 기다리는 프로세스 번호(-1 이면 안 기다린다)
        uint32                  _arrObjectPendingCount[EditorStateHashCompare::kSourceCount];   ///< 물을 때의 그 프로세스 오브젝트 해시 답 수
        uint64                  _arrObjectTick[EditorStateHashCompare::kSourceCount];           ///< 칸마다 받은 오브젝트 해시의 틱
        vector<SceneObjectHash> _arrObjectList[EditorStateHashCompare::kSourceCount];           ///< 칸마다 받은 오브젝트별 해시
        Stopwatch               _arrPendingWatch[EditorStateHashCompare::kSourceCount];         ///< 칸마다 이력 질의를 보낸 뒤 지난 시간
        Stopwatch               _objectWatch;                                                   ///< 오브젝트 해시 질의를 보낸 뒤 지난 시간
        string                  _errorText;
        uint8                   _bObjectRequest : 1; ///< "Which objects differ" 가 답을 기다리는 중이다
    };
} // namespace sw::editor
