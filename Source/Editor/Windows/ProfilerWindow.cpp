#include "pch.h"

#include "Editor/Windows/ProfilerWindow.h"

#include "Core/Memory/MemoryProfiler.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw
{
	ProfilerWindow::ProfilerWindow()
		: IEditorWindow( false ) // starts closed
	{
	}

	void ProfilerWindow::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( isOpen() == false )
			return;

		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) )
		{
			if ( ImGui::BeginTabBar( "ProfilerTabs" ) )
			{
				if ( ImGui::BeginTabItem( "Memory" ) )
				{
					drawMemoryTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void ProfilerWindow::drawMemoryTab()
	{
		MemoryProfiler* pProfiler = editor::getService<MemoryProfiler>();
		if ( pProfiler == nullptr )
		{
			ImGui::TextDisabled( "MemoryProfiler is not active." );
			return;
		}
		MemoryProfiler& profiler = *pProfiler;

		bool bTracking = profiler.isTrackingEnabled();
		if ( ImGui::Checkbox( "Enable Memory Tracking", &bTracking ) )
			profiler.setTrackingEnabled( bTracking );

		bool bDetailed = profiler.isDetailedTrackingEnabled();
		if ( ImGui::Checkbox( "Enable Detailed CallStack Tracking (High Overhead)", &bDetailed ) )
			profiler.setDetailedTrackingEnabled( bDetailed );

		ImGui::Separator();

		if ( ImGui::CollapsingHeader( "Global Statistics By Tag", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			if ( ImGui::BeginTable( "MemoryStatsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
			{
				ImGui::TableSetupColumn( "Tag" );
				ImGui::TableSetupColumn( "Current Bytes" );
				ImGui::TableSetupColumn( "Current Count" );
				ImGui::TableSetupColumn( "Total Allocated" );
				ImGui::TableHeadersRow();

				for ( uint32 tagIndex = 0; tagIndex < static_cast<uint32>( MemoryTag::MaxTags ); ++tagIndex )
				{
					MemoryTag	tag	  = static_cast<MemoryTag>( tagIndex );
					const auto& stats = profiler.getStats( tag );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "%s", MemoryProfiler::getMemoryTagName( tag ) );
					ImGui::TableNextColumn();
					ImGui::Text( "%llu", stats._currentAllocatedBytes.load() );
					ImGui::TableNextColumn();
					ImGui::Text( "%llu", stats._currentAllocationCount.load() );
					ImGui::TableNextColumn();
					ImGui::Text( "%llu", stats._totalAllocatedBytes.load() );
				}
				ImGui::EndTable();
			}
		}

		ImGui::Separator();

		if ( bDetailed && ImGui::CollapsingHeader( "Top Memory Allocations By Call Stack", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			vector<CallStackAllocInfo> listTopStacks = profiler.getTopCallStacks();

			if ( listTopStacks.empty() )
				ImGui::Text( "No detailed call stack data available or all freed." );
			else
			{
				if ( ImGui::BeginTable( "CallStackTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
				{
					ImGui::TableSetupColumn( "Bytes (Count)" );
					ImGui::TableSetupColumn( "Call Stack" );
					ImGui::TableHeadersRow();

					int32 displayCount{ 0 };
					for ( const auto& info : listTopStacks )
					{
						if ( displayCount++ > 100 )
							break; // 최대 100개만 표시

						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text( "%llu B (%llu allocs)", info._currentBytes, info._currentCount );

						ImGui::TableNextColumn();

						// CallStack을 펼쳐볼 수 있도록 Tree 구성
						string treeLabel = "Stack Hash: " + to_string( info._stack.hash );
						if ( ImGui::TreeNode( treeLabel.c_str() ) )
						{
							string stackStr = CallStackCapture::symbolize( info._stack );
							ImGui::TextUnformatted( stackStr.c_str() );
							ImGui::TreePop();
						}
					}
					ImGui::EndTable();
				}
			}
		}
		else if ( bDetailed == false )
			ImGui::Text( "Detailed CallStack tracking is disabled." );
	}

} // namespace sw
