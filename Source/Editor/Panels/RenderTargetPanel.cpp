#include "pch.h"

#include "Editor/Panels/RenderTargetPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /// @brief 목록 칸의 너비. 이름이 길어도(GBufferAlbedo) 잘리지 않을 만큼.
        constexpr float32 kListWidth = 220.0f;

        /** @brief 고른 타깃을 이름으로 찾습니다. 없으면 nullptr. */
        const RenderTargetInfo* findByName( const vector<RenderTargetInfo>& listTarget, const string& name )
        {
            for ( const RenderTargetInfo& info : listTarget )
            {
                if ( info._name == name )
                    return &info;
            }
            return nullptr;
        }
    } // namespace

    RenderTargetPanel::RenderTargetPanel()
        : IEditorPanel( false )
        , _lastGeneration{ 0 }
        , _pPreviewTextureId{ nullptr }
        , _previewTexture{ 0 }
        , _previewZoom{ 0.0f } // 0 = 창에 맞춤
    {
    }

    void RenderTargetPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
    {
        releasePreviewTexture();
    }

    void RenderTargetPanel::releasePreviewTexture()
    {
        if ( _pPreviewTextureId == nullptr )
            return;

        EditorContext* pContext = EditorContext::get();
        // 컨텍스트가 이미 내려갔으면 놓아 줄 상대가 없다 — 백엔드가 자기 힙을 통째로 버린 뒤다.
        if ( pContext != nullptr && pContext->getRendererBackend() != nullptr )
            pContext->getRendererBackend()->unregisterTexture( _pPreviewTextureId );
        _pPreviewTextureId = nullptr;
        _previewTexture    = 0;
    }

    void RenderTargetPanel::refreshTargetsIfStale()
    {
        // 엔진 서비스는 **에디터 로케이터**로 받는다 — `EngineServices.h` 는 Engine/App/Test 전용이다.
        RenderTargetRegistry* pRegistry = editor::getService<RenderTargetRegistry>();
        if ( pRegistry == nullptr )
        {
            _listTarget.clear();
            return;
        }

        const uint64 generation = pRegistry->getGeneration();
        if ( generation == _lastGeneration && _listTarget.empty() == false )
            return;

        _lastGeneration = generation;
        pRegistry->snapshot( _listTarget );

        // 목록이 새로 만들어졌으면 들고 있던 텍스처 id 는 **죽은 핸들**을 가리킬 수 있다.
        // 창 크기가 바뀌면 트랜지언트가 전부 다시 만들어진다 — 그때 그대로 그리면 백엔드가 죽는다.
        releasePreviewTexture();
    }

    void RenderTargetPanel::syncPreviewTexture()
    {
        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        // 깊이 첨부는 등록하지 않는다 — 아래 미리보기가 왜 못 그리는지 적어 둔다.
        const bool   bPreviewable = ( pSelected != nullptr ) && ( pSelected->_bDepth == SW_FALSE );
        const uint64 texture      = bPreviewable ? pSelected->_texture : 0;
        if ( texture == _previewTexture && _pPreviewTextureId != nullptr )
            return;

        releasePreviewTexture();
        if ( texture == 0 )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || pContext->getRendererBackend() == nullptr )
            return;
        _pPreviewTextureId = pContext->getRendererBackend()->registerTexture( texture );
        _previewTexture    = ( _pPreviewTextureId != nullptr ) ? texture : 0;
    }

    void RenderTargetPanel::drawToolbar()
    {
        EditorWidgets::drawSearchField( "##rtsearch", _searchFilter, "Search render targets...", 260.0f );
        ImGui::SameLine();
        ImGui::TextDisabled( "%d targets", static_cast<int32>( _listTarget.size() ) );

        ImGui::SameLine();
        ImGui::SetNextItemWidth( 160.0f );
        // 0 = 창에 맞춤. 원본 해상도로 보고 싶을 때만 배율을 준다(1:1 픽셀 검사).
        ImGui::SliderFloat( "Zoom", &_previewZoom, 0.0f, 2.0f, _previewZoom <= 0.0f ? "fit" : "%.2fx" );
    }

    void RenderTargetPanel::drawTargetList()
    {
        const EditorListFilter filter{ _searchFilter.c_str() };

        ImGui::BeginChild( "##rtlist", ImVec2{ kListWidth, 0.0f }, ImGuiChildFlags_Borders );
        uint32 shownCount = 0;
        for ( const RenderTargetInfo& info : _listTarget )
        {
            // 포맷 이름으로도 찾을 수 있어야 한다 — "이 파이프라인의 HDR 타깃이 뭐지" 가 실제 질문이다.
            const string_view formatName{ FrameRendererUtil::attachmentFormatName( info._format ) };
            if ( filter.matchesAny( { string_view{ info._name }, formatName } ) == false )
                continue;

            ++shownCount;
            const bool bSelected = ( info._name == _selectedName );
            if ( ImGui::Selectable( info._name.c_str(), bSelected ) )
                _selectedName = info._name;

            if ( info._bDepth != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(depth)" );
            }
            else if ( info._bPresented != SW_FALSE )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "(screen)" );
            }
        }

        if ( _listTarget.empty() )
            ImGui::TextDisabled( "Renderer has not published any targets yet." );
        else if ( shownCount == 0 )
            ImGui::TextDisabled( "No target matches the filter." );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawPreview()
    {
        ImGui::BeginChild( "##rtpreview", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );

        const RenderTargetInfo* pSelected = findByName( _listTarget, _selectedName );
        if ( pSelected == nullptr )
        {
            ImGui::TextDisabled( "Pick a render target on the left." );
            ImGui::EndChild();
            return;
        }

        ImGui::TextUnformatted( pSelected->_name.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%ux%u  %s", pSelected->_width, pSelected->_height,
                             FrameRendererUtil::attachmentFormatName( pSelected->_format ) );

        if ( pSelected->_bDepth != SW_FALSE )
        {
            // 깊이 첨부는 색으로 볼 수 없다 — 백엔드마다 깊이 SRV 를 ImGui 텍스처로 받아 주는 방식이
            // 다르고, 받아도 D24S8 은 정규화 깊이라 거의 흰 화면이 된다. 있는 척하지 않고 무엇을
            // 못 하는지, 대신 무엇을 쓰면 되는지를 적는다.
            ImGui::Separator();
            ImGui::TextWrapped( "Depth attachments have no preview. Dump it instead: "
                                "-gv_screenshotAttachment=%s",
                                pSelected->_name.c_str() );
            ImGui::EndChild();
            return;
        }

        if ( _pPreviewTextureId == nullptr )
        {
            ImGui::TextDisabled( "Could not register this target as an ImGui texture." );
            ImGui::EndChild();
            return;
        }

        const ImVec2  avail  = ImGui::GetContentRegionAvail();
        const float32 aspect = ( pSelected->_height > 0 )
                                 ? ( static_cast<float32>( pSelected->_width ) / static_cast<float32>( pSelected->_height ) )
                                 : ( 16.0f / 9.0f );

        float32 drawWidth  = 0.0f;
        float32 drawHeight = 0.0f;
        if ( _previewZoom <= 0.0f )
        {
            // 창에 맞춘다 — 가로·세로 중 먼저 걸리는 쪽에 맞춰야 비율이 유지된다.
            drawWidth  = MathUtil::max( avail.x, 1.0f );
            drawHeight = drawWidth / MathUtil::max( aspect, 0.001f );
            if ( drawHeight > avail.y && avail.y > 1.0f )
            {
                drawHeight = avail.y;
                drawWidth  = drawHeight * aspect;
            }
        }
        else
        {
            drawWidth  = static_cast<float32>( pSelected->_width ) * _previewZoom;
            drawHeight = static_cast<float32>( pSelected->_height ) * _previewZoom;
        }

        ImGui::Image( reinterpret_cast<ImTextureID>( _pPreviewTextureId ), ImVec2{ drawWidth, drawHeight } );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawContent()
    {
        refreshTargetsIfStale();

        // 고른 것이 사라졌으면(파이프라인이 바뀌었다) 선택을 놓는다 — 없는 이름을 들고 있으면
        // 미리보기가 영영 빈 채로 남는다.
        if ( _selectedName.empty() == false && findByName( _listTarget, _selectedName ) == nullptr )
            _selectedName.clear();

        // 아무것도 안 골랐으면 **화면에 나가는 것**을 고른다 — 열자마자 지금 보이는 그림이 뜬다.
        // 이름순 첫 번째로 두면 AOColor 가 잡혀서 "이게 뭐지" 부터 시작하게 된다.
        if ( _selectedName.empty() )
        {
            for ( const RenderTargetInfo& info : _listTarget )
            {
                if ( info._bPresented != SW_FALSE )
                {
                    _selectedName = info._name;
                    break;
                }
            }
        }
        if ( _selectedName.empty() )
        {
            for ( const RenderTargetInfo& info : _listTarget )
            {
                if ( info._bDepth == SW_FALSE )
                {
                    _selectedName = info._name;
                    break;
                }
            }
        }

        syncPreviewTexture();

        drawToolbar();
        ImGui::Separator();
        drawTargetList();
        ImGui::SameLine();
        drawPreview();
    }
} // namespace sw::editor
