/**
 * @file ConsolePanel.cpp
 * @brief Logger::addLogWrittenListener 로 구독해 표시하는 Output Log 패널
 */
#include "Panels/ConsolePanel.h"
#include "Core/Common/CommonDefines.h"
#include <algorithm>
#include <imgui.h>

namespace sw
{
	namespace
	{
		ImVec4 colorForLevel( Logger::LogLevel level )
		{
			switch ( level )
			{
				case Logger::LogLevel::Error:
					return ImVec4( 1.0f, 0.35f, 0.35f, 1.0f );
				case Logger::LogLevel::Warning:
					return ImVec4( 1.0f, 0.85f, 0.25f, 1.0f );
				case Logger::LogLevel::Info:
					return ImVec4( 0.75f, 0.85f, 1.0f, 1.0f );
				case Logger::LogLevel::Trace:
					return ImVec4( 0.55f, 0.55f, 0.60f, 1.0f );
				case Logger::LogLevel::Count:
				default:
					return ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
			}
		}

		const char* levelName( Logger::LogLevel level )
		{
			static constexpr const char* kNames[] = { "Error", "Warning", "Info", "Trace" };
			const uint8 idx = static_cast<uint8>( level );
			if ( idx >= 4 )
				return "Info";
			return kNames[idx];
		}
	}

	ConsolePanel::ConsolePanel()
	{
		_logListenerHandle = Logger::addLogWrittenListener(
			SW_DELEGATE_METHOD( Logger::LogWrittenDelegate, &ConsolePanel::onLogWritten, this ) );
	}

	ConsolePanel::~ConsolePanel()
	{
		unsubscribe();
	}

	void ConsolePanel::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		unsubscribe();
	}

	void ConsolePanel::unsubscribe()
	{
		if ( _logListenerHandle.isValid() )
		{
			Logger::removeLogWrittenListener( _logListenerHandle );
			_logListenerHandle = {};
		}
	}

	void ConsolePanel::onLogWritten( const Logger::LogEntry& entry )
	{
		std::lock_guard<std::mutex> lock( _entriesMutex );
		_entries.push_back( entry );
		while ( _entries.size() > constant::kMaxBuffer2048 )
			_entries.pop_front();
		_bHasNewLogs = true;
	}

	void ConsolePanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		bool bNewLogs = false;
		{
			std::lock_guard<std::mutex> lock( _entriesMutex );
			_drawSnapshot.assign( _entries.begin(), _entries.end() );
			bNewLogs	 = _bHasNewLogs;
			_bHasNewLogs = false;
		}

		if ( ImGui::Button( "Clear" ) )
		{
			std::lock_guard<std::mutex> lock( _entriesMutex );
			_entries.clear();
			_drawSnapshot.clear();
			_bHasNewLogs = false;
			bNewLogs	 = false;
		}
		ImGui::SameLine();
		ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );

		ImGui::SameLine();
		ImGui::Checkbox( "Error", &_levelEnabled[0] );
		ImGui::SameLine();
		ImGui::Checkbox( "Warning", &_levelEnabled[1] );
		ImGui::SameLine();
		ImGui::Checkbox( "Info", &_levelEnabled[2] );
		ImGui::SameLine();
		ImGui::Checkbox( "Trace", &_levelEnabled[3] );

		ImGui::SetNextItemWidth( -1.0f );
		ImGui::InputTextWithHint( "##log_filter", "Filter (tag / message / file)", _filterBuffer, sizeof( _filterBuffer ) );

		ImGui::Separator();

		std::string filterLower = _filterBuffer;
		std::transform( filterLower.begin(), filterLower.end(), filterLower.begin(), []( unsigned char c )
		{ return static_cast<char>( std::tolower( c ) ); } );

		std::vector<const Logger::LogEntry*> visible;
		visible.reserve( _drawSnapshot.size() );
		for ( const Logger::LogEntry& entry : _drawSnapshot )
		{
			const uint8 levelIdx = static_cast<uint8>( entry.level );
			if ( levelIdx >= 4 || _levelEnabled[levelIdx] == false )
				continue;

			if ( filterLower.empty() == false )
			{
				auto contains = [&]( const std::string& text )
				{
					std::string lower = text;
					std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c )
					{ return static_cast<char>( std::tolower( c ) ); } );
					return lower.find( filterLower ) != std::string::npos;
				};

				if ( contains( entry.message ) == false && contains( entry.tag ) == false && contains( entry.file ) == false )
					continue;
			}

			visible.push_back( &entry );
		}

		ImGui::BeginChild( "##log_scroll", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar );

		ImGuiListClipper clipper;
		clipper.Begin( static_cast<int>( visible.size() ) );
		while ( clipper.Step() )
		{
			for ( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i )
			{
				const Logger::LogEntry& entry = *visible[static_cast<size_t>( i )];
				ImGui::PushID( i );

				// 콘솔 출력과 동일 순서: [date] [tag] [level] - message
				ImGui::TextDisabled( "[%s]", entry.timeStamp.c_str() );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( ImVec4( 0.45f, 0.85f, 0.95f, 1.0f ), " [%s]", entry.tag.c_str() );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( colorForLevel( entry.level ), " [%s]", levelName( entry.level ) );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextUnformatted( " - " );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextUnformatted( entry.message.c_str() );

				if ( ImGui::IsItemHovered() && entry.file.empty() == false )
					ImGui::SetTooltip( "%s(%d)", entry.file.c_str(), entry.line );

				ImGui::PopID();
			}
		}

		if ( _bAutoScroll && bNewLogs )
			ImGui::SetScrollHereY( 1.0f );

		ImGui::EndChild();

		ImGui::TextDisabled( "%d / %d lines", static_cast<int>( visible.size() ), static_cast<int>( _drawSnapshot.size() ) );

		ImGui::End();
	}
}
