#include "pch.h"

#include "Editor/Windows/ConsoleWindow.h"

#include "Core/Concurrency/mutex.h"

#include <imgui.h>

namespace sw
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
			static constexpr const utf8* kNames[] = { "Error", "Warning", "Info", "Trace" };
			const uint8					 idx	  = static_cast<uint8>( level );
			if ( idx >= 4 )
				return "Info";
			return kNames[idx];
		}

	} // namespace

	ConsoleWindow::ConsoleWindow()
		: _listEntries{}
		, _listDrawSnapshot{}
		, _entriesMutex{}
		, _logListenerHandle{}
		, _arrFilterBuffer{}
		, _bAutoScroll{ true }
		, _arrLevelEnabled{ true, true, true, true }
		, _bHasNewLogs{ 1 }
		, _reservedFlags{ 0 }
	{
		_logListenerHandle = Logger::addGlobalListener(
			SW_DELEGATE_METHOD( LogWrittenDelegate, &ConsoleWindow::onLogWritten, this ) );
	}

	ConsoleWindow::~ConsoleWindow()
	{
		unsubscribe();
	}

	void ConsoleWindow::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		bool bNewLogs{ false };
		{
			std::scoped_lock<mutex> lock{ _entriesMutex };
			if ( _bHasNewLogs )
			{
				_listDrawSnapshot.assign( _listEntries.begin(), _listEntries.end() );
				bNewLogs	 = true;
				_bHasNewLogs = false;
			}
		}

		if ( ImGui::Button( "Clear" ) )
		{
			std::scoped_lock<mutex> lock{ _entriesMutex };
			_listEntries.clear();
			_listDrawSnapshot.clear();
			_bHasNewLogs = false;
			bNewLogs	 = false;
		}
		ImGui::SameLine();
		ImGui::Checkbox( "Auto-scroll", &_bAutoScroll );

		ImGui::SameLine();
		ImGui::Checkbox( "Error", &_arrLevelEnabled[0] );
		ImGui::SameLine();
		ImGui::Checkbox( "Warning", &_arrLevelEnabled[1] );
		ImGui::SameLine();
		ImGui::Checkbox( "Info", &_arrLevelEnabled[2] );
		ImGui::SameLine();
		ImGui::Checkbox( "Trace", &_arrLevelEnabled[3] );

		ImGui::SetNextItemWidth( -1.0f );
		ImGui::InputTextWithHint( "##log_filter", "Filter (tag / message / file)", _arrFilterBuffer, sizeof( _arrFilterBuffer ) );

		ImGui::Separator();

		const string filterStr = StringUtil::trim( _arrFilterBuffer );

		vector<const LogEntry*> listVisible;
		listVisible.reserve( _listDrawSnapshot.size() );
		for ( const LogEntry& entry : _listDrawSnapshot )
		{
			const uint8 levelIdx = static_cast<uint8>( entry.level );
			if ( levelIdx >= 4 || _arrLevelEnabled[levelIdx] == false )
				continue;

			if ( filterStr.empty() == false )
			{
				auto contains = [&]( const string& text )
				{
					if ( text.empty() )
						return false;
					return StringUtil::stristr( text.c_str(), filterStr.c_str() ) != nullptr;
				};

				if ( contains( entry.message ) == false && contains( entry.tag ) == false && contains( entry.file ) == false )
					continue;
			}

			listVisible.push_back( &entry );
		}

		ImGui::BeginChild( "##log_scroll", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar );

		ImGuiListClipper clipper;
		clipper.Begin( static_cast<int32>( listVisible.size() ) );
		while ( clipper.Step() )
		{
			for ( int32 logIndex = clipper.DisplayStart; logIndex < clipper.DisplayEnd; ++logIndex )
			{
				const LogEntry& entry = *listVisible[static_cast<size_t>( logIndex )];
				ImGui::PushID( logIndex );

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

		ImGui::TextDisabled( "%d / %d lines", static_cast<int32>( listVisible.size() ), static_cast<int32>( _listDrawSnapshot.size() ) );

		ImGui::End();
	}

	void ConsoleWindow::shutdown( IRHIDevice* /*rhiDevice*/ )
	{
		unsubscribe();
	}

	void ConsoleWindow::onLogWritten( const LogEntry& entry )
	{
		std::scoped_lock<mutex> lock{ _entriesMutex };
		_listEntries.push_back( entry );
		while ( _listEntries.size() > constant::kMaxBuffer2048 )
		{
			_listEntries.pop_front();
		}
		_bHasNewLogs = true;
	}

	void ConsoleWindow::unsubscribe()
	{
		if ( _logListenerHandle.isValid() )
		{
			Logger::removeGlobalListener( _logListenerHandle );
			_logListenerHandle = {};
		}
	}
} // namespace sw
