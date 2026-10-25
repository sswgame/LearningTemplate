#include "pch.h"

#include "Editor/Viewport/EditorDebugHUDView.h"

#include "Core/Container/formatString.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/UI/Debug/DebugHUD.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorDebugHUDViewInternal
        {
            static constexpr Color kBackground{ Color::fromBytes( 15, 17, 22, 210 ) };
            static constexpr Color kBorder{ Color::fromBytes( 50, 60, 80, 180 ) };
            static constexpr Color kViewText{ Color::fromBytes( 140, 160, 190, 220 ) };
            static constexpr Color kSectionTitle{ Color::fromBytes( 80, 230, 120, 240 ) };
            static constexpr Color kLineLabel{ Color::fromBytes( 160, 200, 255, 230 ) };
            static constexpr Color kLineValue{ Color::fromBytes( 230, 232, 240, 240 ) };
            static constexpr Color kGraphBar{ Color::fromBytes( 80, 200, 120, 200 ) };
            static constexpr Color kNote{ Color::fromBytes( 150, 150, 160, 200 ) };

            /** @brief 섹션 본문을 다시 부르는 간격(초) — 게임 HUD 와 같은 4 Hz. */
            static constexpr float32 kRefreshSeconds = 0.25f;
            /** @brief 상자 여백 · 열 간격 · 바깥 여백(픽셀, DPI 전)입니다. */
            static constexpr float32 kPadding   = 8.0f;
            static constexpr float32 kColumnGap = 12.0f;
            static constexpr float32 kMargin    = 12.0f;
            /** @brief 그래프 높이(픽셀)입니다. */
            static constexpr float32 kGraphHeight = 24.0f;
        };
    } // namespace

    EditorDebugHUDView::EditorDebugHUDView()
        : _listSlot{}
        , _registryRevision{ UINT64_MAX }
        , _refreshTimer{ 0.0f }
        , _drawnSectionCount{ 0 }
        , _skippedProfilerSectionCount{ 0 }
    {
    }

    EditorDebugHUDView::~EditorDebugHUDView() = default;

    void EditorDebugHUDView::rebuildSlots()
    {
        // 기록(프레임 시간)은 섹션마다 따로 들므로, 남는 섹션의 쓰개는 그대로 옮긴다.
        vector<SectionSlot> listOld;
        listOld.swap( _listSlot );
        for ( const DebugHUDSectionRegistration* pSection : DebugHUDRegistry::get().getSections() )
        {
            SectionSlot slot{ pSection, nullptr };
            for ( SectionSlot& old : listOld )
            {
                if ( old._pRegistration == pSection )
                    slot._pWriter = std::move( old._pWriter );
            }
            if ( slot._pWriter == nullptr )
                slot._pWriter = make_unique<DebugHUDSectionWriter>();
            _listSlot.push_back( std::move( slot ) );
        }
        _registryRevision = DebugHUDRegistry::get().getRevision();
    }

    void EditorDebugHUDView::refresh()
    {
        const FrameProfiler* pProfiler = editor::findSubsystem<FrameProfiler>(); // 필수 서비스: 없으면 버그
        if ( pProfiler == nullptr )
            SW_LOG_ERROR_ONCE( "[DebugHUDView] FrameProfiler is missing - the profiler sections are skipped" );
        const bool bProfilerOn       = pProfiler != nullptr && pProfiler->isEnabled();
        _skippedProfilerSectionCount = 0;
        for ( SectionSlot& slot : _listSlot )
        {
            slot._pWriter->reset();
            if ( DebugHUD::isSectionShown( *slot._pRegistration ) == false )
                continue;
            if ( ( slot._pRegistration->_flags & DebugHUDSectionFlag::kUsesFrameProfiler ) != 0 && bProfilerOn == false )
            {
                ++_skippedProfilerSectionCount;
                continue;
            }
            slot._pRegistration->_pFunc( *slot._pWriter );
        }
    }

    float32 EditorDebugHUDView::draw( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float2& topRightInset, float32 deltaSeconds )
    {
        _drawnSectionCount = 0;
        if ( pDrawList == nullptr )
            return 0.0f;
        if ( _registryRevision != DebugHUDRegistry::get().getRevision() )
        {
            rebuildSlots();
            _refreshTimer = 0.0f;
        }
        for ( SectionSlot& slot : _listSlot )
        {
            slot._pWriter->recordFrameSeconds( deltaSeconds );
        }
        _refreshTimer -= deltaSeconds;
        if ( _refreshTimer <= 0.0f )
        {
            refresh();
            _refreshTimer = EditorDebugHUDViewInternal::kRefreshSeconds;
        }

        // 크기를 먼저 측정한다 — 제목 줄 · 이름 열 · 값 열 · 그래프.
        const float32                        dpiScale   = EditorThemeUtil::getDpiScale();
        const float32                        lineHeight = ImGui::GetTextLineHeight();
        const float32                        padding    = EditorDebugHUDViewInternal::kPadding * dpiScale;
        fixed_string<constant::kMaxBuffer64> viewLine;
        formatstring( viewLine.data(), viewLine.capacity(), "%# x %#", Fmt( static_cast<float64>( canvasSize._x ), Format().precision( 0 ) ),
                      Fmt( static_cast<float64>( canvasSize._y ), Format().precision( 0 ) ) );
        float32 labelWidth = ImGui::CalcTextSize( "view" ).x;
        float32 valueWidth = ImGui::CalcTextSize( viewLine.c_str() ).x;
        float32 height     = lineHeight;
        for ( const SectionSlot& slot : _listSlot )
        {
            const DebugHUDSectionWriter& writer = *slot._pWriter;
            if ( writer.getLineCount() == 0 && writer.hasGraph() == false )
                continue;
            ++_drawnSectionCount;
            valueWidth = MathUtil::max( valueWidth, ImGui::CalcTextSize( slot._pRegistration->_pTitle ).x - labelWidth );
            height += lineHeight;
            for ( uint32 index = 0; index < writer.getLineCount(); ++index )
            {
                labelWidth = MathUtil::max( labelWidth, ImGui::CalcTextSize( writer.getLineAt( index )._label.c_str() ).x );
                valueWidth = MathUtil::max( valueWidth, ImGui::CalcTextSize( writer.getLineAt( index )._value.c_str() ).x );
                height += lineHeight;
            }
            height += writer.hasGraph() ? EditorDebugHUDViewInternal::kGraphHeight * dpiScale : 0.0f;
        }
        if ( _skippedProfilerSectionCount > 0 )
            height += lineHeight;
        const float32 width = padding * 2.0f + labelWidth + EditorDebugHUDViewInternal::kColumnGap * dpiScale + valueWidth;
        const float32 x0    = canvasPos._x + canvasSize._x - topRightInset._x - width - EditorDebugHUDViewInternal::kMargin * dpiScale;
        const float32 y0    = canvasPos._y + topRightInset._y + EditorDebugHUDViewInternal::kMargin * dpiScale;
        const float32 boxH  = height + padding * 2.0f;
        pDrawList->AddRectFilled( ImVec2{ x0, y0 }, ImVec2{ x0 + width, y0 + boxH }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kBackground ), 6.0f );
        pDrawList->AddRect( ImVec2{ x0, y0 }, ImVec2{ x0 + width, y0 + boxH }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kBorder ), 6.0f );

        const float32 valueX = x0 + padding + labelWidth + EditorDebugHUDViewInternal::kColumnGap * dpiScale;
        float32       y      = y0 + padding;
        pDrawList->AddText( ImVec2{ x0 + padding, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kViewText ), "view" );
        pDrawList->AddText( ImVec2{ valueX, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kViewText ), viewLine.c_str() );
        y += lineHeight;
        for ( const SectionSlot& slot : _listSlot )
        {
            const DebugHUDSectionWriter& writer = *slot._pWriter;
            if ( writer.getLineCount() == 0 && writer.hasGraph() == false )
                continue;
            pDrawList->AddText( ImVec2{ x0 + padding, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kSectionTitle ), slot._pRegistration->_pTitle );
            y += lineHeight;
            for ( uint32 index = 0; index < writer.getLineCount(); ++index )
            {
                const DebugHUDLine& line = writer.getLineAt( index );
                pDrawList->AddText( ImVec2{ x0 + padding, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kLineLabel ), line._label.c_str() );
                pDrawList->AddText( ImVec2{ valueX, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kLineValue ), line._value.c_str() );
                y += lineHeight;
            }
            if ( writer.hasGraph() )
            {
                // 막대 그래프 — 값이 끝(getGraphMax)을 넘으면 꽉 찬 막대다.
                const vector<float32>& listValue = writer.getGraph();
                const float32          graphH    = EditorDebugHUDViewInternal::kGraphHeight * dpiScale;
                const float32          barWidth  = ( width - padding * 2.0f ) / static_cast<float32>( MathUtil::max( listValue.size(), size_t( 1 ) ) );
                const float32          maxValue  = MathUtil::max( writer.getGraphMax(), 0.001f );
                for ( size_t index = 0; index < listValue.size(); ++index )
                {
                    const float32 ratio = MathUtil::clamp( listValue[index] / maxValue, 0.0f, 1.0f );
                    const float32 bx    = x0 + padding + barWidth * static_cast<float32>( index );
                    pDrawList->AddRectFilled( ImVec2{ bx, y + graphH * ( 1.0f - ratio ) }, ImVec2{ bx + MathUtil::max( barWidth - 0.5f, 1.0f ), y + graphH },
                                              EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kGraphBar ) );
                }
                y += graphH;
            }
        }
        if ( _skippedProfilerSectionCount > 0 )
        {
            fixed_string<constant::kMaxBuffer64> note;
            formatstring( note.data(), note.capacity(), "%# section(s) need the profiler", _skippedProfilerSectionCount );
            pDrawList->AddText( ImVec2{ x0 + padding, y }, EditorColorUtil::toImU32( EditorDebugHUDViewInternal::kNote ), note.c_str() );
        }
        return topRightInset._y + boxH + EditorDebugHUDViewInternal::kMargin * dpiScale;
    }
} // namespace sw::editor
