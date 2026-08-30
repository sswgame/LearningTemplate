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
			static void cloneDrawData( const ImDrawData* pSrc, ImDrawData& outDrawData, vector<ImDrawList*>& outListOwned )
			{
				outDrawData.Clear();
				for ( ImDrawList* pOwned : outListOwned )
					IM_DELETE( pOwned );
				outListOwned.clear();

				if ( pSrc == nullptr || pSrc->Valid == false )
					return;

				outDrawData.Valid			 = true;
				outDrawData.DisplayPos		 = pSrc->DisplayPos;
				outDrawData.DisplaySize		 = pSrc->DisplaySize;
				outDrawData.FramebufferScale = pSrc->FramebufferScale;
				// Textures 는 라이브 컨텍스트의 per-frame 리스트(&GetPlatformIO().Textures)를 가리킨다.
				// 스냅샷을 렌더 스레드로 넘기면 다음 프레임의 resize 와 레이스가 나므로 공유하지 않는다.
				// 텍스처 갱신은 UI 스레드의 IImGuiRendererBackend::processTextureUpdates() 가 이미 끝냈다.
				outDrawData.Textures = nullptr;
				// ImGui 1.92 백엔드(imgui_impl_dx12 등)는 RenderDrawData 안에서
				// OwnerViewport->RendererUserData(뷰포트별 프레임 버퍼)를 참조한다. 메인 뷰포트는
				// 컨텍스트 수명 동안 유지되는 영속 객체이므로 포인터를 그대로 넘겨도 안전하다.
				outDrawData.OwnerViewport = pSrc->OwnerViewport;
				outDrawData.TotalIdxCount = pSrc->TotalIdxCount;
				outDrawData.TotalVtxCount = pSrc->TotalVtxCount;

				outListOwned.reserve( static_cast<size_t>( pSrc->CmdLists.Size ) );
				for ( int32 listIndex = 0; listIndex < pSrc->CmdLists.Size; ++listIndex )
				{
					ImDrawList* pSrcList = pSrc->CmdLists[listIndex];
					if ( pSrcList == nullptr )
						continue;
					ImDrawList* pClone = pSrcList->CloneOutput();
					if ( pClone == nullptr )
						continue;

					// ImGui 1.92+ 의 ImDrawData::AddDrawList() 는 PrimReserve↔write 정합성을 assert 한다.
					// CloneOutput() 은 버퍼만 복사하고 내부 write 커서를 안 맞추므로(초기값 NULL),
					// "다 쓴 상태" 로 직접 고정해 준다. (렌더러는 CmdBuffer/버퍼만 읽으므로 이걸로 충분)
					pClone->_VtxWritePtr   = pClone->VtxBuffer.Data + pClone->VtxBuffer.Size;
					pClone->_IdxWritePtr   = pClone->IdxBuffer.Data + pClone->IdxBuffer.Size;
					pClone->_VtxCurrentIdx = static_cast<uint32>( pClone->VtxBuffer.Size );

					outListOwned.push_back( pClone );
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
		ImDrawData			   _mainDrawData;
		vector<ImDrawList*>	   _listMainOwned;
		uint8				   _bValid	 : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};

	EditorDrawDataSnapshot::EditorDrawDataSnapshot()
		: _pImpl{ make_unique<Impl>() }
	{
		_pImpl->_bValid	  = SW_FALSE;
		_pImpl->_reserved = 0;
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
		_pImpl->_bValid = SW_FALSE;
	}

	void EditorDrawDataSnapshot::capture()
	{
		clear();
		if ( ImGui::GetCurrentContext() == nullptr )
			return;

		EditorDrawDataSnapshotInternal::cloneDrawData( ImGui::GetDrawData(), _pImpl->_mainDrawData, _pImpl->_listMainOwned );
		_pImpl->_bValid = ( _pImpl->_mainDrawData.Valid ) ? SW_TRUE : SW_FALSE;
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
} // namespace sw::editor
