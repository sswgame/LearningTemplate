#include "pch.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Backend/EditorUICapture.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

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
             * @brief @p src 의 그리기 목록을 복사해 @p outDrawData 뒤에 붙입니다. 목록 객체와 버퍼는 프레임마다 다시 쓴다(풀 `outListOwned`, 앞 `inoutUsedCount` 개는 이미 쓰였다).
             * @details `ImDrawList::CloneOutput` 으로 매 프레임 새로 만들면 목록 수 × 버퍼 넷을 프레임마다 잡고 놓아 에디터 할당의 대부분을 차지한다
             *          (프레임당 ~260 KB, 6 분에 20 GB). 버퍼 크기는 거의 그대로라 한 번 자란 뒤로는 할당이 없다.
             */
            static void appendClonedLists( const ImDrawData& src, ImDrawData& outDrawData, vector<ImDrawList*>& outListOwned, size_t& inoutUsedCount )
            {
                for ( int32 listIndex = 0; listIndex < src.CmdLists.Size; ++listIndex )
                {
                    ImDrawList* pSrcList = src.CmdLists[listIndex];
                    if ( pSrcList == nullptr )
                        continue;
                    if ( inoutUsedCount == outListOwned.size() )
                        outListOwned.push_back( IM_NEW( ImDrawList )( pSrcList->_Data ) );
                    ImDrawList* pClone = outListOwned[inoutUsedCount++];
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
            }

            /** @brief 그리기 데이터를 스냅샷에 복사합니다. 목록 풀은 `appendClonedLists` 를 봅니다. */
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

                size_t usedCount = 0;
                appendClonedLists( *pSrc, outDrawData, outListOwned, usedCount );

                outDrawData.TotalIdxCount = pSrc->TotalIdxCount;
                outDrawData.TotalVtxCount = pSrc->TotalVtxCount;
            }

            /** @brief 플로팅 뷰포트를 마지막으로 앞에 온 순서로 놓습니다(뒤에 놓인 것이 위에 그려진다). */
            static bool isFocusedEarlier( const ImGuiViewportP* pLeft, const ImGuiViewportP* pRight )
            {
                return pLeft->LastFocusedStampCount < pRight->LastFocusedStampCount;
            }

            /**
             * @brief 모든 뷰포트(메인 + 플로팅)의 그리기 목록을 한 그리기 데이터로 합칩니다. 표시 영역은 모든 뷰포트를 감싸는 화면 사각형입니다.
             * @details 멀티 뷰포트에서는 모든 목록의 정점이 OS 화면 좌표라 목록을 그대로 이어 붙이고 `DisplayPos` 만 감싸는 사각형의 왼쪽 위로 두면 된다.
             *          주인 뷰포트는 메인이다. 렌더러 백엔드는 주인의 정점 버퍼를 쓰는데, 플로팅 뷰포트의 버퍼는 UI 스레드가 그 창을 그릴 때 쓰고 있다.
             */
            static void composeAllViewports( ImDrawData& outDrawData, vector<ImDrawList*>& outListOwned )
            {
                outDrawData.Clear();
                const ImGuiPlatformIO& platformIO      = ImGui::GetPlatformIO();
                ImGuiViewportP* const  pMain           = static_cast<ImGuiViewportP*>( ImGui::GetMainViewport() );
                const bool             bNoMainDrawData = pMain == nullptr || pMain->DrawData == nullptr || pMain->DrawData->Valid == false;
                if ( bNoMainDrawData )
                    return;

                vector<ImGuiViewportP*> listViewport;
                listViewport.push_back( pMain );
                if ( ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) != 0 )
                {
                    for ( int32 viewportIndex = 0; viewportIndex < platformIO.Viewports.Size; ++viewportIndex )
                    {
                        ImGuiViewportP* const pViewport    = static_cast<ImGuiViewportP*>( platformIO.Viewports[viewportIndex] );
                        const bool            bNotFloating = pViewport == nullptr || pViewport == pMain || pViewport->DrawData == nullptr;
                        if ( bNotFloating )
                            continue;
                        const bool bMinimized = ( pViewport->Flags & ImGuiViewportFlags_IsMinimized ) != 0;
                        if ( pViewport->DrawData->Valid && bMinimized == false )
                            listViewport.push_back( pViewport );
                    }
                }
                std::sort( listViewport.begin() + 1, listViewport.end(), &EditorDrawDataSnapshotInternal::isFocusedEarlier );

                ImVec2 minPos = pMain->DrawData->DisplayPos;
                ImVec2 maxPos{ minPos.x + pMain->DrawData->DisplaySize.x, minPos.y + pMain->DrawData->DisplaySize.y };
                for ( const ImGuiViewportP* pViewport : listViewport )
                {
                    const ImDrawData& drawData = *pViewport->DrawData;
                    const ImVec2      endPos{ drawData.DisplayPos.x + drawData.DisplaySize.x, drawData.DisplayPos.y + drawData.DisplaySize.y };
                    minPos = ImMin( minPos, drawData.DisplayPos );
                    maxPos = ImMax( maxPos, endPos );
                }

                outDrawData.Valid            = true;
                outDrawData.DisplayPos       = minPos;
                outDrawData.DisplaySize      = ImVec2{ maxPos.x - minPos.x, maxPos.y - minPos.y };
                outDrawData.FramebufferScale = pMain->DrawData->FramebufferScale;
                outDrawData.Textures         = nullptr;
                outDrawData.OwnerViewport    = pMain;

                size_t usedCount = 0;
                for ( const ImGuiViewportP* pViewport : listViewport )
                {
                    appendClonedLists( *pViewport->DrawData, outDrawData, outListOwned, usedCount );
                }
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
        ImDrawData             _captureDrawData; ///< 에디터 UI 캡처를 요청한 프레임에만 채우는 모든 뷰포트 합본입니다.
        vector<ImDrawList*>    _listCaptureOwned;
        EditorUICaptureJob     _captureJob;
        uint8                  _bValid   : 1;
        uint8                  _bCapture : 1;
        [[maybe_unused]] uint8 _reserved : 6;
    };

    EditorDrawDataSnapshot::EditorDrawDataSnapshot()
        : _pImpl{ make_unique<Impl>() }
        , _sequence{ 0 }
    {
        _pImpl->_bValid   = SW_FALSE;
        _pImpl->_bCapture = SW_FALSE;
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
        EditorDrawDataSnapshotInternal::destroyOwnedLists( _pImpl->_captureDrawData, _pImpl->_listCaptureOwned );
        _pImpl->_bValid   = SW_FALSE;
        _pImpl->_bCapture = SW_FALSE;
        _sequence         = 0;
    }

    void EditorDrawDataSnapshot::capture( uint64 sequence, const EditorUICaptureJob* pCaptureJob )
    {
        // 목록 풀은 두고 다시 쓴다(`cloneDrawData`). 컨텍스트가 없으면 풀까지 놓는다.
        _pImpl->_mainDrawData.Clear();
        _pImpl->_captureDrawData.Clear();
        _pImpl->_bValid   = SW_FALSE;
        _pImpl->_bCapture = SW_FALSE;
        _sequence         = 0;
        if ( ImGui::GetCurrentContext() == nullptr )
        {
            clear();
            return;
        }

        _sequence = sequence;
        EditorDrawDataSnapshotInternal::cloneDrawData( ImGui::GetDrawData(), _pImpl->_mainDrawData, _pImpl->_listMainOwned );
        _pImpl->_bValid = ( _pImpl->_mainDrawData.Valid ) ? SW_TRUE : SW_FALSE;

        if ( pCaptureJob != nullptr )
        {
            EditorDrawDataSnapshotInternal::composeAllViewports( _pImpl->_captureDrawData, _pImpl->_listCaptureOwned );
            _pImpl->_captureJob = *pCaptureJob;
            _pImpl->_bCapture   = _pImpl->_captureDrawData.Valid ? SW_TRUE : SW_FALSE;
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

    ImDrawData* EditorDrawDataSnapshot::findCaptureDrawData()
    {
        if ( isValid() == false || _pImpl->_bCapture == SW_FALSE )
            return nullptr;
        return &_pImpl->_captureDrawData;
    }

    const EditorUICaptureJob& EditorDrawDataSnapshot::getCaptureJob() const
    {
        return _pImpl->_captureJob;
    }
} // namespace sw::editor
