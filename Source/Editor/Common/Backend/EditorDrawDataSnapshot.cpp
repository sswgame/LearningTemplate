#include "pch.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"

#include "Core/Container/vector.h"

#include <cstring>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorDrawDataSnapshotInternal
        {
            /** @brief @p src 의 원소를 @p outDst 에 옮겨 담습니다. 담을 자리가 있으면 버퍼를 다시 잡지 않는다(`ImVector::operator=` 는 먼저 해제한다). */
            template <typename T>
            static void copyVector( const ImVector<T>& src, ImVector<T>& outDst )
            {
                outDst.resize( src.Size );
                if ( src.Size > 0 )
                    std::memcpy( outDst.Data, src.Data, static_cast<size_t>( src.Size ) * sizeof( T ) );
            }

            /**
             * @brief 그리기 목록을 스냅샷에 복사합니다. 목록 객체와 버퍼는 프레임마다 다시 쓴다(풀 — `outListOwned`).
             * @details `ImDrawList::CloneOutput` 으로 매 프레임 새로 만들면 목록 수 × 버퍼 넷을 프레임마다 잡고 놓는다 — 에디터 할당의 대부분이었다
             *          (프레임당 ~260 KB, 6 분에 20 GB). 버퍼 크기는 거의 그대로라 한 번 자란 뒤로는 할당이 없다.
             */
            static void cloneDrawData( const ImDrawData* pSrc, ImDrawData& outDrawData, vector<ImDrawList*>& outListOwned )
            {
                outDrawData.Clear();

                if ( pSrc == nullptr || pSrc->Valid == false )
                    return;

                outDrawData.Valid            = true;
                outDrawData.DisplayPos       = pSrc->DisplayPos;
                outDrawData.DisplaySize      = pSrc->DisplaySize;
                outDrawData.FramebufferScale = pSrc->FramebufferScale;
                // Textures 는 살아 있는 컨텍스트의 프레임별 리스트(&GetPlatformIO().Textures)를 가리킨다.
                // 스냅샷을 렌더 스레드로 넘기면 다음 프레임의 resize 와 레이스가 나므로 공유하지 않는다.
                // 텍스처 갱신은 UI 스레드의 IImGuiRendererBackend::processTextureUpdates() 가 이미 끝냈다.
                outDrawData.Textures = nullptr;
                // ImGui 1.92 백엔드(imgui_impl_dx12 등)는 RenderDrawData 안에서
                // OwnerViewport->RendererUserData(뷰포트별 프레임 버퍼)를 참조한다. 메인 뷰포트는
                // 컨텍스트 수명 동안 유지되는 영속 객체이므로 포인터를 그대로 넘겨도 안전하다.
                outDrawData.OwnerViewport = pSrc->OwnerViewport;
                outDrawData.TotalIdxCount = pSrc->TotalIdxCount;
                outDrawData.TotalVtxCount = pSrc->TotalVtxCount;

                size_t usedCount = 0;
                for ( int32 listIndex = 0; listIndex < pSrc->CmdLists.Size; ++listIndex )
                {
                    ImDrawList* pSrcList = pSrc->CmdLists[listIndex];
                    if ( pSrcList == nullptr )
                        continue;
                    if ( usedCount == outListOwned.size() )
                        outListOwned.push_back( IM_NEW( ImDrawList )( pSrcList->_Data ) );
                    ImDrawList* pClone = outListOwned[usedCount++];
                    pClone->_Data      = pSrcList->_Data;
                    pClone->Flags      = pSrcList->Flags;
                    copyVector( pSrcList->CmdBuffer, pClone->CmdBuffer );
                    copyVector( pSrcList->IdxBuffer, pClone->IdxBuffer );
                    copyVector( pSrcList->VtxBuffer, pClone->VtxBuffer );

                    // ImGui 1.92+ 의 ImDrawData::AddDrawList() 는 PrimReserve 와 실제 쓰기가 맞는지 assert 한다.
                    // CloneOutput() 은 버퍼만 복사하고 내부 쓰기 커서는 맞추지 않으므로(초기값 NULL), "다 쓴 상태" 로 직접 맞춰 준다.
                    // (렌더러는 CmdBuffer 와 버퍼만 읽으므로 이것으로 충분하다)
                    pClone->_VtxWritePtr   = pClone->VtxBuffer.Data + pClone->VtxBuffer.Size;
                    pClone->_IdxWritePtr   = pClone->IdxBuffer.Data + pClone->IdxBuffer.Size;
                    pClone->_VtxCurrentIdx = static_cast<uint32>( pClone->VtxBuffer.Size );

                    outDrawData.AddDrawList( pClone );
                }

                outDrawData.TotalIdxCount = pSrc->TotalIdxCount;
                outDrawData.TotalVtxCount = pSrc->TotalVtxCount;
            }

            static void destroyOwnedLists( ImDrawData& drawData, vector<ImDrawList*>& listOwned )
            {
                drawData.Clear();
                for ( ImDrawList* pOwned : listOwned )
                {
                    IM_DELETE( pOwned );
                }
                listOwned.clear();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    struct EditorDrawDataSnapshot::Impl
    {
        ImDrawData             _mainDrawData;
        vector<ImDrawList*>    _listMainOwned;
        uint8                  _bValid   : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };

    EditorDrawDataSnapshot::EditorDrawDataSnapshot()
        : _pImpl{ make_unique<Impl>() }
        , _sequence{ 0 }
    {
        _pImpl->_bValid   = SW_FALSE;
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
        _sequence       = 0;
    }

    void EditorDrawDataSnapshot::capture( uint64 sequence )
    {
        // 목록 풀은 두고 다시 쓴다(`cloneDrawData`). 컨텍스트가 없으면 풀까지 놓는다.
        _pImpl->_mainDrawData.Clear();
        _pImpl->_bValid = SW_FALSE;
        _sequence       = 0;
        if ( ImGui::GetCurrentContext() == nullptr )
        {
            clear();
            return;
        }

        _sequence = sequence;
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
