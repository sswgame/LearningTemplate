/**
 * @file UiTestWidgets.h
 * @brief UI 시험이 쓰는 위젯(스위트 아님) — 리플렉션 없는 상자 · 패널과 고정 배치 도우미.
 * @details 시험 위젯은 `REFLECT` 를 쓰지 않습니다(시험 타깃에 리플렉션 단계가 없다). `getTypeInfo()` 는 가장 가까운 리플렉션 조상(`Widget` · `PanelWidget`)의 것입니다.
 *          레이아웃 걷기 없이 짓는 시험은 `UiTestUtil::placeWidget` 으로 기하를 직접 넣습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTree.h"

namespace sw::test
{
    /** @brief 시험 위젯이 받은 사건 한 줄입니다("이름 단계 종류"). */
    struct UiEventRecord
    {
        vector<string> _listLine{};

        void   add( const hashed_string& name, const utf8* pText ) { _listLine.push_back( string{ name.c_str() } + " " + pText ); }
        string joined() const
        {
            string result;
            for ( const string& line : _listLine )
            {
                if ( result.empty() == false )
                    result += ", ";
                result += line;
            }
            return result;
        }
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 고정 원하는 크기 · 포커스 받기 여부를 정하는 잎 위젯입니다. 받은 사건을 기록에 적고, 정한 단계에서 처리합니다. */
    class TestBoxWidget : public Widget
    {
    public:
        explicit TestBoxWidget( const hashed_string& name = {}, bool bFocusable = false )
            : Widget{}
            , _pRecord{ nullptr }
            , _focusChangeCount{ 0 }
            , _hoverChangeCount{ 0 }
            , _clickCount{ 0 }
            , _bFocusable{ bFocusable }
            , _bTextInput{ false }
            , _bHandleTunnel{ false }
            , _bHandleBubble{ false }
            , _bCaptureOnDown{ false }
        {
            setName( name );
        }

        bool supportsFocus() const override { return _bFocusable; }
        bool supportsTextInput() const override { return _bTextInput; }

        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override
        {
            const utf8* pPhase = phase == UiRoutePhase::Tunnel ? "T" : "B";
            if ( _pRecord != nullptr )
                _pRecord->add( getName(), pPhase );
            const bool bHandle = ( phase == UiRoutePhase::Tunnel && _bHandleTunnel ) || ( phase == UiRoutePhase::Bubble && _bHandleBubble );
            if ( bHandle == false )
                return UiReply::makeUnhandled();
            UiReply reply = UiReply::makeHandled();
            if ( _bCaptureOnDown && event._kind == UiPointerEventKind::Down )
                reply.capturePointer();
            if ( event._kind == UiPointerEventKind::Up )
                reply.releasePointer();
            return reply;
        }

        UiReply onActionEvent( const UiActionEvent& event, UiRoutePhase phase ) override
        {
            if ( _pRecord != nullptr )
                _pRecord->add( getName(), phase == UiRoutePhase::Tunnel ? "T action" : "B action" );
            if ( phase == UiRoutePhase::Bubble && _bFocusable && event._action == hashed_string( "UI.Accept" ) )
            {
                ++_clickCount;
                return UiReply::makeHandled();
            }
            return UiReply::makeUnhandled();
        }

        void onFocusChanged( bool bFocused ) override
        {
            (void)bFocused;
            ++_focusChangeCount;
        }

        void onHoverChanged( bool bHovered ) override
        {
            if ( _pRecord != nullptr )
                _pRecord->add( getName(), bHovered ? "Enter" : "Leave" );
            ++_hoverChangeCount;
        }

        UiEventRecord* _pRecord;
        uint32         _focusChangeCount;
        uint32         _hoverChangeCount;
        uint32         _clickCount;
        bool           _bFocusable;
        bool           _bTextInput;
        bool           _bHandleTunnel;
        bool           _bHandleBubble;
        bool           _bCaptureOnDown;
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 자식을 드는 시험 패널입니다. `_bLayoutBoundary` 면 크기가 고정된 패널(레이아웃 경계)로 답합니다. */
    class TestPanelWidget : public PanelWidget
    {
    public:
        explicit TestPanelWidget( const hashed_string& name = {}, bool bLayoutBoundary = false )
            : PanelWidget{}
            , _pRecord{ nullptr }
            , _bLayoutBoundary{ bLayoutBoundary }
            , _bHandleTunnel{ false }
        {
            setName( name );
        }

        bool isLayoutBoundary() const override { return _bLayoutBoundary; }

        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override
        {
            (void)event;
            if ( _pRecord != nullptr )
                _pRecord->add( getName(), phase == UiRoutePhase::Tunnel ? "T" : "B" );
            return phase == UiRoutePhase::Tunnel && _bHandleTunnel ? UiReply::makeHandled() : UiReply::makeUnhandled();
        }

        void onHoverChanged( bool bHovered ) override
        {
            if ( _pRecord != nullptr )
                _pRecord->add( getName(), bHovered ? "Enter" : "Leave" );
        }

        UiEventRecord* _pRecord;
        bool           _bLayoutBoundary;
        bool           _bHandleTunnel;
    };
} // namespace sw::test

namespace sw::test
{
    struct UiTestUtil
    {
        /** @brief 레이아웃 없이 위젯을 화면 사각형에 놓습니다(렌더 변환이 있으면 그것도 겹친다). */
        static void placeWidget( Widget& widget, float32 x, float32 y, float32 width, float32 height )
        {
            const WidgetGeometry rect = WidgetGeometry::makeAxisAligned( float2{ x, y }, float2{ width, height } );
            widget.setArrangedGeometry( widget.getRenderTransform().applyTo( rect ) );
        }
    };
} // namespace sw::test
