#include "pch.h"

#include "Editor/Panels/ProfilerPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		void formatBytes( uint64 bytes, utf8* pOut, size_t outSize )
		{
			const uint32 bufSize = static_cast<uint32>( outSize );
			if ( bytes < 1024 )
				formatstring( pOut, bufSize, "%llu B", bytes );
			else if ( bytes < 1024 * 1024 )
				formatstring( pOut, bufSize, "%.2f KB", static_cast<float64>( bytes ) / 1024.0 );
			else if ( bytes < 1024 * 1024 * 1024 )
				formatstring( pOut, bufSize, "%.2f MB", static_cast<float64>( bytes ) / ( 1024.0 * 1024.0 ) );
			else
				formatstring( pOut, bufSize, "%.2f GB",
							  static_cast<float64>( bytes ) / ( 1024.0 * 1024.0 * 1024.0 ) );
		}
	} // namespace

	ProfilerPanel::ProfilerPanel()
		: IEditorPanel( false ) // starts closed
	{
	}

	void ProfilerPanel::drawContent()
	{
		if ( ImGui::BeginTabBar( "ProfilerTabs" ) )
		{
			if ( ImGui::BeginTabItem( "Performance & Scene" ) )
			{
				drawPerformanceTab();
				ImGui::EndTabItem();
			}
			if ( ImGui::BeginTabItem( "Memory" ) )
			{
				drawMemoryTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	void ProfilerPanel::drawPerformanceTab()
	{
		const float32 dt		  = ImGui::GetIO().DeltaTime;
		const float32 frameTimeMs = dt * 1000.0f;
		const float32 fps		  = ( dt > 0.0f ) ? ( 1.0f / dt ) : 0.0f;

		_arrFrameTimeHistory[_historyOffset] = frameTimeMs;
		_historyOffset						 = ( _historyOffset + 1 ) % 120;

		float32 minMs{ MathUtil::MaxFloat };
		float32 maxMs{ 0.0f };
		float32 sumMs{ 0.0f };
		for ( size_t historyIndex = 0; historyIndex < 120; ++historyIndex )
		{
			const float32 val = _arrFrameTimeHistory[historyIndex];
			if ( val > 0.0f )
			{
				minMs = MathUtil::min( minMs, val );
				maxMs = MathUtil::max( maxMs, val );
				sumMs += val;
			}
		}
		const float32 avgMs	 = sumMs / 120.0f;
		const float32 avgFps = ( avgMs > 0.0f ) ? ( 1000.0f / avgMs ) : 0.0f;

		ImGui::Text( "Current FPS: %.1f (%.2f ms)", static_cast<float64>( fps ), static_cast<float64>( frameTimeMs ) );
		ImGui::Text( "Average FPS: %.1f (Avg: %.2f ms, Min: %.2f ms, Max: %.2f ms)",
					 static_cast<float64>( avgFps ), static_cast<float64>( avgMs ),
					 static_cast<float64>( minMs ), static_cast<float64>( maxMs ) );

		ImGui::PlotLines( "Frame Time (ms)", _arrFrameTimeHistory, 120, static_cast<int32>( _historyOffset ),
						  nullptr, 0.0f, 33.3f, ImVec2{ 0.0f, 80.0f } );

		ImGui::Separator();

		if ( ImGui::CollapsingHeader( "Active Scene & Component Distribution", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			SceneManager* pSceneManager = editor::getService<SceneManager>();
			Scene*		  pScene		= ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
			if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
			{
				GameObjectManager*		   pManager	   = pScene->getObjectManager();
				const vector<GameObject*>& listObjects = pManager->getAllGameObjects();

				size_t rootCount{ 0 };
				size_t totalComponents{ 0 };
				size_t sceneCompCount{ 0 };
				size_t meshCompCount{ 0 };
				size_t spriteCompCount{ 0 };
				size_t box2dCompCount{ 0 };
				size_t cameraCompCount{ 0 };

				for ( const GameObject* pObj : listObjects )
				{
					if ( pObj == nullptr )
						continue;
					if ( pObj->getParent() == nullptr )
						++rootCount;
					totalComponents += pObj->getComponentCount();

					if ( pObj->getComponent<SceneComponent>() != nullptr )
						++sceneCompCount;
					if ( pObj->getComponent<MeshComponent>() != nullptr )
						++meshCompCount;
					if ( pObj->getComponent<SpriteComponent>() != nullptr )
						++spriteCompCount;
					if ( pObj->getComponent<BoxCollider2DComponent>() != nullptr )
						++box2dCompCount;
					if ( pObj->getComponent<CameraComponent>() != nullptr )
						++cameraCompCount;
				}

				ImGui::BulletText( "Total GameObjects: %zu (Roots: %zu)", listObjects.size(), rootCount );
				ImGui::BulletText( "Total Attached Components: %zu", totalComponents );

				if ( ImGui::BeginTable( "CompDistributionTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
				{
					ImGui::TableSetupColumn( "Component Type" );
					ImGui::TableSetupColumn( "Active Instances" );
					ImGui::TableHeadersRow();

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "SceneComponent" );
					ImGui::TableNextColumn();
					ImGui::Text( "%zu", sceneCompCount );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "MeshComponent" );
					ImGui::TableNextColumn();
					ImGui::Text( "%zu", meshCompCount );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "SpriteComponent" );
					ImGui::TableNextColumn();
					ImGui::Text( "%zu", spriteCompCount );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "BoxCollider2DComponent" );
					ImGui::TableNextColumn();
					ImGui::Text( "%zu", box2dCompCount );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "CameraComponent" );
					ImGui::TableNextColumn();
					ImGui::Text( "%zu", cameraCompCount );

					ImGui::EndTable();
				}
			}
			else
			{
				ImGui::TextDisabled( "No active scene loaded." );
			}
		}

		ImGui::Separator();

		if ( ImGui::CollapsingHeader( "Resource Catalog Summary", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			static size_t s_sceneCount{ 0 };
			static size_t s_prefabCount{ 0 };
			static size_t s_textureCount{ 0 };
			static size_t s_shaderCount{ 0 };
			static bool	  s_bScanned{ false };

			if ( s_bScanned == false || ImGui::Button( "Scan Resources" ) )
			{
				vector<string> listScenes, listPrefabs, listTextures, listShaders;
				const string   resPath = FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource" );
				FileUtil::collectFiles( resPath, ".scene.xml", listScenes, true, false );
				FileUtil::collectFiles( resPath, ".prefab.xml", listPrefabs, true, false );
				FileUtil::collectFiles( resPath, ".png", listTextures, true, false );
				FileUtil::collectFiles( resPath, ".hlsl", listShaders, true, false );

				s_sceneCount   = listScenes.size();
				s_prefabCount  = listPrefabs.size();
				s_textureCount = listTextures.size();
				s_shaderCount  = listShaders.size();
				s_bScanned	   = true;
			}

			ImGui::BulletText( "Scenes (.scene.xml): %zu", s_sceneCount );
			ImGui::BulletText( "Prefabs (.prefab.xml): %zu", s_prefabCount );
			ImGui::BulletText( "Textures (.png): %zu", s_textureCount );
			ImGui::BulletText( "Shaders (.hlsl): %zu", s_shaderCount );
		}

		ImGui::Separator();

		if ( ImGui::CollapsingHeader( "Task Manager & Concurrency", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			TaskManager* pTaskManager = editor::getService<TaskManager>();
			if ( pTaskManager != nullptr )
			{
				ImGui::BulletText( "Worker Threads: %u", pTaskManager->getWorkerCount() );
				ImGui::BulletText( "Task System: Active" );
			}
			else
			{
				ImGui::TextDisabled( "TaskManager is not active." );
			}
		}
	}

	void ProfilerPanel::drawMemoryTab()
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
			if ( ImGui::BeginTable( "MemoryStatsTable", 4,
									ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
			{
				ImGui::TableSetupColumn( "Tag" );
				ImGui::TableSetupColumn( "Current Bytes" );
				ImGui::TableSetupColumn( "Current Count" );
				ImGui::TableSetupColumn( "Total Allocated" );
				ImGui::TableHeadersRow();

				utf8 arrBytesBuf[32];
				utf8 arrTotalBuf[32];

				for ( uint32 tagIndex = 0; tagIndex < static_cast<uint32>( MemoryTag::MaxTags ); ++tagIndex )
				{
					MemoryTag	tag	  = static_cast<MemoryTag>( tagIndex );
					const auto& stats = profiler.getStats( tag );

					formatBytes( stats._currentAllocatedBytes.load(), arrBytesBuf, sizeof( arrBytesBuf ) );
					formatBytes( stats._totalAllocatedBytes.load(), arrTotalBuf, sizeof( arrTotalBuf ) );

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text( "%s", MemoryProfiler::getMemoryTagName( tag ) );
					ImGui::TableNextColumn();
					ImGui::Text( "%s", arrBytesBuf );
					ImGui::TableNextColumn();
					ImGui::Text( "%llu", stats._currentAllocationCount.load() );
					ImGui::TableNextColumn();
					ImGui::Text( "%s", arrTotalBuf );
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
						string treeLabel = "Stack Hash: " + to_string( info._stack._hash );
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
} // namespace sw::editor
