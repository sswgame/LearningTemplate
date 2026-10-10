/**
 * @file ConsolePanel.h
 * @brief Logger 를 구독해 항목을 걸러 보여 주는 Output Log 창입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorLogCommands.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/ConsoleLogRows.h"

#include "Engine/Console/DevConsole.h"

namespace sw::editor
{
    /** @brief Logger 출력을 그대로 보여 주는 콘솔입니다. */
    class ConsolePanel : public IEditorPanel
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 — 생성 시 Logger 구독, shutdown/소멸 시 해제
        // ------------------------------------------------------------------------------
        /** @brief Output Log 창을 만들고 Logger 를 구독합니다. */
        ConsolePanel();
        /** @brief Logger 구독을 해제하고 창을 파괴합니다. */
        virtual ~ConsolePanel() override;

        // ------------------------------------------------------------------------------
        // 2) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Output Log"; }
        /** @brief 필터, 레벨 토글, 로그 목록을 그립니다. */
        void drawContent() override;
        /** @brief 지우기·필터·레벨 토글 툴바를 그립니다. */
        void drawConsoleToolbar( bool& bNewLogs );
        /** @brief 필터를 통과한 로그 목록을 그립니다. */
        void drawLogList( bool bNewLogs );
        /** @brief Logger 구독을 해제합니다. */
        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 태그(카테고리) 필터입니다. */
        EditorLogTagFilter& getTagFilter() { return _tagFilter; }
        /** @brief 입력 줄이 쓰는 개발 콘솔입니다(기록을 듭니다). 답은 로그로 남아 이 패널에 보입니다. */
        DevConsole& getDevConsole() { return _devConsole; }
        /** @brief 마지막으로 그린 로그(걸러지기 전)에 이 메시지가 있으면 true 입니다(에디터 자체 시험용). */
        bool isMessageInSnapshot( string_view message ) const;
        /** @brief 마지막으로 그린, 걸러진 목록에 이 메시지가 있으면 true 입니다(에디터 자체 시험용). */
        bool isMessageVisible( string_view message ) const;

        // ------------------------------------------------------------------------------
        // 3) Logger 구독 (콜백은 그리기 스레드가 아닌 곳에서 올 수 있다)
        // ------------------------------------------------------------------------------
        /** @brief Logger 콜백입니다. 어느 스레드에서 불려도 뮤텍스로 보호해 항목을 추가합니다. */
        void onLogWritten( const LogEntry& entry );
        /** @brief Logger 구독을 제거합니다. */
        void unsubscribe();
        /** @brief 필터를 통과한 로그 포인터 목록을 다시 만듭니다. */
        void updateFilteredEntries( const string& filterStr );
        /** @brief 로그 줄의 카테고리입니다 — 로그를 쓴 자리(`SW_LOG_CALLER`), 없으면 모듈 태그입니다. 태그 필터 · 팝업이 이 이름을 씁니다. */
        static const string& getEntryCategory( const LogEntry& entry );
        /** @brief 태그(로거 카테고리)를 켜고 끄는 팝업을 그립니다. */
        void drawTagFilterPopup();
        /** @brief 로그 줄이 가리키는 소스 위치를 IDE 로 엽니다(메시지 안의 위치가 먼저). */
        static void openEntryInIde( const LogEntry& entry );
        /** @brief 아래쪽 명령 입력 줄(개발 콘솔 — 명령 · 전역 변수 get/set, Tab 자동완성, ↑↓ 기록)을 그립니다. */
        void drawCommandLine();
        /** @brief 지난 그리기의 줄 수입니다(접기 · 거르기 뒤 — 탐침 `Editor.OutputLogVisibleRows`). */
        uint32 getRowCount() const { return static_cast<uint32>( _listRow.size() ); }
        /** @brief 들고 있는 로그를 모두 지웁니다(Clear 단추 · Clear on Play). */
        void clearEntries();
        /** @brief 한 줄의 전체 글(시각 · 태그 · 수준 · 메시지)을 만듭니다. 복사가 씁니다. */
        static string formatEntryLine( const LogEntry& entry );
        /** @brief 선택한 줄을 클립보드에 복사합니다(접힌 줄은 한 번). */
        void copySelectedRows() const;
        /** @brief 로그 파일 폴더에서 가장 최근 파일을 파일 탐색기로 엽니다. */
        static void showLogFileInExplorer();
        /** @brief Play 시작(Clear on Play)과 새 오류(Error Pause)를 보고 반응합니다. */
        void updatePlayReactions();

    private:
        deque<LogEntry>                       _listEntry;
        vector<LogEntry>                      _listDrawSnapshot;
        vector<const LogEntry*>               _listVisible;
        vector<ConsoleLogRow>                 _listRow;
        ConsoleLogSelection                   _selection;
        string                                _cachedFilter;
        EditorLogTagFilter                    _tagFilter;
        uint32                                _cachedTagRevision;
        mutex                                 _entriesMutex;
        DelegateHandle                        _logListenerHandle;
        uint64                                _errorSerial;     ///< 받은 오류 줄 누계(_entriesMutex 가 지킨다)
        uint64                                _seenErrorSerial; ///< Error Pause 가 마지막으로 본 누계
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        DevConsole                            _devConsole;
        fixed_string<constant::kMaxBuffer512> _commandBuffer;
        bool                                  _arrLevelEnabled[4];
        bool                                  _arrCachedLevelEnabled[4];
        bool                                  _bAutoScroll;
        uint8                                 _bHasNewLogs   : 1;
        uint8                                 _bCollapse     : 1; ///< 같은 줄을 한 줄로 접는다(유니티 Collapse)
        uint8                                 _bClearOnPlay  : 1; ///< Play 를 시작하면 지운다
        uint8                                 _bErrorPause   : 1; ///< Play 중 오류 줄이 오면 멈춘다
        uint8                                 _bScrollToEnd  : 1; ///< 다음 그리기에서 맨 아래로 내린다(명령을 친 뒤)
        uint8                                 _bWasStopped   : 1; ///< 지난 그리기에 Play 가 멈춰 있었다
        uint8                                 _bRowsChanged  : 1; ///< 줄을 다시 만들었다(선택을 자른다)
        [[maybe_unused]] uint8                _reservedFlags : 1;
    };
} // namespace sw::editor
