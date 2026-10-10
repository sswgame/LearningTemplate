#include "pch.h"

#include "Editor/Panels/ConsolePanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Console/DevCommandRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ConsolePanelInternal
        {
            static ImVec4 colorForLevel( LogLevel level )
            {
                switch ( level )
                {
                    case LogLevel::Error:
                        return ImVec4( 1.0f, 0.4f, 0.4f, 1.0f );
                    case LogLevel::Warning:
                        return ImVec4( 1.0f, 0.8f, 0.4f, 1.0f );
                    case LogLevel::Info:
                        return ImVec4( 0.8f, 0.8f, 0.8f, 1.0f );
                    case LogLevel::Trace:
                        return ImVec4( 0.6f, 0.6f, 0.6f, 1.0f );
                    case LogLevel::Count:
                        return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
                }
            }

            /** @brief 입력 줄의 Tab(자동완성) · ↑↓(기록)을 개발 콘솔로 처리합니다. */
            static int32 onCommandLineEdit( ImGuiInputTextCallbackData* pData )
            {
                DevConsole* pConsole = static_cast<DevConsole*>( pData->UserData );
                if ( pConsole == nullptr )
                    return 0;
                if ( pData->EventFlag == ImGuiInputTextFlags_CallbackCompletion )
                {
                    string         line( pData->Buf, static_cast<size_t>( pData->BufTextLen ) );
                    vector<string> listCandidate;
                    if ( pConsole->complete( line, listCandidate ) )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, line.c_str() );
                    }
                    if ( listCandidate.size() > 1 )
                    {
                        string candidates;
                        for ( const string& candidate : listCandidate )
                        {
                            candidates += candidate;
                            candidates += "  ";
                        }
                        SW_LOG_INFO( "%#", candidates.c_str() );
                    }
                    return 0;
                }
                if ( pData->EventFlag == ImGuiInputTextFlags_CallbackHistory )
                {
                    const string* pLine = pData->EventKey == ImGuiKey_UpArrow ? pConsole->moveHistoryBack() : pConsole->moveHistoryForward();
                    if ( pLine != nullptr )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, pLine->c_str() );
                    }
                }
                return 0;
            }

            /** @brief 열린 Output Log 패널입니다. 없으면 nullptr 입니다. */
            static ConsolePanel* findPanel()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? static_cast<ConsolePanel*>( pContext->getPanelManager().findPanel( "console" ) ) : nullptr;
            }

            /** @brief Output Log 가 지난 그리기에 보인 줄 수입니다(접기 · 거르기 뒤). */
            [[nodiscard]] static bool readVisibleRows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ConsolePanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getRowCount() );
                return true;
            }

#if SW_DEV_COMMANDS_ENABLED
            /** @brief `log.repeat <count> <warning|info> <text...>` — 같은 줄을 여러 번 남깁니다(Output Log 접기 시험). */
            static bool runLogRepeat( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 )
                    return false;
                int32 count{ 0 };
                if ( StringUtil::parseInt( listArgument[0], count ) == false || count <= 0 || count > 1000 )
                    return false;
                const bool bWarning = listArgument[1] == "warning";
                if ( bWarning == false && listArgument[1] != "info" )
                    return false;
                string text = listArgument[2];
                for ( size_t argumentIndex = 3; argumentIndex < listArgument.size(); ++argumentIndex )
                {
                    text += " ";
                    text += listArgument[argumentIndex];
                }
                for ( int32 lineIndex = 0; lineIndex < count; ++lineIndex )
                {
                    if ( bWarning )
                        SW_LOG_WARNING( "%#", text.c_str() );
                    else
                        SW_LOG_INFO( "%#", text.c_str() );
                }
                outReply = "logged " + to_string( count ) + " lines";
                return true;
            }
#endif

            static const utf8* levelName( LogLevel level )
            {
                static constexpr const utf8* kArrNames[] = { "Error", "Warning", "Info", "Trace" };
                const uint8                  index       = static_cast<uint8>( level );
                if ( index >= 4 )
                    return "Info";
                return kArrNames[index];
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ConsolePanel, "console", EditorPanelCategory::Core, 400 );
    SW_AUTOMATION_PROBE( editorOutputLogVisibleRows, "Editor.OutputLogVisibleRows", "Rows the Output Log drew in the last frame (after its filters and Collapse)",
                         &ConsolePanelInternal::readVisibleRows );
    SW_DEV_COMMAND( LogRepeat, "log.repeat", "log.repeat <count> <warning|info> <text...>", "Write the same log line several times (Output Log Collapse checks)",
                    &ConsolePanelInternal::runLogRepeat );

    ConsolePanel::ConsolePanel()
        : _listEntry{}
        , _listDrawSnapshot{}
        , _listVisible{}
        , _listRow{}
        , _selection{}
        , _cachedFilter{}
        , _tagFilter{}
        , _cachedTagRevision{ 0 }
        , _entriesMutex{}
        , _logListenerHandle{}
        , _errorSerial{ 0 }
        , _seenErrorSerial{ 0 }
        , _filterBuffer{}
        , _devConsole{}
        , _commandBuffer{}
        , _arrLevelEnabled{ true, true, true, true }
        , _arrCachedLevelEnabled{ true, true, true, true }
        , _bAutoScroll{ true }
        , _bHasNewLogs{ SW_TRUE }
        , _bCollapse{ SW_FALSE }
        , _bClearOnPlay{ SW_FALSE }
        , _bErrorPause{ SW_FALSE }
        , _bScrollToEnd{ SW_TRUE }
        , _bWasStopped{ SW_TRUE }
        , _bRowsChanged{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        _logListenerHandle = Logger::addGlobalListener(
            SW_DELEGATE_METHOD( LogWrittenDelegate, &ConsolePanel::onLogWritten, this ) );
    }

    ConsolePanel::~ConsolePanel()
    {
        unsubscribe();
    }

    void ConsolePanel::updateFilteredEntries( const string& filterStr )
    {
        const EditorListFilter filter{ filterStr };

        _listVisible.clear();
        _listVisible.reserve( _listDrawSnapshot.size() );

        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            const uint8 levelIndex = static_cast<uint8>( entry._level );
            if ( levelIndex >= 4 || _arrLevelEnabled[levelIndex] == false )
                continue;

            if ( _tagFilter.isTagVisible( getEntryCategory( entry ) ) == false )
                continue;

            if ( filter.matchesAny( { entry._message, entry._tag, entry._caller, entry._file } ) == false )
                continue;

            _listVisible.push_back( &entry );
        }

        ConsoleLogRows::populate( _listVisible, _bCollapse == SW_TRUE, _listRow );
        _bRowsChanged = SW_TRUE;

        _cachedFilter      = filterStr;
        _cachedTagRevision = _tagFilter.getRevision();
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            _arrCachedLevelEnabled[levelIndex] = _arrLevelEnabled[levelIndex];
        }
    }

    void ConsolePanel::drawContent()
    {
        updatePlayReactions();

        bool bNewLogs{ false };
        {
            std::scoped_lock<mutex> lock{ _entriesMutex };
            if ( _bHasNewLogs == SW_TRUE )
            {
                _listDrawSnapshot.assign( _listEntry.begin(), _listEntry.end() );
                bNewLogs     = true;
                _bHasNewLogs = SW_FALSE;
            }
        }

        const bool bCollapseBefore = _bCollapse == SW_TRUE;
        drawConsoleToolbar( bNewLogs );
        const bool bCollapseChanged = bCollapseBefore != ( _bCollapse == SW_TRUE );

        ImGui::Separator();

        const string filterStr{ StringUtil::trim( _filterBuffer.c_str() ) };

        // 필터나 레벨 설정이 바뀌었거나 새 로그가 들어왔을 때만 다시 계산한다
        bool bLevelChanged{ false };
        for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
        {
            if ( _arrCachedLevelEnabled[levelIndex] != _arrLevelEnabled[levelIndex] )
            {
                bLevelChanged = true;
                break;
            }
        }

        const bool bFilterChanged = ( filterStr != _cachedFilter ) || _cachedTagRevision != _tagFilter.getRevision();

        if ( bNewLogs || bFilterChanged || bLevelChanged || bCollapseChanged )
            updateFilteredEntries( filterStr );

        drawLogList( bNewLogs );
    }

    void ConsolePanel::drawConsoleToolbar( bool& bNewLogs )
    {
        size_t errorCount{ 0 };
        size_t warnCount{ 0 };
        size_t infoCount{ 0 };
        size_t traceCount{ 0 };
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            switch ( entry._level )
            {
                case LogLevel::Error:
                {
                    ++errorCount;
                    break;
                }
                case LogLevel::Warning:
                {
                    ++warnCount;
                    break;
                }
                case LogLevel::Info:
                {
                    ++infoCount;
                    break;
                }
                case LogLevel::Trace:
                {
                    ++traceCount;
                    break;
                }
                case LogLevel::Count:
                {
                    break;
                }
            }
        }

        if ( EditorChrome::beginToolbar( "##ConsoleToolbar" ) )
        {
            if ( ImGui::Button( "Clear" ) )
            {
                clearEntries();
                bNewLogs = false;
            }
            EditorSelfTestMarks::note( "console.clear" );
            EditorWidgets::drawTooltip( "콘솔 로그 출력을 모두 지웁니다" );

            ImGui::SameLine();
            ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );
            EditorWidgets::drawTooltip( "맨 아래에 있을 때 새 로그를 따라 내려갑니다. 위로 올려 읽는 동안은 멈추고, 맨 아래로 내리면 다시 따라갑니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( "Collapse", _bCollapse == SW_TRUE, editor::style::kOk ) )
                _bCollapse = _bCollapse == SW_TRUE ? SW_FALSE : SW_TRUE;
            EditorSelfTestMarks::note( "console.collapse" );
            EditorWidgets::drawTooltip( "같은 로그 줄을 한 줄로 접고 앞에 횟수를 적습니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( "Clear on Play", _bClearOnPlay == SW_TRUE, editor::style::kOk ) )
                _bClearOnPlay = _bClearOnPlay == SW_TRUE ? SW_FALSE : SW_TRUE;
            EditorWidgets::drawTooltip( "Play 를 시작하면 로그를 지웁니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( "Error Pause", _bErrorPause == SW_TRUE, editor::style::kError ) )
                _bErrorPause = _bErrorPause == SW_TRUE ? SW_FALSE : SW_TRUE;
            EditorWidgets::drawTooltip( "Play 중 오류 로그가 나오면 Play 를 멈춥니다(Pause)" );

            fixed_string<constant::kMaxBuffer32> arrErrLabel;
            formatstring( arrErrLabel.data(), arrErrLabel.capacity(), "Error (%zu)", errorCount );
            fixed_string<constant::kMaxBuffer32> arrWarnLabel;
            formatstring( arrWarnLabel.data(), arrWarnLabel.capacity(), "Warning (%zu)", warnCount );
            fixed_string<constant::kMaxBuffer32> arrInfoLabel;
            formatstring( arrInfoLabel.data(), arrInfoLabel.capacity(), "Info (%zu)", infoCount );
            fixed_string<constant::kMaxBuffer32> arrTraceLabel;
            formatstring( arrTraceLabel.data(), arrTraceLabel.capacity(), "Trace (%zu)", traceCount );

            constexpr editor::Color4 kTraceChip{ 0.40f, 0.40f, 0.45f, 1.0f };

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrErrLabel.c_str(), _arrLevelEnabled[0], editor::style::kError ) )
                _arrLevelEnabled[0] = ( _arrLevelEnabled[0] == false );
            EditorWidgets::drawTooltip( "오류(Error) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrWarnLabel.c_str(), _arrLevelEnabled[1], editor::style::kWarn ) )
                _arrLevelEnabled[1] = ( _arrLevelEnabled[1] == false );
            EditorWidgets::drawTooltip( "경고(Warning) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrInfoLabel.c_str(), _arrLevelEnabled[2], editor::style::kOk ) )
                _arrLevelEnabled[2] = ( _arrLevelEnabled[2] == false );
            EditorSelfTestMarks::note( "console.level.info" );
            EditorWidgets::drawTooltip( "정보(Info) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( EditorWidgets::drawToggleButton( arrTraceLabel.c_str(), _arrLevelEnabled[3], kTraceChip ) )
                _arrLevelEnabled[3] = ( _arrLevelEnabled[3] == false );
            EditorWidgets::drawTooltip( "상세(Trace) 수준 로그 표시 여부를 토글합니다" );

            ImGui::SameLine();
            if ( ImGui::Button( "Copy All" ) && _listVisible.empty() == false )
            {
                string allLogs;
                for ( const LogEntry* pEntry : _listVisible )
                {
                    if ( pEntry != nullptr )
                        allLogs += formatEntryLine( *pEntry ) + "\n";
                }
                ImGui::SetClipboardText( allLogs.c_str() );
            }
            EditorWidgets::drawTooltip( "필터링된 모든 콘솔 로그를 클립보드에 복사합니다" );

            ImGui::SameLine();
            if ( ImGui::Button( "Open Log File" ) )
                showLogFileInExplorer();
            EditorWidgets::drawTooltip( "저장된 로그 파일(가장 최근)을 파일 탐색기에서 엽니다" );

            ImGui::SameLine();
            fixed_string<constant::kMaxBuffer32> tagLabel;
            if ( _tagFilter.getHiddenCount() > 0 )
                formatstring( tagLabel.data(), tagLabel.capacity(), "Tags (-%u)", _tagFilter.getHiddenCount() );
            else
                formatstring( tagLabel.data(), tagLabel.capacity(), "Tags" );
            if ( ImGui::Button( tagLabel.c_str() ) )
                ImGui::OpenPopup( "##LogTagFilter" );
            EditorWidgets::drawTooltip( "로그 태그(카테고리)마다 보이기를 켜고 끕니다" );
            drawTagFilterPopup();

            EditorWidgets::drawSearchField( "##log_filter", _filterBuffer, "Filter (tag / message / file)", -1.0f, false );
            EditorSelfTestMarks::note( "console.filter" );
            EditorWidgets::drawTooltip( "로그 메시지, 모듈 태그, 파일명으로 필터링하여 검색합니다" );
        }

        EditorChrome::endToolbar();
    }

    void ConsolePanel::drawLogList( bool bNewLogs )
    {
        editor::EditorSectionDesc logDesc{};
        logDesc._pID   = "##log_scroll";
        logDesc._kind  = editor::EditorSectionKind::Child;
        logDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining |
                         editor::EditorSectionFlags::HorizontalScrollbar;
        EditorChrome::beginSection( logDesc );

        // 0건의 이유를 나눠서 알려 준다. 로그가 아직 없는 것, 검색어가 걸러 낸 것, 레벨을 모두
        // 끈 것은 서로 다른 상황이고 고치는 방법도 다르다.
        if ( _listVisible.empty() )
        {
            const EditorListFilter filter{ _cachedFilter };
            if ( _listDrawSnapshot.empty() )
                EditorWidgets::drawEmptyHint( "No log output yet." );
            else if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( "All log levels are hidden. Re-enable one in the toolbar." );
        }

        // 맨 아래에 붙어 있을 때만 새 로그를 따라간다 — 값은 지난 프레임 배치 기준이다(스크롤은 다음 Begin 에 적용된다).
        const float32 lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const bool    bAtBottom  = ConsoleLogRows::isScrolledToBottom( ImGui::GetScrollY(), ImGui::GetScrollMaxY(), lineHeight * 0.5f );

        if ( _bRowsChanged == SW_TRUE )
        {
            _selection.clampTo( static_cast<uint32>( _listRow.size() ) );
            _bRowsChanged = SW_FALSE;
        }
        if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
            _selection.release();

        const bool    bListHovered = ImGui::IsWindowHovered();
        const float32 rowWidth     = ImGui::GetContentRegionAvail().x;
        ImDrawList*   pDrawList    = ImGui::GetWindowDrawList();
        const ImU32   selectColor  = ImGui::GetColorU32( ImGuiCol_Header );

        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int32>( _listRow.size() ) );
        while ( clipper.Step() )
        {
            for ( int32 rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex )
            {
                const ConsoleLogRow& row   = _listRow[static_cast<size_t>( rowIndex )];
                const LogEntry&      entry = *row._pEntry;
                ImGui::PushID( rowIndex );

                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const ImVec2 rowMax{ rowMin.x + rowWidth, rowMin.y + lineHeight };
                if ( _selection.isSelected( static_cast<uint32>( rowIndex ) ) )
                    pDrawList->AddRectFilled( rowMin, rowMax, selectColor );

                if ( row._repeatCount > 1 )
                {
                    ImGui::TextColored( ConsolePanelInternal::colorForLevel( entry._level ), "(%u)", row._repeatCount );
                    ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x );
                }
                ImGui::TextDisabled( "[%s]", entry._timeStamp.c_str() );
                ImGui::SameLine( 0.0f, 0.0f );
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getInfoColor() );
                if ( entry._caller.empty() )
                    ImGui::Text( " [%s]", entry._tag.c_str() );
                else
                    ImGui::Text( " [%s:%s]", entry._tag.c_str(), entry._caller.c_str() );
                EditorThemeUtil::popTextColor();
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextColored( ConsolePanelInternal::colorForLevel( entry._level ), " [%s]", ConsolePanelInternal::levelName( entry._level ) );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( " - " );
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::TextUnformatted( entry._message.c_str() );

                if ( entry._file.empty() == false )
                {
                    fixed_string<constant::kMaxBuffer256> tooltipText;
                    formatstring( tooltipText.data(), tooltipText.capacity(), "%s(%d) - double-click to open in IDE", entry._file.c_str(), entry._line );
                    EditorWidgets::drawTooltip( tooltipText.c_str() );
                }
                if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                    openEntryInIde( entry );

                // 줄을 누르면 고르고, 누른 채 끌거나 Shift 로 누르면 넓힌다(언리얼 Output Log 의 여러 줄 선택).
                if ( bListHovered && ImGui::IsMouseHoveringRect( rowMin, rowMax, false ) )
                {
                    if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                        _selection.press( static_cast<uint32>( rowIndex ), ImGui::GetIO().KeyShift );
                    else if ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && _selection.isSelected( static_cast<uint32>( rowIndex ) ) == false )
                    {
                        _selection.press( static_cast<uint32>( rowIndex ), false );
                        _selection.release();
                    }
                    else if ( _selection.isDragging() )
                        _selection.dragTo( static_cast<uint32>( rowIndex ) );
                }

                if ( ImGui::BeginPopupContextItem( "LogEntryCtx" ) )
                {
                    if ( ImGui::MenuItem( "Open in IDE" ) )
                        openEntryInIde( entry );
                    if ( ImGui::MenuItem( "Copy Message" ) )
                        ImGui::SetClipboardText( entry._message.c_str() );
                    if ( ImGui::MenuItem( "Copy Full Log Line" ) )
                    {
                        string full = formatEntryLine( entry );
                        if ( entry._file.empty() == false )
                            full += " (" + entry._file + ":" + to_string( entry._line ) + ")";
                        ImGui::SetClipboardText( full.c_str() );
                    }
                    fixed_string<constant::kMaxBuffer64> copySelectedLabel;
                    formatstring( copySelectedLabel.data(), copySelectedLabel.capacity(), "Copy Selected (%u lines)", _selection.getCount() );
                    if ( ImGui::MenuItem( copySelectedLabel.c_str(), "Ctrl+C", false, _selection.hasSelection() ) )
                        copySelectedRows();
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        if ( _selection.hasSelection() && ImGui::IsWindowFocused() && ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_C ) )
            copySelectedRows();

        const bool bScrollRequested = _bScrollToEnd == SW_TRUE;
        if ( _bAutoScroll && ( bScrollRequested || ( bNewLogs && bAtBottom ) ) )
            ImGui::SetScrollHereY( 1.0f );
        _bScrollToEnd = SW_FALSE;

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( _listRow.size() ), static_cast<uint32>( _listDrawSnapshot.size() ), "lines" );
        ImGui::SameLine();
        drawCommandLine();
    }

    void ConsolePanel::drawCommandLine()
    {
        ImGui::SetNextItemWidth( -1.0f );
        constexpr ImGuiInputTextFlags kFlags     = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        const bool                    bSubmitted = ImGui::InputTextWithHint( "##ConsoleCommand", "> command or gv_name [value] (Tab completes, Up/Down history, help)", _commandBuffer.data(),
                                                                             _commandBuffer.capacity(), kFlags, &ConsolePanelInternal::onCommandLineEdit, &_devConsole );
        if ( bSubmitted )
        {
            (void)_devConsole.submit( _commandBuffer.c_str() ); // 답 · 실패는 로그로 남아 이 패널에 보인다
            _commandBuffer.clear();
            _bAutoScroll  = true;
            _bScrollToEnd = SW_TRUE;           // 친 명령의 답을 보도록 맨 아래로 내린다
            ImGui::SetKeyboardFocusHere( -1 ); // 이어서 칠 수 있게 입력 줄에 머문다
        }
    }

    const string& ConsolePanel::getEntryCategory( const LogEntry& entry )
    {
        return entry._caller.empty() ? entry._tag : entry._caller;
    }

    bool ConsolePanel::isMessageInSnapshot( string_view message ) const
    {
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            if ( entry._message == message )
                return true;
        }
        return false;
    }

    bool ConsolePanel::isMessageVisible( string_view message ) const
    {
        for ( const LogEntry* pEntry : _listVisible )
        {
            if ( pEntry != nullptr && pEntry->_message == message )
                return true;
        }
        return false;
    }

    void ConsolePanel::clearEntries()
    {
        std::scoped_lock<mutex> lock{ _entriesMutex };
        _listEntry.clear();
        _listDrawSnapshot.clear();
        _listVisible.clear();
        _listRow.clear();
        _selection.clear();
        _bHasNewLogs = SW_FALSE;
    }

    string ConsolePanel::formatEntryLine( const LogEntry& entry )
    {
        return "[" + entry._timeStamp + "] [" + entry._tag + "] [" + ConsolePanelInternal::levelName( entry._level ) + "] - " + entry._message;
    }

    void ConsolePanel::copySelectedRows() const
    {
        if ( _selection.hasSelection() == false )
            return;
        string text;
        for ( uint32 rowIndex = _selection.getFirst(); rowIndex <= _selection.getLast() && rowIndex < _listRow.size(); ++rowIndex )
        {
            text += formatEntryLine( *_listRow[rowIndex]._pEntry );
            text += "\n";
        }
        ImGui::SetClipboardText( text.c_str() );
    }

    void ConsolePanel::showLogFileInExplorer()
    {
        ILogSink* pSink = Logger::getGlobalSink();
        if ( pSink == nullptr || pSink->getLogFolderPath().empty() )
        {
            SW_LOG_WARNING( "No log file folder (the log sink writes no file)" );
            return;
        }
        // 파일 이름은 시작 시각이라 이름이 가장 큰 파일이 이번 실행의 로그다.
        vector<string> listFilePath;
        if ( FileUtil::collectFiles( pSink->getLogFolderPath(), ".txt", listFilePath, false ) == false || listFilePath.empty() )
        {
            (void)EditorAssetCommands::showInFileExplorer( pSink->getLogFolderPath() );
            return;
        }
        std::sort( listFilePath.begin(), listFilePath.end() );
        (void)EditorAssetCommands::showInFileExplorer( listFilePath.back() );
    }

    void ConsolePanel::updatePlayReactions()
    {
        const bool bStopped = EditorPlaySession::isStopped();
        if ( _bClearOnPlay == SW_TRUE && _bWasStopped == SW_TRUE && bStopped == false )
            clearEntries();
        _bWasStopped = bStopped ? SW_TRUE : SW_FALSE;

        uint64 errorSerial{ 0 };
        {
            std::scoped_lock<mutex> lock{ _entriesMutex };
            errorSerial = _errorSerial;
        }
        // 오류는 로그를 쓴 다음 그리기에서 본다 — 그 프레임이 끝나기 전에 멈추는 유니티 Error Pause 보다 한 프레임 늦다.
        if ( _bErrorPause == SW_TRUE && errorSerial != _seenErrorSerial && EditorPlaySession::isPlaying() && EditorPlaySession::isPaused() == false )
            EditorPlaySession::pause();
        _seenErrorSerial = errorSerial;
    }

    void ConsolePanel::drawTagFilterPopup()
    {
        if ( ImGui::BeginPopup( "##LogTagFilter" ) == false )
            return;
        // 지금 들고 있는 로그에 나온 태그만 보인다(순서는 처음 나온 순서).
        vector<string> listTag;
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            const string& category = getEntryCategory( entry );
            bool          bSeen{ false };
            for ( const string& tag : listTag )
            {
                bSeen = bSeen || tag == category;
            }
            if ( bSeen == false )
                listTag.push_back( category );
        }
        std::sort( listTag.begin(), listTag.end() );
        if ( ImGui::MenuItem( "Show All" ) )
            _tagFilter.showAll();
        ImGui::Separator();
        for ( const string& tag : listTag )
        {
            bool bVisible = _tagFilter.isTagVisible( tag );
            if ( ImGui::Checkbox( tag.empty() ? "(none)" : tag.c_str(), &bVisible ) )
                _tagFilter.setTagVisible( tag, bVisible );
        }
        ImGui::EndPopup();
    }

    void ConsolePanel::openEntryInIde( const LogEntry& entry )
    {
        EditorSourceLocation location;
        if ( EditorLogCommands::findLogEntryLocation( entry._message, entry._file, entry._line, location ) )
            (void)EditorLogCommands::openInIde( location ); // 실패는 openInIde 가 경고로 남긴다
    }

    void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
    {
        unsubscribe();
    }

    void ConsolePanel::onLogWritten( const LogEntry& entry )
    {
        // 어느 스레드 · 어느 스코프에서 쓴 로그든 콘솔이 들고 있는 사본은 에디터 몫이다.
        SW_MEMORY_SCOPE( Editor );
        std::scoped_lock<mutex> lock{ _entriesMutex };
        _listEntry.push_back( entry );
        if ( entry._level == LogLevel::Error )
            ++_errorSerial;
        while ( _listEntry.size() > constant::kMaxBuffer2048 )
        {
            _listEntry.pop_front();
        }
        _bHasNewLogs = SW_TRUE;
    }

    void ConsolePanel::unsubscribe()
    {
        if ( _logListenerHandle.isValid() )
        {
            Logger::removeGlobalListener( _logListenerHandle );
            _logListenerHandle = {};
        }
    }
} // namespace sw::editor
