#include "pch.h"

#include "Editor/Panels/ConsolePanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		ImVec4 colorForLevel( LogLevel level )
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
				default:
					return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
			}
		}

		const utf8* levelName( LogLevel level )
		{
			static constexpr const utf8* kArrNames[] = { "Error", "Warning", "Info", "Trace" };
			const uint8					 index		 = static_cast<uint8>( level );
			if ( index >= 4 )
				return "Info";
			return kArrNames[index];
		}

	} // namespace

	ConsolePanel::ConsolePanel()
		: _listEntry{}
		, _listDrawSnapshot{}
		, _listVisible{}
		, _cachedFilter{}
		, _entriesMutex{}
		, _logListenerHandle{}
		, _arrFilterBuffer{}
		, _arrLevelEnabled{ true, true, true, true }
		, _arrCachedLevelEnabled{ true, true, true, true }
		, _bAutoScroll{ true }
		, _bHasNewLogs{ SW_TRUE }
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
		_listVisible.clear();
		_listVisible.reserve( _listDrawSnapshot.size() );

		for ( const LogEntry& entry : _listDrawSnapshot )
		{
			const uint8 levelIndex = static_cast<uint8>( entry._level );
			if ( levelIndex >= 4 || _arrLevelEnabled[levelIndex] == false )
				continue;

			if ( filterStr.empty() == false )
			{
				auto contains = [&]( const string& text )
				{
					if ( text.empty() )
						return false;
					return StringUtil::stristr( text.c_str(), filterStr.c_str() ) != nullptr;
				};

				if ( contains( entry._message ) == false && contains( entry._tag ) == false && contains( entry._file ) == false )
					continue;
			}

			_listVisible.push_back( &entry );
		}

		_cachedFilter = filterStr;
		for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
			_arrCachedLevelEnabled[levelIndex] = _arrLevelEnabled[levelIndex];
	}

	void ConsolePanel::drawContent()
	{
		bool bNewLogs{ false };
		{
			std::scoped_lock<mutex> lock{ _entriesMutex };
			if ( _bHasNewLogs == SW_TRUE )
			{
				_listDrawSnapshot.assign( _listEntry.begin(), _listEntry.end() );
				bNewLogs	 = true;
				_bHasNewLogs = SW_FALSE;
			}
		}

		size_t errorCount{ 0 };
		size_t warnCount{ 0 };
		size_t infoCount{ 0 };
		size_t traceCount{ 0 };
		for ( const LogEntry& entry : _listDrawSnapshot )
		{
			switch ( entry._level )
			{
				case LogLevel::Error:
					++errorCount;
					break;
				case LogLevel::Warning:
					++warnCount;
					break;
				case LogLevel::Info:
					++infoCount;
					break;
				case LogLevel::Trace:
					++traceCount;
					break;
				case LogLevel::Count:
				default:
					break;
			}
		}

		if ( editor::beginToolbar( "##ConsoleToolbar" ) )
		{
			if ( ImGui::Button( "Clear" ) )
			{
				std::scoped_lock<mutex> lock{ _entriesMutex };
				_listEntry.clear();
				_listDrawSnapshot.clear();
				_listVisible.clear();
				_bHasNewLogs = SW_FALSE;
				bNewLogs	 = false;
			}
			ImGui::SameLine();
			ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );

			utf8 arrErrLabel[32], arrWarnLabel[32], arrInfoLabel[32], arrTraceLabel[32];
			formatstring( arrErrLabel, sizeof( arrErrLabel ), "Error (%zu)", errorCount );
			formatstring( arrWarnLabel, sizeof( arrWarnLabel ), "Warning (%zu)", warnCount );
			formatstring( arrInfoLabel, sizeof( arrInfoLabel ), "Info (%zu)", infoCount );
			formatstring( arrTraceLabel, sizeof( arrTraceLabel ), "Trace (%zu)", traceCount );

			constexpr editor::Color4 kTraceChip{ 0.40f, 0.40f, 0.45f, 1.0f };

			ImGui::SameLine();
			if ( editor::drawToggleButton( arrErrLabel, _arrLevelEnabled[0], editor::style::kError ) )
				_arrLevelEnabled[0] = ( _arrLevelEnabled[0] == false );

			ImGui::SameLine();
			if ( editor::drawToggleButton( arrWarnLabel, _arrLevelEnabled[1], editor::style::kWarn ) )
				_arrLevelEnabled[1] = ( _arrLevelEnabled[1] == false );

			ImGui::SameLine();
			if ( editor::drawToggleButton( arrInfoLabel, _arrLevelEnabled[2] ) )
				_arrLevelEnabled[2] = ( _arrLevelEnabled[2] == false );

			ImGui::SameLine();
			if ( editor::drawToggleButton( arrTraceLabel, _arrLevelEnabled[3], kTraceChip ) )
				_arrLevelEnabled[3] = ( _arrLevelEnabled[3] == false );

			ImGui::SameLine();
			if ( ImGui::Button( "Copy All" ) && _listVisible.empty() == false )
			{
				string allLogs;
				for ( const LogEntry* pEntry : _listVisible )
				{
					if ( pEntry != nullptr )
					{
						allLogs += "[" + pEntry->_timeStamp + "] [" + pEntry->_tag + "] [" + levelName( pEntry->_level ) +
								   "] - " + pEntry->_message + "\n";
					}
				}
				ImGui::SetClipboardText( allLogs.c_str() );
			}

			editor::drawSearchField( "##log_filter", _arrFilterBuffer, sizeof( _arrFilterBuffer ),
									 "Filter (tag / message / file)", -1.0f, false );
		}
		editor::endToolbar();

		ImGui::Separator();

		const string filterStr = StringUtil::trim( _arrFilterBuffer );

		// 필터 또는 레벨 설정이 바뀌었거나 새 로그가 들어왔을 때만 재계산
		bool bLevelChanged{ false };
		for ( int32 levelIndex = 0; levelIndex < 4; ++levelIndex )
		{
			if ( _arrCachedLevelEnabled[levelIndex] != _arrLevelEnabled[levelIndex] )
			{
				bLevelChanged = true;
				break;
			}
		}

		const bool bFilterChanged = ( filterStr != _cachedFilter );

		if ( bNewLogs || bFilterChanged || bLevelChanged )
		{
			updateFilteredEntries( filterStr );
		}

		editor::EditorSectionDesc logDesc{};
		logDesc._pId   = "##log_scroll";
		logDesc._kind  = editor::EditorSectionKind::Child;
		logDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining |
						 editor::EditorSectionFlags::HorizontalScrollbar;
		editor::beginSection( logDesc );

		ImGuiListClipper clipper;
		clipper.Begin( static_cast<int32>( _listVisible.size() ) );
		while ( clipper.Step() )
		{
			for ( int32 logIndex = clipper.DisplayStart; logIndex < clipper.DisplayEnd; ++logIndex )
			{
				const LogEntry& entry = *_listVisible[static_cast<size_t>( logIndex )];
				ImGui::PushID( logIndex );

				ImGui::TextDisabled( "[%s]", entry._timeStamp.c_str() );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( ImVec4( 0.45f, 0.85f, 0.95f, 1.0f ), " [%s]", entry._tag.c_str() );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( colorForLevel( entry._level ), " [%s]", levelName( entry._level ) );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextUnformatted( " - " );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextUnformatted( entry._message.c_str() );

				if ( ImGui::IsItemHovered() && entry._file.empty() == false )
					ImGui::SetTooltip( "%s(%d)", entry._file.c_str(), entry._line );

				if ( ImGui::BeginPopupContextItem( "LogEntryCtx" ) )
				{
					if ( ImGui::MenuItem( "Copy Message" ) )
						ImGui::SetClipboardText( entry._message.c_str() );
					if ( ImGui::MenuItem( "Copy Full Log Line" ) )
					{
						string full = "[" + entry._timeStamp + "] [" + entry._tag + "] [" + levelName( entry._level ) +
									  "] - " + entry._message;
						if ( entry._file.empty() == false )
							full += " (" + entry._file + ":" + to_string( entry._line ) + ")";
						ImGui::SetClipboardText( full.c_str() );
					}
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}
		}

		if ( _bAutoScroll && bNewLogs )
			ImGui::SetScrollHereY( 1.0f );

		editor::endSection();

		editor::drawCountLabel( static_cast<uint32>( _listVisible.size() ), static_cast<uint32>( _listDrawSnapshot.size() ),
								"lines" );
	}

	void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		unsubscribe();
	}

	void ConsolePanel::onLogWritten( const LogEntry& entry )
	{
		std::scoped_lock<mutex> lock{ _entriesMutex };
		_listEntry.push_back( entry );
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
