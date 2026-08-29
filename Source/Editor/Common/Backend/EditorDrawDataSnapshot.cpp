#include "pch.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"

#include "Core/Container/vector.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		struct EditorDrawDataSnapshotInternal
		{
			static void cloneDrawData( const ImDrawData* pSrc, ImDrawData& outDrawData, vector<ImDrawList*>& outOwnedList )
			{
				outDrawData.Clear();
				for ( ImDrawList* pOwned : outOwnedList )
					IM_DELETE( pOwned );
				outOwnedList.clear();

				if ( pSrc == nullptr || pSrc->Valid == false )
					return;

				outDrawData.Valid			 = true;
				outDrawData.DisplayPos		 = pSrc->DisplayPos;
				outDrawData.DisplaySize		 = pSrc->DisplaySize;
				outDrawData.FramebufferScale = pSrc->FramebufferScale;
				outDrawData.Textures		 = pSrc->Textures;
				outDrawData.OwnerViewport	 = nullptr;
				outDrawData.TotalIdxCount	 = pSrc->TotalIdxCount;
				outDrawData.TotalVtxCount	 = pSrc->TotalVtxCount;

				outOwnedList.reserve( static_cast<size_t>( pSrc->CmdLists.Size ) );
				for ( int32 listIndex = 0; listIndex < pSrc->CmdLists.Size; ++listIndex )
				{
					ImDrawList* pSrcList = pSrc->CmdLists[listIndex];
					if ( pSrcList == nullptr )
						continue;
					ImDrawList* pClone = pSrcList->CloneOutput();
					if ( pClone == nullptr )
						continue;
					outOwnedList.push_back( pClone );
					outDrawData.AddDrawList( pClone );
				}

				outDrawData.TotalIdxCount = pSrc->TotalIdxCount;
				outDrawData.TotalVtxCount = pSrc->TotalVtxCount;
			}

			static void destroyOwnedLists( ImDrawData& drawData, vector<ImDrawList*>& listOwned )
			{
				drawData.Clear();
				for ( ImDrawList* pOwned : listOwned )
					IM_DELETE( pOwned );
				listOwned.clear();
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	struct EditorDrawDataSnapshot::Impl
	{
		struct ExtraViewportClone
		{
			ImDrawData			_drawData;
			vector<ImDrawList*> _listOwned;
			ImGuiID				_id;
			ImGuiViewportFlags	_flags;
			ImVec2				_pos;
			ImVec2				_size;
			ImVec2				_framebufferScale;
			void*				_pRendererUserData;
			void*				_pPlatformUserData;
			void*				_pPlatformHandle;
			void*				_pPlatformHandleRaw;
		};

		ImDrawData							   _mainDrawData;
		vector<ImDrawList*>					   _listMainOwned;
		vector<unique_ptr<ExtraViewportClone>> _listExtraViewport;
		void ( *_pPlatformRenderWindow )( ImGuiViewport*, void* );
		void ( *_pRendererRenderWindow )( ImGuiViewport*, void* );
		void ( *_pPlatformSwapBuffers )( ImGuiViewport*, void* );
		uint8				   _bValid	 : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};

	EditorDrawDataSnapshot::EditorDrawDataSnapshot()
		: _pImpl{ make_unique<Impl>() }
	{
		_pImpl->_pPlatformRenderWindow = nullptr;
		_pImpl->_pRendererRenderWindow = nullptr;
		_pImpl->_pPlatformSwapBuffers  = nullptr;
		_pImpl->_bValid				   = SW_FALSE;
		_pImpl->_reserved			   = 0;
	}

	EditorDrawDataSnapshot::~EditorDrawDataSnapshot()
	{
		clear();
	}

	void EditorDrawDataSnapshot::clear()
	{
		if ( _pImpl == nullptr )
			return;

		EditorDrawDataSnapshotInternal::destroyOwnedLists( _pImpl->_mainDrawData, _pImpl->_listMainOwned );
		for ( unique_ptr<Impl::ExtraViewportClone>& pExtra : _pImpl->_listExtraViewport )
		{
			if ( pExtra == nullptr )
				continue;
			EditorDrawDataSnapshotInternal::destroyOwnedLists( pExtra->_drawData, pExtra->_listOwned );
		}
		_pImpl->_listExtraViewport.clear();
		_pImpl->_pPlatformRenderWindow = nullptr;
		_pImpl->_pRendererRenderWindow = nullptr;
		_pImpl->_pPlatformSwapBuffers  = nullptr;
		_pImpl->_bValid				   = SW_FALSE;
	}

	void EditorDrawDataSnapshot::capture()
	{
		clear();
		if ( ImGui::GetCurrentContext() == nullptr )
			return;

		EditorDrawDataSnapshotInternal::cloneDrawData( ImGui::GetDrawData(), _pImpl->_mainDrawData, _pImpl->_listMainOwned );
		if ( _pImpl->_mainDrawData.Valid == false )
			return;

		_pImpl->_bValid = SW_TRUE;

		ImGuiIO& io = ImGui::GetIO();
		if ( ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) == 0 )
			return;

		ImGuiPlatformIO& platformIo	   = ImGui::GetPlatformIO();
		_pImpl->_pPlatformRenderWindow = platformIo.Platform_RenderWindow;
		_pImpl->_pRendererRenderWindow = platformIo.Renderer_RenderWindow;
		_pImpl->_pPlatformSwapBuffers  = platformIo.Platform_SwapBuffers;

		for ( int32 viewportIndex = 1; viewportIndex < platformIo.Viewports.Size; ++viewportIndex )
		{
			ImGuiViewport* pViewport = platformIo.Viewports[viewportIndex];
			if ( pViewport == nullptr )
				continue;
			if ( ( pViewport->Flags & ImGuiViewportFlags_IsMinimized ) != 0 )
				continue;
			if ( pViewport->DrawData == nullptr || pViewport->DrawData->Valid == false )
				continue;

			unique_ptr<Impl::ExtraViewportClone> pExtra = make_unique<Impl::ExtraViewportClone>();
			pExtra->_id									= pViewport->ID;
			pExtra->_flags								= pViewport->Flags;
			pExtra->_pos								= pViewport->Pos;
			pExtra->_size								= pViewport->Size;
			pExtra->_framebufferScale					= pViewport->FramebufferScale;
			pExtra->_pRendererUserData					= pViewport->RendererUserData;
			pExtra->_pPlatformUserData					= pViewport->PlatformUserData;
			pExtra->_pPlatformHandle					= pViewport->PlatformHandle;
			pExtra->_pPlatformHandleRaw					= pViewport->PlatformHandleRaw;
			EditorDrawDataSnapshotInternal::cloneDrawData( pViewport->DrawData, pExtra->_drawData, pExtra->_listOwned );
			_pImpl->_listExtraViewport.push_back( std::move( pExtra ) );
		}
	}

	bool EditorDrawDataSnapshot::isValid() const
	{
		return _pImpl != nullptr && _pImpl->_bValid == SW_TRUE;
	}

	ImDrawData* EditorDrawDataSnapshot::getMainDrawData()
	{
		if ( isValid() == false )
			return nullptr;
		return &_pImpl->_mainDrawData;
	}

	void EditorDrawDataSnapshot::presentExtraViewports()
	{
		if ( isValid() == false )
			return;

		for ( unique_ptr<Impl::ExtraViewportClone>& pExtra : _pImpl->_listExtraViewport )
		{
			if ( pExtra == nullptr || pExtra->_drawData.Valid == false )
				continue;

			Impl::ExtraViewportClone& extra = *pExtra;

			ImGuiViewport viewport{};
			viewport.ID				   = extra._id;
			viewport.Flags			   = extra._flags;
			viewport.Pos			   = extra._pos;
			viewport.Size			   = extra._size;
			viewport.FramebufferScale  = extra._framebufferScale;
			viewport.DrawData		   = &extra._drawData;
			viewport.RendererUserData  = extra._pRendererUserData;
			viewport.PlatformUserData  = extra._pPlatformUserData;
			viewport.PlatformHandle	   = extra._pPlatformHandle;
			viewport.PlatformHandleRaw = extra._pPlatformHandleRaw;

			if ( _pImpl->_pPlatformRenderWindow != nullptr )
				_pImpl->_pPlatformRenderWindow( &viewport, nullptr );
			if ( _pImpl->_pRendererRenderWindow != nullptr )
				_pImpl->_pRendererRenderWindow( &viewport, nullptr );
			if ( _pImpl->_pPlatformSwapBuffers != nullptr )
				_pImpl->_pPlatformSwapBuffers( &viewport, nullptr );

			viewport.RendererUserData  = nullptr;
			viewport.PlatformUserData  = nullptr;
			viewport.PlatformHandle	   = nullptr;
			viewport.PlatformHandleRaw = nullptr;
		}
	}
} // namespace sw::editor
