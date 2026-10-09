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
#include "Editor/Common/Gui/IEditorPanel.h"

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
        void shutdown( IRHIDevice* pRhiDevice ) override;

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

    private:
        deque<LogEntry>                       _listEntry;
        vector<LogEntry>                      _listDrawSnapshot;
        vector<const LogEntry*>               _listVisible;
        string                                _cachedFilter;
        EditorLogTagFilter                    _tagFilter;
        uint32                                _cachedTagRevision;
        mutex                                 _entriesMutex;
        DelegateHandle                        _logListenerHandle;
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        DevConsole                            _devConsole;
        fixed_string<constant::kMaxBuffer512> _commandBuffer;
        bool                                  _arrLevelEnabled[4];
        bool                                  _arrCachedLevelEnabled[4];
        bool                                  _bAutoScroll;
        uint8                                 _bHasNewLogs   : 1;
        [[maybe_unused]] uint8                _reservedFlags : 7;
    };
} // namespace sw::editor
