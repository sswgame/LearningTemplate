#include "pch.h"

#include "Editor/Panels/RenderTarget/RenderTargetPanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RenderTargetPanel, RenderTargetPanel::kPanelID, EditorPanelCategory::Tool, 900 );

    namespace
    {
        /// @brief 목록 열의 너비입니다. 이름이 길어도(GBufferAlbedo) 잘리지 않을 만큼 잡습니다.
        constexpr float32 kListColumns = 29.0f; ///< 목록 폭(글자 칸 수)

        /** @brief 고른 타깃을 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const RenderTargetInfo* findByName( const vector<RenderTargetInfo>& listTarget, const string& name )
        {
            return VectorUtil::findBy<&RenderTargetInfo::_name>( listTarget, name );
        }
    } // namespace

    RenderTargetPanel::RenderTargetPanel()
        : IEditorPanel( false )
        , _viewModel{}
        , _viewState{}
        , _probe{}
        , _searchFilter{}
        , _previewZoom{ 0.0f } // 0 = 창에 맞춤
    {
    }

    void RenderTargetPanel::shutdown()
    {
        _viewModel.shutdown();
    }

    bool RenderTargetPanel::isSelectedDepth() const
    {
        const RenderTargetInfo* pSelected = findByName( _viewState._listTarget, _viewState._selectedName );
        return pSelected != nullptr && pSelected->_bDepth != SW_FALSE;
    }

    uint32 RenderTargetPanel::getSelectedPassUseCount() const
    {
        const RenderTargetInfo* pSelected = findByName( _viewState._listTarget, _viewState._selectedName );
        return pSelected != nullptr ? static_cast<uint32>( pSelected->_listProducer.size() + pSelected->_listConsumer.size() ) : 0u;
    }

    void RenderTargetPanel::drawToolbar()
    {
        EditorWidgets::drawSearchField( "##rtsearch", _searchFilter, SW_LOCKEY( "EditorRenderTarget", "SearchHint" ), 260.0f );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Targets", TextArgumentList().addInteger( "targetCount", static_cast<int64>( _viewState._listTarget.size() ) ) ).c_str() );

        ImGui::SameLine();
        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "Zoom" ) );
        EditorChrome::setNextItemWidthColumns( 21.0f );
        // 0 = 창에 맞춤. 원본 해상도로 보고 싶을 때만 배율을 준다(1:1 픽셀 검사).
        ImGui::SliderFloat( "##Zoom", &_previewZoom, 0.0f, 2.0f, _previewZoom <= 0.0f ? "fit" : "%.2fx" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "ZoomTip" ) );

        // 채널 · 깊이 · 픽셀 값은 CPU 로 읽은 스냅샷이다. 다시 읽기는 GPU 를 기다리므로 누를 때(또는 Auto 간격)만 한다.
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
            _viewModel.requestRefresh();
        EditorSelfTestMarks::record( "renderTargets.refresh" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "RefreshTip" ) );
        bool bAuto = _viewState._bAutoRefresh != SW_FALSE;
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Auto" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Auto" ), &bAuto ) )
            _viewModel.setAutoRefresh( bAuto );
        EditorSelfTestMarks::record( "renderTargets.auto" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "AutoRefreshTip" ) );
    }

    void RenderTargetPanel::drawTargetList()
    {
        const EditorListFilter filter{ _searchFilter.c_str() };

        ImGui::BeginChild( "##rtlist", EditorChrome::measureTextExtent( EditorTextExtent::ofText( kListColumns, 0.0f ) ), ImGuiChildFlags_Borders );
        uint32 shownCount = 0;
        for ( const RenderTargetInfo& info : _viewState._listTarget )
        {
            // 포맷 이름으로도 찾을 수 있어야 한다. "이 파이프라인의 HDR 타깃이 뭐지" 가 실제로 나오는 질문이다.
            const string_view formatName{ FrameRendererUtil::attachmentFormatName( info._format ) };
            if ( filter.matchesAny( { string_view{ info._name }, formatName } ) == false )
                continue;

            ++shownCount;
            const bool bSelected = ( info._name == _viewState._selectedName );
            if ( ImGui::Selectable( info._name.c_str(), bSelected ) )
                _viewModel.selectTarget( info._name );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( "renderTargets.row." + info._name ).c_str() );

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

        if ( _viewState._listTarget.empty() )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "NoTargets" ) );
        else if ( shownCount == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "NoFilterMatch" ) );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawViewControls( const RenderTargetInfo& selected )
    {
        if ( selected._bDepth != SW_FALSE )
        {
            // 깊이는 한 채널이다. 원근 깊이는 먼 쪽에 몰려 있어 그대로 보면 거의 흰색이라, 거리로 되돌리고 화면의 범위로 늘린다.
            const RenderTargetViewSettings& settings   = _viewState._viewSettings;
            bool                            bLinearize = settings._bLinearizeDepth != SW_FALSE;
            bool                            bAutoRange = settings._bAutoDepthRange != SW_FALSE;
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Linearize" ), &bLinearize ) )
                _viewModel.setLinearizeDepth( bLinearize );
            EditorSelfTestMarks::record( "renderTargets.linearize" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "LinearizeTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "AutoRange" ) ) );
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "AutoRange" ), &bAutoRange ) )
                _viewModel.setAutoDepthRange( bAutoRange );
            EditorSelfTestMarks::record( "renderTargets.autoRange" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "AutoRangeTip" ) );
            if ( bLinearize )
            {
                ImGui::SameLine();
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "NearFar" ) );
                EditorChrome::setNextItemWidthColumns( 24.0f );
                float32 arrPlane[2]{ settings._nearPlane, settings._farPlane };
                if ( ImGui::DragFloat2( "##NearFar", arrPlane, 0.1f, 0.001f, 100000.0f, "%.3f" ) )
                    _viewModel.setDepthPlanes( arrPlane[0], arrPlane[1] );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "NearFarTip" ) );
            }
            if ( hasSnapshot() )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Range", TextArgumentList().addDecimal( "snapshotDepthRangeX", static_cast<float64>( _viewState._snapshotDepthRange._x ), 3 ).addDecimal( "snapshotDepthRangeY", static_cast<float64>( _viewState._snapshotDepthRange._y ), 3 ) ).c_str() );
            }
            return;
        }

        for ( uint32 channelIndex = 0; channelIndex < static_cast<uint32>( RenderTargetChannel::Count ); ++channelIndex )
        {
            const RenderTargetChannel channel = static_cast<RenderTargetChannel>( channelIndex );
            if ( channelIndex > 0 )
                EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( RenderTargetViewSettings::getChannelLabel( channel ) ) );
            if ( ImGui::RadioButton( RenderTargetViewSettings::getChannelLabel( channel ), _viewState._viewSettings._channel == channel ) )
                _viewModel.setChannel( channel );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderTarget", "ChannelTip" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string{ "renderTargets.channel." } + RenderTargetViewSettings::getChannelLabel( channel ) ).c_str() );
        }
    }

    void RenderTargetPanel::probeHoveredPixel( const float2& imageMin, const float2& imageSize )
    {
        if ( ImGui::IsItemHovered() == false || hasSnapshot() == false )
            return;
        const ImVec2 mouse = ImGui::GetMousePos();
        if ( _viewModel.probePixel( float2{ mouse.x - imageMin._x, mouse.y - imageMin._y }, imageSize, _probe ) == false )
            return;

        ImGui::BeginTooltip();
        ImGui::Text( "(%d, %d)", _probe._texel._x, _probe._texel._y );
        if ( _probe._bDepth != SW_FALSE )
        {
            ImGui::Text( "%s %.6f", SW_LOCKEY( "EditorRenderTarget", "DepthLabel" ), static_cast<float64>( _probe._value._x ) );
            ImGui::Text( "%s %.3f", SW_LOCKEY( "EditorRenderTarget", "LinearLabel" ), static_cast<float64>( _probe._linearDepth ) );
        }
        else
        {
            // G버퍼 첨부면 채널을 명세의 뜻으로 이름 짓고 푼 값을 잇는다(ViewModel 이 셰이더 읽기와 같은 식으로 풀었다).
            const float32 arrValue[4]{ _probe._value._x, _probe._value._y, _probe._value._z, _probe._value._w };
            const utf8*   arrLabel[4]{ "R", "G", "B", "A" };
            for ( uint32 channelIndex = 0; channelIndex < _probe._channelCount && channelIndex < 4; ++channelIndex )
            {
                const utf8* pMeaning = _probe._arrMeaning[channelIndex];
                ImGui::Text( "%s %.4f%s%s", arrLabel[channelIndex], static_cast<float64>( arrValue[channelIndex] ), pMeaning != nullptr ? "  " : "",
                             pMeaning != nullptr ? pMeaning : "" );
            }
            if ( _probe._bGBuffer != SW_FALSE )
            {
                ImGui::Separator();
                for ( const GBufferDecodedValue& decoded : _probe._listDecodedValue )
                {
                    if ( decoded._componentCount == 3 )
                        ImGui::Text( "%s (%.3f, %.3f, %.3f)", decoded._pName, static_cast<float64>( decoded._value._x ), static_cast<float64>( decoded._value._y ),
                                     static_cast<float64>( decoded._value._z ) );
                    else
                        ImGui::Text( "%s %.3f", decoded._pName, static_cast<float64>( decoded._value._x ) );
                }
                ImGui::TextDisabled( "G-buffer layout v%u", GBufferLayout::kVersion );
            }
        }
        ImGui::EndTooltip();
    }

    void RenderTargetPanel::drawPreview()
    {
        ImGui::BeginChild( "##rtpreview", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );

        const RenderTargetInfo* pSelected = findByName( _viewState._listTarget, _viewState._selectedName );
        if ( pSelected == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "PickTargetHint" ) );
            ImGui::EndChild();
            return;
        }

        ImGui::TextUnformatted( pSelected->_name.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%ux%u  %s", pSelected->_width, pSelected->_height,
                             FrameRendererUtil::attachmentFormatName( pSelected->_format ) );
        // 생산자 · 소비자 · 수명(파이프라인 순서의 패스 번호) — 이 그림을 누가 쓰고 누가 읽는지로 그래픽 문제를 거슬러 찾는다.
        const auto joinPasses = []( const vector<string>& listPass ) -> string
        {
            string joined;
            for ( const string& pass : listPass )
            {
                joined += ( joined.empty() ? "" : ", " ) + pass;
            }
            return joined.empty() ? string( "-" ) : joined;
        };
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderTarget", "Producers", TextArgumentList().addText( "passes", joinPasses( pSelected->_listProducer ) ) ).c_str() );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderTarget", "Consumers", TextArgumentList().addText( "passes", joinPasses( pSelected->_listConsumer ) ) ).c_str() );
        if ( pSelected->_firstPass.has_value() && pSelected->_lastPass.has_value() ) // 쓰는 패스가 없는 첨부는 수명 줄이 없다
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderTarget", "Lifetime",
                                                        TextArgumentList().addInteger( "firstPass", static_cast<int64>( *pSelected->_firstPass ) ).addInteger( "lastPass", static_cast<int64>( *pSelected->_lastPass ) ) )
                                           .c_str() );

        const bool bDepth     = pSelected->_bDepth != SW_FALSE;
        const bool bDecodable = RenderTargetReadback::isDecodableFormat( pSelected->_format, bDepth );
        if ( bDecodable )
            drawViewControls( *pSelected );

        // 마지막으로 읽은 픽셀 값 줄. 미리보기 밖으로 나가도 남아 값을 옮겨 적을 수 있다.
        const int2&   probedTexel = _viewState._probedTexel;
        const float4& probedPixel = _viewState._probedPixel;
        if ( _viewState._bProbedPixel != SW_FALSE )
        {
            if ( bDepth )
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "PixelDepth", TextArgumentList().addInteger( "probedTexelX", static_cast<int64>( probedTexel._x ) ).addInteger( "probedTexelY", static_cast<int64>( probedTexel._y ) ).addDecimal( "probedPixelX", static_cast<float64>( probedPixel._x ), 6 ) ).c_str() );
            }
            else
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Pixel", TextArgumentList().addInteger( "probedTexelX", static_cast<int64>( probedTexel._x ) ).addInteger( "probedTexelY", static_cast<int64>( probedTexel._y ) ).addDecimal( "probedPixelX", static_cast<float64>( probedPixel._x ), 4 ).addDecimal( "probedPixelY", static_cast<float64>( probedPixel._y ), 4 ).addDecimal( "probedPixelZ", static_cast<float64>( probedPixel._z ), 4 ).addDecimal( "probedPixelW", static_cast<float64>( probedPixel._w ), 4 ) ).c_str() );
            }
        }

        // 색 그대로(RGBA)는 GPU 텍스처를 그대로 보인다(매 프레임 최신). 깊이 · 채널 하나는 CPU 스냅샷으로 만든 이미지다.
        const bool bLive      = ( bDepth == false ) && ( _viewState._viewSettings._channel == RenderTargetChannel::RGBA );
        void*      pTextureID = bLive ? _viewState._pLiveTextureID : _viewState._pSnapshotTextureID;
        if ( pTextureID == nullptr )
        {
            if ( bLive )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "RegisterFailed" ) );
            else if ( bDecodable == false )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "FormatNotReadable" ) );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderTarget", "ReadingTarget" ) );
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
            // 창에 맞춘다. 가로 · 세로 중 먼저 걸리는 쪽에 맞춰야 비율이 유지된다.
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

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ drawWidth, drawHeight } );
        EditorSelfTestMarks::record( "renderTargets.preview" );
        probeHoveredPixel( float2{ imageMin.x, imageMin.y }, float2{ drawWidth, drawHeight } );
        ImGui::EndChild();
    }

    void RenderTargetPanel::drawContent()
    {
        _viewModel.gather( _viewState );

        drawToolbar();
        ImGui::Separator();
        drawTargetList();
        ImGui::SameLine();
        drawPreview();
    }
} // namespace sw::editor
