#include "pch.h"

#include "Editor/Panels/Console/ConsolePanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ConsolePanelInternal
        {
            static Color colorForLevel( LogLevel level )
            {
                switch ( level )
                {
                    case LogLevel::Error:
                        return Color{ 1.0f, 0.4f, 0.4f, 1.0f };
                    case LogLevel::Warning:
                        return Color{ 1.0f, 0.8f, 0.4f, 1.0f };
                    case LogLevel::Info:
                        return Color{ 0.8f, 0.8f, 0.8f, 1.0f };
                    case LogLevel::Trace:
                        return Color{ 0.6f, 0.6f, 0.6f, 1.0f };
                    case LogLevel::Count:
                        return Color{ 1.0f, 1.0f, 1.0f, 1.0f };
                }
            }

            /** @brief 로그 목록에서 레벨별 줄 수를 셉니다. */
            static ConsolePanel::LevelCounts countLevels( const deque<LogEntry>& listEntry )
            {
                ConsolePanel::LevelCounts counts;
                for ( const LogEntry& entry : listEntry )
                {
                    switch ( entry._level )
                    {
                        case LogLevel::Error:
                        {
                            ++counts._error;
                            break;
                        }
                        case LogLevel::Warning:
                        {
                            ++counts._warning;
                            break;
                        }
                        case LogLevel::Info:
                        {
                            ++counts._info;
                            break;
                        }
                        case LogLevel::Trace:
                        {
                            ++counts._trace;
                            break;
                        }
                        case LogLevel::Count:
                        {
                            break;
                        }
                    }
                }
                return counts;
            }

            /** @brief 레벨 보기 토글 칩 하나("이름 (줄 수)")를 그립니다. @p pMark 가 있으면 시나리오용 레이블을 남깁니다. */
            static void drawLevelChip( bool& inoutEnabled, const utf8* pName, size_t count, const Color& color, const utf8* pTooltip, const utf8* pMark )
            {
                fixed_string<constant::kMaxBuffer32> label;
                formatstring( label.data(), label.capacity(), "%# (%#)", pName, static_cast<uint64>( count ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) );
                if ( EditorWidgets::drawToggleButton( label.c_str(), inoutEnabled, color ) )
                    inoutEnabled = ( inoutEnabled == false );
                if ( pMark != nullptr )
                    EditorSelfTestMarks::note( pMark );
                EditorWidgets::drawTooltip( pTooltip );
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
                    string     line;
                    const bool bMoved = pData->EventKey == ImGuiKey_UpArrow ? pConsole->moveHistoryBack( line ) : pConsole->moveHistoryForward( line );
                    if ( bMoved )
                    {
                        pData->DeleteChars( 0, pData->BufTextLen );
                        pData->InsertChars( 0, line.c_str() );
                    }
                }
                return 0;
            }

            /** @brief 열린 Output Log 패널입니다. 없으면 nullptr 입니다. */
            static ConsolePanel* findPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<ConsolePanel>() : nullptr;
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
            /** @brief `log.repeat <count> <warning|info> <text...>` — 같은 줄을 여러 번 남깁니다(Output Log 접기 테스트). */
            static bool runLogRepeat( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 )
                    return false;
                int32      count{ 0 };
                const bool bInvalidLogCount = StringUtil::parseInt( listArgument[0], count ) == false || count <= 0 || count > 1000;
                if ( bInvalidLogCount )
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
    SW_EDITOR_PANEL( ConsolePanel, ConsolePanel::kPanelID, EditorPanelCategory::Core, 400 );
    SW_AUTOMATION_WATCH( editorOutputLogVisibleRows, "Editor.OutputLogVisibleRows", "Rows the Output Log drew in the last frame (after its filters and Collapse)",
                         &ConsolePanelInternal::readVisibleRows );
    SW_DEV_COMMAND( LogRepeat, "log.repeat", "log.repeat <count> <warning|info> <text...>", "Write the same log line several times (Output Log Collapse checks)",
                    &ConsolePanelInternal::runLogRepeat );

    ConsolePanel::ConsolePanel()
        : _listEntry{}
        , _listDrawSnapshot{}
        , _listTagCache{}
        , _listVisible{}
        , _listRow{}
        , _selection{}
        , _cachedFilter{}
        , _tagFilter{}
        , _cachedTagRevision{ 0 }
        , _entriesMutex{}
        , _logListenerSubscription{}
        , _errorSerial{ 0 }
        , _entrySerial{ 0 }
        , _snapshotSerial{ 0 }
        , _seenErrorSerial{ 0 }
        , _snapshotGeneration{ 1 }
        , _tagCacheGeneration{ 0 }
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
        _logListenerSubscription = Logger::registerGlobalListener(
            SW_DELEGATE_METHOD( LogWrittenDelegate, &ConsolePanel::onLogWritten, this ) );
    }

    ConsolePanel::~ConsolePanel()
    {
        _logListenerSubscription.reset(); // 멤버가 사라지기 전에 — 다른 스레드의 방송이 끝나기를 기다린다
    }

    void ConsolePanel::updateFilteredEntries( const string& filterStr )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
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
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        updatePlayReactions();

        bool bNewLogs{ false };
        {
            ScopedLock<mutex> lock{ _entriesMutex };
            if ( _bHasNewLogs == SW_TRUE )
            {
                // 새 줄만 덧붙이고 넘친 앞줄을 뺀다. 통째로 다시 담으면 새 로그가 올 때마다 최대 2048 줄(글자열마다 할당)을 복사한다.
                const uint64 newCount = _entrySerial - _snapshotSerial;
                if ( newCount >= _listEntry.size() )
                    _listDrawSnapshot.assign( _listEntry.begin(), _listEntry.end() );
                else
                {
                    _listDrawSnapshot.insert( _listDrawSnapshot.end(), _listEntry.end() - static_cast<ptrdiff_t>( newCount ), _listEntry.end() );
                    while ( _listDrawSnapshot.size() > _listEntry.size() )
                    {
                        _listDrawSnapshot.pop_front();
                    }
                }
                _snapshotSerial = _entrySerial;
                ++_snapshotGeneration;
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

        const bool bRefreshFilteredEntries = bNewLogs || bFilterChanged || bLevelChanged || bCollapseChanged;
        if ( bRefreshFilteredEntries )
            updateFilteredEntries( filterStr );

        drawLogList( bNewLogs );
    }

    void ConsolePanel::drawConsoleToolbar( bool& bNewLogs )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        const LevelCounts counts = ConsolePanelInternal::countLevels( _listDrawSnapshot );

        if ( EditorChrome::beginToolbar( "##ConsoleToolbar" ) )
        {
            drawToolbarToggles( bNewLogs );
            drawLevelChips( counts );
            drawToolbarCommands();
        }

        EditorChrome::endToolbar();
    }

    void ConsolePanel::drawToolbarToggles( bool& bNewLogs )
    {
        if ( ImGui::Button( "Clear" ) )
        {
            clearEntries();
            bNewLogs = false;
        }
        EditorSelfTestMarks::note( "console.clear" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "Clear" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Auto-scroll" ) );
        ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "AutoScroll" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Collapse" ) );
        if ( EditorWidgets::drawToggleButton( "Collapse", _bCollapse == SW_TRUE, EditorThemeUtil::getSuccessColor() ) )
            _bCollapse = _bCollapse == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorSelfTestMarks::note( "console.collapse" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "Collapse" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Clear on Play" ) );
        if ( EditorWidgets::drawToggleButton( "Clear on Play", _bClearOnPlay == SW_TRUE, EditorThemeUtil::getSuccessColor() ) )
            _bClearOnPlay = _bClearOnPlay == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "ClearOnPlay" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Error Pause" ) );
        if ( EditorWidgets::drawToggleButton( "Error Pause", _bErrorPause == SW_TRUE, EditorThemeUtil::getErrorColor() ) )
            _bErrorPause = _bErrorPause == SW_TRUE ? SW_FALSE : SW_TRUE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "ErrorPause" ) );
    }

    void ConsolePanel::drawLevelChips( const LevelCounts& counts )
    {
        constexpr Color kTraceChip{ 0.40f, 0.40f, 0.45f, 1.0f };

        // 0 건이면 경고색을 쓰지 않는다. 걸러 보이는 중이라는 뜻만 강조색으로 보인다(빨간 "Error (0)" 은 문제가 있다고 읽힌다).
        const Color& errorChip = counts._error > 0 ? EditorThemeUtil::getErrorColor() : EditorThemeUtil::getAccentColor();
        const Color& warnChip  = counts._warning > 0 ? EditorThemeUtil::getWarningColor() : EditorThemeUtil::getAccentColor();
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[0], "Error", counts._error, errorChip, SW_LOCKEY( "EditorConsole", "ShowErrors" ), nullptr );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[1], "Warning", counts._warning, warnChip, SW_LOCKEY( "EditorConsole", "ShowWarnings" ), nullptr );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[2], "Info", counts._info, EditorThemeUtil::getSuccessColor(), SW_LOCKEY( "EditorConsole", "ShowInfo" ), "console.level.info" );
        ConsolePanelInternal::drawLevelChip( _arrLevelEnabled[3], "Trace", counts._trace, kTraceChip, SW_LOCKEY( "EditorConsole", "ShowTrace" ), nullptr );
    }

    void ConsolePanel::drawToolbarCommands()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Copy All" ) );
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
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "CopyAll" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Open Log File" ) );
        if ( ImGui::Button( "Open Log File" ) )
            showLogFileInExplorer();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "OpenLogFile" ) );

        ImGui::SameLine();
        fixed_string<constant::kMaxBuffer32> tagLabel;
        if ( _tagFilter.getHiddenCount() > 0 )
            formatstring( tagLabel.data(), tagLabel.capacity(), "Tags (-%u)", _tagFilter.getHiddenCount() );
        else
            formatstring( tagLabel.data(), tagLabel.capacity(), "Tags" );
        if ( ImGui::Button( tagLabel.c_str() ) )
            EditorInputGUI::openPopup( "##LogTagFilter" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "TagFilter" ) );
        drawTagFilterPopup();

        EditorWidgets::drawSearchField( "##log_filter", _filterBuffer, SW_LOCKEY( "EditorConsole", "FilterHint" ), -1.0f, false );
        EditorSelfTestMarks::note( "console.filter" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorConsole", "FilterTooltip" ) );
    }

    void ConsolePanel::drawLogList( bool bNewLogs )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
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
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorConsole", "NoLogOutput" ) );
            else if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorConsole", "AllLevelsHidden" ) );
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

        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int32>( _listRow.size() ) );
        while ( clipper.Step() )
        {
            for ( int32 rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex )
            {
                drawLogRow( rowIndex, rowWidth, lineHeight, bListHovered );
            }
        }

        // Ctrl+C 는 `Editor.Panel.console` 레이어의 액션이다(이 창이 포커스일 때만 켜진다).
        const bool bCopyShortcutPressed = _selection.hasSelection() && EditorInputGUI::wasActionTriggered( "console.copySelection" );
        if ( bCopyShortcutPressed )
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

    void ConsolePanel::drawLogRow( int32 rowIndex, float32 rowWidth, float32 lineHeight, bool bListHovered )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        const ConsoleLogRow& row   = _listRow[static_cast<size_t>( rowIndex )];
        const LogEntry&      entry = *row._pEntry;
        ImGui::PushID( rowIndex );

        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax{ rowMin.x + rowWidth, rowMin.y + lineHeight };
        if ( _selection.isSelected( static_cast<uint32>( rowIndex ) ) )
            ImGui::GetWindowDrawList()->AddRectFilled( rowMin, rowMax, ImGui::GetColorU32( ImGuiCol_Header ) );

        if ( row._repeatCount > 1 )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( ConsolePanelInternal::colorForLevel( entry._level ) ), "(%u)", row._repeatCount );
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
        ImGui::TextColored( EditorColorUtil::toImVec4( ConsolePanelInternal::colorForLevel( entry._level ) ), " [%s]", ConsolePanelInternal::levelName( entry._level ) );
        ImGui::SameLine( 0.0f, 0.0f );
        ImGui::TextUnformatted( " - " );
        ImGui::SameLine( 0.0f, 0.0f );
        ImGui::TextUnformatted( entry._message.c_str() );

        if ( entry._file.empty() == false && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
        {
            const string tooltipText = SW_LOCKEYFORMAT( "EditorConsole", "EntrySource", TextArgumentList().addText( "file", entry._file.c_str() ).addInteger( "line", entry._line ) );
            EditorWidgets::drawTooltip( tooltipText.c_str() );
        }
        if ( EditorInputGUI::wasItemDoubleClicked() )
            openEntryInIde( entry );

        // 줄을 누르면 고르고, 누른 채 끌거나 Shift 로 누르면 넓힌다(언리얼 Output Log 의 여러 줄 선택). 선택 밖 줄을 오른쪽으로 누르면 그 줄만 고른다(R2).
        if ( bListHovered && ImGui::IsMouseHoveringRect( rowMin, rowMax, false ) )
        {
            const EditorPointerState pointer = EditorInputGUI::readPointer();
            if ( pointer.wasPressed( EditorMouseButton::Left ) )
                _selection.press( static_cast<uint32>( rowIndex ), pointer.hasModifier( ModifierKey::Shift ) );
            else if ( pointer.wasPressed( EditorMouseButton::Right ) && _selection.isSelected( static_cast<uint32>( rowIndex ) ) == false )
            {
                _selection.press( static_cast<uint32>( rowIndex ), false );
                _selection.release();
            }
            else if ( _selection.isDragging() )
                _selection.dragTo( static_cast<uint32>( rowIndex ) );
        }

        drawLogRowContextMenu( entry );
        ImGui::PopID();
    }

    void ConsolePanel::drawLogRowContextMenu( const LogEntry& entry )
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( EditorInputGUI::beginItemContextMenu( "LogEntryCtx" ) == false )
            return;
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

    void ConsolePanel::drawCommandLine()
    {
        ImGui::SetNextItemWidth( -1.0f );
        constexpr ImGuiInputTextFlags kFlags     = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        const bool                    bSubmitted = ImGui::InputTextWithHint( "##ConsoleCommand", SW_LOCKEY( "EditorConsole", "CommandHint" ), _commandBuffer.data(),
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
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        return VectorUtil::containsBy<&LogEntry::_message>( _listDrawSnapshot, message );
    }

    bool ConsolePanel::isMessageVisible( string_view message ) const
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        for ( const LogEntry* pEntry : _listVisible )
        {
            if ( pEntry != nullptr && pEntry->_message == message )
                return true;
        }
        return false;
    }

    void ConsolePanel::clearEntries()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        ScopedLock<mutex> lock{ _entriesMutex };
        _listEntry.clear();
        _listDrawSnapshot.clear();
        _listVisible.clear();
        _listRow.clear();
        _selection.clear();
        _snapshotSerial = _entrySerial;
        ++_snapshotGeneration;
        _bHasNewLogs = SW_FALSE;
    }

    string ConsolePanel::formatEntryLine( const LogEntry& entry )
    {
        return "[" + entry._timeStamp + "] [" + entry._tag + "] [" + ConsolePanelInternal::levelName( entry._level ) + "] - " + entry._message;
    }

    void ConsolePanel::copySelectedRows() const
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
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
            (void)EditorAssetFileCommands::showInFileExplorer( pSink->getLogFolderPath() );
            return;
        }
        std::sort( listFilePath.begin(), listFilePath.end() );
        (void)EditorAssetFileCommands::showInFileExplorer( listFilePath.back() );
    }

    void ConsolePanel::updatePlayReactions()
    {
        const bool bStopped          = EditorPlaySession::isStopped();
        const bool bClearOnPlayStart = _bClearOnPlay == SW_TRUE && _bWasStopped == SW_TRUE && bStopped == false;
        if ( bClearOnPlayStart )
            clearEntries();
        _bWasStopped = bStopped ? SW_TRUE : SW_FALSE;

        uint64 errorSerial{ 0 };
        {
            ScopedLock<mutex> lock{ _entriesMutex };
            errorSerial = _errorSerial;
        }
        // 오류는 로그를 쓴 다음 그리기에서 본다 — 그 프레임이 끝나기 전에 멈추는 유니티 Error Pause 보다 한 프레임 늦다.
        const bool bPauseOnNewError = _bErrorPause == SW_TRUE && errorSerial != _seenErrorSerial && EditorPlaySession::isPlaying() && EditorPlaySession::isPaused() == false;
        if ( bPauseOnNewError )
            EditorPlaySession::pause();
        _seenErrorSerial = errorSerial;
    }

    void ConsolePanel::drawTagFilterPopup()
    {
        _uiThread.assumeHeld(); // 패널을 그리는 UI 스레드에서 불린다
        if ( ImGui::BeginPopup( "##LogTagFilter" ) == false )
            return;
        if ( ImGui::MenuItem( "Show All" ) )
            _tagFilter.showAll();
        ImGui::Separator();
        for ( const string& tag : getSnapshotTags() )
        {
            bool bVisible = _tagFilter.isTagVisible( tag );
            if ( ImGui::Checkbox( tag.empty() ? "(none)" : tag.c_str(), &bVisible ) )
                _tagFilter.setTagVisible( tag, bVisible );
        }
        ImGui::EndPopup();
    }

    const vector<string>& ConsolePanel::getSnapshotTags()
    {
        if ( _tagCacheGeneration == _snapshotGeneration )
            return _listTagCache;
        // 지금 보관하고 있는 로그에 나온 태그만 보인다.
        _listTagCache.clear();
        _listTagCache.reserve( _listDrawSnapshot.size() );
        for ( const LogEntry& entry : _listDrawSnapshot )
        {
            _listTagCache.push_back( getEntryCategory( entry ) );
        }
        VectorUtil::sortUnique( _listTagCache );
        _tagCacheGeneration = _snapshotGeneration;
        return _listTagCache;
    }

    void ConsolePanel::openEntryInIde( const LogEntry& entry )
    {
        EditorSourceLocation location;
        if ( EditorLogCommands::findLogEntryLocation( entry._message, entry._file, entry._line, location ) )
            (void)EditorLogCommands::openInIde( location ); // 실패는 openInIde 가 경고로 남긴다
    }

    void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
    {
        _logListenerSubscription.reset();
    }

    void ConsolePanel::onLogWritten( const LogEntry& entry )
    {
        // 어느 스레드 · 어느 스코프에서 쓴 로그든 콘솔이 보관하고 있는 사본은 에디터 담당이다.
        SW_MEMORY_SCOPE( Editor );
        ScopedLock<mutex> lock{ _entriesMutex };
        _listEntry.push_back( entry );
        ++_entrySerial;
        if ( entry._level == LogLevel::Error )
            ++_errorSerial;
        while ( _listEntry.size() > constant::kMaxBuffer2048 )
        {
            _listEntry.pop_front();
        }
        _bHasNewLogs = SW_TRUE;
    }

} // namespace sw::editor
