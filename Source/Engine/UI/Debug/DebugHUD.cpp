#include "pch.h"

#include "Engine/UI/Debug/DebugHUD.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Log/Logger.h"
    #include "Core/Math/MathUtil.h"

    #include "Engine/Automation/AutomationProbe.h"
    #include "Engine/Common/EngineServices.h"
    #include "Engine/Graphics/Canvas/CanvasPainter.h"
    #include "Engine/Input/Map/InputMap.h"
    #include "Engine/Profiling/FrameProfiler.h"
    #include "Engine/UI/Layout/BoxPanel.h"
    #include "Engine/UI/Layout/CanvasPanel.h"
    #include "Engine/UI/UISystem.h"
    #include "Engine/UI/Widgets/BorderPanel.h"
    #include "Engine/UI/Widgets/CheckBoxWidget.h"
    #include "Engine/UI/Widgets/TextWidget.h"
    #include "Engine/UI/Widgets/UIBrush.h"
    #include "Engine/UserSettings/UserSettingsManager.h"
    #include "Engine/UserSettings/UserSettingsVariables.h"

SW_TEST_GLOBAL_VARIABLE( sw::string, gv_debugHUDProbeSection, "", "자동화 탐침 DebugHUD.SectionShown 이 볼 디버그 HUD 섹션 이름" );

namespace sw
{
    SW_LOG_CALLER( "DebugHUD" );

    namespace
    {
        struct DebugHUDInternal
        {
            /** @brief 섹션 본문을 다시 부르는 간격(초)입니다 — 언리얼 `stat` 처럼 읽을 수 있는 빠르기(4 Hz). 켜짐 · 목록이 바뀌면 바로 부른다. */
            static constexpr float32 kRefreshSeconds = 0.25f;
            /** @brief 화면 모서리에서 패널까지의 여백(UI 단위)입니다. */
            static constexpr float32 kPanelMargin = 16.0f;
            /** @brief 줄 이름 칸의 너비(UI 단위)입니다. */
            static constexpr float32 kLabelWidth = 150.0f;
            /** @brief 그래프 크기(UI 단위)입니다. */
            static constexpr float32 kGraphWidth    = 240.0f;
            static constexpr float32 kGraphHeight   = 40.0f;
            static constexpr float32 kTitleFontSize = 16.0f;
            static constexpr float32 kLineFontSize  = 14.0f;

            static constexpr const utf8* kSectionWidgetPrefix = "Section.";

            static constexpr float4 kPanelColor{ 0.04f, 0.05f, 0.07f, 0.82f };
            static constexpr float4 kTitleColor{ 1.0f, 0.82f, 0.3f, 1.0f };
            static constexpr float4 kLabelColor{ 0.7f, 0.74f, 0.8f, 1.0f };
            static constexpr float4 kValueColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            static constexpr float4 kGraphColor{ 0.35f, 0.85f, 0.45f, 1.0f };
            static constexpr float4 kGraphBackground{ 0.0f, 0.0f, 0.0f, 0.45f };

            static constexpr const utf8* kArrCornerName[] = { "topLeft", "topRight", "bottomLeft", "bottomRight" };

            static const hashed_string& getShownSettingID()
            {
                static const hashed_string s_id( "debug.hud" );
                return s_id;
            }
            static const hashed_string& getSectionsSettingID()
            {
                static const hashed_string s_id( "debug.hudSections" );
                return s_id;
            }
            static const hashed_string& getCornerSettingID()
            {
                static const hashed_string s_id( "debug.hudCorner" );
                return s_id;
            }
            static const hashed_string& getOpacitySettingID()
            {
                static const hashed_string s_id( "debug.hudOpacity" );
                return s_id;
            }

            static unique_ptr<TextWidget> makeText( string_view text, float32 fontSize, const float4& color )
            {
                unique_ptr<TextWidget> widget = make_unique<TextWidget>();
                TextLayoutStyle        style  = widget->getTextStyle();
                style._fontSize               = fontSize;
                widget->setTextStyle( style );
                widget->setColor( color );
                widget->setLocalized( false ); // 개발 도구라 영어 고정(README "함정")
                widget->setText( text );
                return widget;
            }

            static string makeSectionWidgetName( string_view sectionName )
            {
                string name( kSectionWidgetPrefix );
                name.append( sectionName.data(), sectionName.size() );
                return name;
            }

            /** @brief 모서리의 캔버스 슬롯 — 오른쪽 · 아래 모서리는 앵커 1 과 음의 여백, 그쪽 변을 두고 커진다. */
            static void applyCorner( Widget& panel, DebugHUDCorner corner )
            {
                const bool       bRight  = corner == DebugHUDCorner::TopRight || corner == DebugHUDCorner::BottomRight;
                const bool       bBottom = corner == DebugHUDCorner::BottomLeft || corner == DebugHUDCorner::BottomRight;
                const float2     anchor{ bRight ? 1.0f : 0.0f, bBottom ? 1.0f : 0.0f };
                const float2     offset{ bRight ? -kPanelMargin : kPanelMargin, bBottom ? -kPanelMargin : kPanelMargin };
                WidgetLayoutSlot slot = panel.getLayoutSlot();
                slot._anchorMin       = anchor;
                slot._anchorMax       = anchor;
                slot._offsetMin       = offset;
                slot._offsetMax       = offset;
                slot._bAutoSize       = true;
                slot._growHorizontal  = bRight ? UIGrowDirection::Begin : UIGrowDirection::End;
                slot._growVertical    = bBottom ? UIGrowDirection::Begin : UIGrowDirection::End;
                panel.setLayoutSlot( slot );
            }

            static DebugHUDCorner clampCorner( int32 value )
            {
                const bool bInRange = 0 <= value && value < static_cast<int32>( DebugHUDCorner::Count );
                return bInRange ? static_cast<DebugHUDCorner>( value ) : DebugHUDCorner::TopLeft;
            }

            static string makeFloatText( float32 value )
            {
                fixed_string<constant::kMaxBuffer32> text;
                formatstring( text.data(), text.capacity(), "%.2f", value );
                return string( text.c_str() );
            }
        };

        /**
         * @class DebugHUDGraphWidget
         * @brief 막대 그래프 하나를 칠합니다(프레임 시간). 리플렉션 타입이 아니다 — 문서에 쓰지 않고 HUD 만 코드로 짓는다.
         */
        class DebugHUDGraphWidget final : public Widget
        {
        public:
            DebugHUDGraphWidget()
                : Widget{}
                , _listValue{}
                , _maxValue{ 1.0f }
            {
            }

            /** @brief 값을 바꿉니다(그리기만 다시). */
            void setValues( const vector<float32>& listValue, float32 maxValue )
            {
                _listValue = listValue;
                _maxValue  = maxValue > 0.0f ? maxValue : 1.0f;
                invalidate( WidgetDirty::kPaint );
            }

        protected:
            float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override
            {
                (void)context;
                (void)availableSize;
                return float2{ DebugHUDInternal::kGraphWidth, DebugHUDInternal::kGraphHeight };
            }

            void paint( CanvasPainter& painter, const UIPaintContext& context ) const override
            {
                (void)context;
                const float2& size = getGeometry()._size;
                CanvasBrush   background{};
                background._color = DebugHUDInternal::kGraphBackground;
                painter.fillRect( float2{}, size, background );
                if ( _listValue.empty() )
                    return;
                CanvasBrush bar{};
                bar._color             = DebugHUDInternal::kGraphColor;
                const float32 barWidth = size._x / static_cast<float32>( _listValue.size() );
                for ( size_t index = 0; index < _listValue.size(); ++index )
                {
                    const float32 height = size._y * MathUtil::saturate( _listValue[index] / _maxValue );
                    painter.fillRect( float2{ barWidth * static_cast<float32>( index ), size._y - height }, float2{ barWidth, height }, bar );
                }
            }

        private:
            vector<float32> _listValue;
            float32         _maxValue;
        };

        /**
         * @class DebugHUDScreen
         * @brief HUD 화면입니다(오버레이 층 — 포커스 · 포인터 · 게임 입력을 받지 않는다). 켜진 섹션마다 제목 · 줄 · 그래프 상자 하나를 둡니다.
         * @details `onTick` 이 프레임 시간을 모으고, `kRefreshSeconds` 마다(또는 섹션 목록 · 설정 글이 바뀌면 바로) 켜진 섹션의 본문만 부릅니다.
         *          위젯은 줄 수 · 그래프 유무가 그대로면 글만 바꾼다(같은 글이면 무효화도 없다).
         */
        class DebugHUDScreen final : public UIScreen
        {
        public:
            DebugHUDScreen( const UIScreenDesc& desc, unique_ptr<Widget> root, WidgetID panel, WidgetID column )
                : UIScreen{ desc, std::move( root ) }
                , _writer{}
                , _listView{}
                , _listShownScratch{}
                , _seenState{}
                , _seenRevision{ invalid_index::kUint64 }
                , _panel{ panel }
                , _column{ column }
                , _secondsSinceRefresh{ DebugHUDInternal::kRefreshSeconds }
                , _seenOpacity{ -1.0f }
                , _seenCorner{ -1 }
            {
            }

            /** @brief HUD 화면을 짓습니다(캔버스 루트 → 패널 → 세로 상자). */
            static unique_ptr<DebugHUDScreen> create()
            {
                unique_ptr<CanvasPanel> root  = make_unique<CanvasPanel>();
                unique_ptr<BorderPanel> panel = make_unique<BorderPanel>();
                panel->setName( hashed_string( DebugHUD::kPanelName ) );
                panel->setBackground( UIBrush::makeSolid( DebugHUDInternal::kPanelColor, 6.0f ) );
                panel->setContentPadding( float4{ 10.0f, 8.0f, 10.0f, 8.0f } );
                unique_ptr<BoxPanel> column = make_unique<BoxPanel>();
                column->setName( hashed_string( "DebugHUDColumn" ) );
                column->setOrientation( UIOrientation::Vertical );
                column->setSpacing( 8.0f );
                const WidgetID panelID  = panel->getID();
                const WidgetID columnID = column->getID();
                (void)panel->addChild( std::move( column ) );
                (void)root->addChild( std::move( panel ) );

                UIScreenDesc desc{};
                desc._layer       = UILayer::Overlay;
                desc._bTakesFocus = false;
                desc._bShowCursor = false;
                return sw::make_unique<DebugHUDScreen>( desc, std::move( root ), panelID, columnID );
            }

            void onTick( float32 deltaSeconds ) override
            {
                _writer.recordFrameSeconds( deltaSeconds );
                applyPlacement();
                _secondsSinceRefresh += deltaSeconds;
                const DebugHUDRegistry& registry     = DebugHUDRegistry::get();
                const bool              bListChanged = registry.getRevision() != _seenRevision || gv_debugHUDSections != _seenState;
                if ( bListChanged == false && _secondsSinceRefresh < DebugHUDInternal::kRefreshSeconds )
                    return;
                _secondsSinceRefresh = 0.0f;
                _seenRevision        = registry.getRevision();
                _seenState           = gv_debugHUDSections;
                refreshSections();
            }

            /** @brief 지금 그린 섹션 수입니다(탐침). */
            uint32 getSectionViewCount() const { return static_cast<uint32>( _listView.size() ); }
            /** @brief 섹션 @p name 의 상자를 그리고 있으면 true 입니다(탐침). */
            bool hasSectionView( string_view name ) const
            {
                for ( const SectionView& view : _listView )
                {
                    if ( StringUtil::equals( view._name, name, true ) )
                        return true;
                }
                return false;
            }

        private:
            /** @struct SectionView @brief 그리는 섹션 하나 — 상자 위젯 · 줄 수 · 그래프 위젯입니다. */
            struct SectionView
            {
                string   _name;
                WidgetID _box;
                WidgetID _graph; ///< 없으면 0
                uint32   _lineCount;
            };

            void applyPlacement()
            {
                const int32   corner  = static_cast<int32>( DebugHUD::getCorner() );
                const float32 opacity = DebugHUD::getOpacity();
                if ( corner == _seenCorner && opacity == _seenOpacity )
                    return;
                Widget* pPanel = getTree().findWidgetByID( _panel );
                if ( pPanel == nullptr )
                    return;
                DebugHUDInternal::applyCorner( *pPanel, static_cast<DebugHUDCorner>( corner ) );
                pPanel->setOpacity( opacity );
                _seenCorner  = corner;
                _seenOpacity = opacity;
            }

            void refreshSections()
            {
                SW_PROFILE_SCOPE( "GT.DebugHUD.Sections" );
                _listShownScratch.clear();
                for ( const DebugHUDSectionRegistration* pRegistration : DebugHUDRegistry::get().getSections() )
                {
                    if ( DebugHUD::isSectionShown( *pRegistration ) )
                        _listShownScratch.push_back( pRegistration );
                }
                if ( matchesShownSections() == false )
                    rebuildSectionViews();
                for ( size_t index = 0; index < _listShownScratch.size(); ++index )
                {
                    _writer.reset();
                    _listShownScratch[index]->_pFunc( _writer );
                    writeSectionView( _listView[index], *_listShownScratch[index] );
                }
                _listShownScratch.clear(); // 등록 포인터는 이 호출 안에서만 — 다음 갱신 전에 모듈이 내려갈 수 있다
            }

            bool matchesShownSections() const
            {
                if ( _listView.size() != _listShownScratch.size() )
                    return false;
                for ( size_t index = 0; index < _listView.size(); ++index )
                {
                    if ( StringUtil::equals( _listView[index]._name, _listShownScratch[index]->_pName, true ) == false )
                        return false;
                }
                return true;
            }

            void rebuildSectionViews()
            {
                PanelWidget* pColumn = castTo<PanelWidget>( getTree().findWidgetByID( _column ) );
                if ( pColumn == nullptr )
                    return;
                pColumn->clearChildren();
                _listView.clear();
                for ( const DebugHUDSectionRegistration* pRegistration : _listShownScratch )
                {
                    unique_ptr<BoxPanel> box = make_unique<BoxPanel>();
                    box->setName( hashed_string( DebugHUDInternal::makeSectionWidgetName( pRegistration->_pName ) ) );
                    box->setOrientation( UIOrientation::Vertical );
                    box->setSpacing( 2.0f );
                    (void)box->addChild( DebugHUDInternal::makeText( pRegistration->_pTitle, DebugHUDInternal::kTitleFontSize, DebugHUDInternal::kTitleColor ) );
                    _listView.push_back( SectionView{ string( pRegistration->_pName ), box->getID(), 0, 0 } );
                    (void)pColumn->addChild( std::move( box ) );
                }
            }

            /** @brief 상자의 자식을 이번 줄 수 · 그래프 유무에 맞추고 글을 씁니다(제목 → 그래프 → 줄 순서). */
            void writeSectionView( SectionView& view, const DebugHUDSectionRegistration& registration )
            {
                PanelWidget* pBox = castTo<PanelWidget>( getTree().findWidgetByID( view._box ) );
                if ( pBox == nullptr )
                    return;
                const bool bHasGraph = _writer.hasGraph();
                const bool bReshaped = view._lineCount != _writer.getLineCount() || ( view._graph != 0 ) != bHasGraph;
                if ( bReshaped )
                {
                    pBox->clearChildren();
                    (void)pBox->addChild( DebugHUDInternal::makeText( registration._pTitle, DebugHUDInternal::kTitleFontSize, DebugHUDInternal::kTitleColor ) );
                    view._graph = 0;
                    if ( bHasGraph )
                    {
                        unique_ptr<DebugHUDGraphWidget> graph = make_unique<DebugHUDGraphWidget>();
                        view._graph                           = graph->getID();
                        (void)pBox->addChild( std::move( graph ) );
                    }
                    for ( uint32 index = 0; index < _writer.getLineCount(); ++index )
                    {
                        unique_ptr<BoxPanel>   row   = make_unique<BoxPanel>();
                        unique_ptr<TextWidget> label = DebugHUDInternal::makeText( {}, DebugHUDInternal::kLineFontSize, DebugHUDInternal::kLabelColor );
                        WidgetLayoutSlot       slot  = label->getLayoutSlot();
                        slot._widthOverride          = DebugHUDInternal::kLabelWidth;
                        label->setLayoutSlot( slot );
                        (void)row->addChild( std::move( label ) );
                        (void)row->addChild( DebugHUDInternal::makeText( {}, DebugHUDInternal::kLineFontSize, DebugHUDInternal::kValueColor ) );
                        (void)pBox->addChild( std::move( row ) );
                    }
                    view._lineCount = _writer.getLineCount();
                }
                if ( bHasGraph )
                {
                    // 그래프 위젯은 리플렉션 타입이 아니라 castTo 로 풀 수 없다 — 이 번호는 위에서 DebugHUDGraphWidget 으로 지은 것이다.
                    Widget* pGraph = getTree().findWidgetByID( view._graph );
                    if ( pGraph != nullptr )
                        static_cast<DebugHUDGraphWidget*>( pGraph )->setValues( _writer.getGraph(), _writer.getGraphMax() );
                }
                const uint32 firstRow = bHasGraph ? 2u : 1u;
                for ( uint32 index = 0; index < view._lineCount && firstRow + index < pBox->getChildCount(); ++index )
                {
                    PanelWidget* pRow = castTo<PanelWidget>( pBox->getChild( firstRow + index ) );
                    if ( pRow == nullptr || pRow->getChildCount() < 2 )
                        continue;
                    const DebugHUDLine& line   = _writer.getLineAt( index );
                    TextWidget*         pLabel = castTo<TextWidget>( pRow->getChild( 0 ) );
                    TextWidget*         pValue = castTo<TextWidget>( pRow->getChild( 1 ) );
                    if ( pLabel != nullptr )
                        pLabel->setText( line._label );
                    if ( pValue != nullptr )
                        pValue->setText( line._value );
                }
            }

        private:
            DebugHUDSectionWriter                      _writer;
            vector<SectionView>                        _listView;
            vector<const DebugHUDSectionRegistration*> _listShownScratch; ///< 이번 갱신의 켜진 섹션(호출 안에서만 — 모듈이 내려가면 빠진다)
            string                                     _seenState;        ///< 마지막으로 본 `gv_debugHUDSections`
            uint64                                     _seenRevision;     ///< 마지막으로 본 등록부 판
            WidgetID                                   _panel;
            WidgetID                                   _column;
            float32                                    _secondsSinceRefresh;
            float32                                    _seenOpacity;
            int32                                      _seenCorner;
        };

        /**
         * @class DebugHUDWindowScreen
         * @brief 설정 창입니다(문서 `engine/ui/debughud.ui.xml`). 표시 · 모서리 · 불투명도는 문서의 설정 바인딩이, 섹션 체크 상자는 이 클래스가 등록부에서 짓습니다.
         * @details 체크 상자를 사용자가 바꾸면 `DebugHUD::setSectionShown` 으로 쓰고, 명령 · 다른 곳에서 설정이 바뀌면 체크 상자를 따라 맞춥니다.
         */
        class DebugHUDWindowScreen final : public UIScreen
        {
        public:
            DebugHUDWindowScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
                : UIScreen{ desc, std::move( root ) }
                , _listSectionName{}
                , _listCheckBox{}
                , _listLastChecked{}
                , _seenRevision{ invalid_index::kUint64 }
            {
            }

            void onTick( float32 deltaSeconds ) override
            {
                (void)deltaSeconds;
                const DebugHUDRegistry& registry = DebugHUDRegistry::get();
                if ( registry.getRevision() != _seenRevision )
                    rebuildCheckBoxes();
                DebugHUD* pHUD = DebugHUD::findActive();
                for ( size_t index = 0; index < _listSectionName.size(); ++index )
                {
                    const DebugHUDSectionRegistration* pRegistration = registry.findSection( _listSectionName[index] );
                    CheckBoxWidget*                    pCheckBox     = castTo<CheckBoxWidget>( getTree().findWidgetByID( _listCheckBox[index] ) );
                    if ( pRegistration == nullptr || pCheckBox == nullptr )
                        continue;
                    const bool bChecked = pCheckBox->isChecked();
                    if ( bChecked != ( _listLastChecked[index] == SW_TRUE ) && pHUD != nullptr )
                        (void)pHUD->setSectionShown( _listSectionName[index], bChecked ); // 이름은 방금 등록부에서 찾았다
                    else
                        pCheckBox->setChecked( DebugHUD::isSectionShown( *pRegistration ) );
                    _listLastChecked[index] = pCheckBox->isChecked() ? SW_TRUE : SW_FALSE;
                }
            }

            bool onCommand( const hashed_string& command, Widget& source ) override
            {
                if ( command == hashed_string( "Close" ) )
                {
                    close();
                    return true;
                }
                return UIScreen::onCommand( command, source );
            }

            void onTreeRebuilt() override { _seenRevision = invalid_index::kUint64; }

        private:
            void rebuildCheckBoxes()
            {
                const DebugHUDRegistry& registry = DebugHUDRegistry::get();
                _seenRevision                    = registry.getRevision();
                _listSectionName.clear();
                _listCheckBox.clear();
                _listLastChecked.clear();
                PanelWidget* pSections = getTree().findWidget<PanelWidget>( hashed_string( "Sections" ) );
                if ( pSections == nullptr )
                    return;
                pSections->clearChildren();
                for ( const DebugHUDSectionRegistration* pRegistration : registry.getSections() )
                {
                    unique_ptr<CheckBoxWidget> checkBox = make_unique<CheckBoxWidget>();
                    checkBox->setName( hashed_string( DebugHUDInternal::makeSectionWidgetName( pRegistration->_pName ) ) );
                    const bool bShown = DebugHUD::isSectionShown( *pRegistration );
                    checkBox->setChecked( bShown );
                    string label( pRegistration->_pName );
                    label += "  -  ";
                    label += pRegistration->_pTitle;
                    (void)checkBox->addChild( DebugHUDInternal::makeText( label, DebugHUDInternal::kLineFontSize + 2.0f, DebugHUDInternal::kValueColor ) );
                    _listSectionName.push_back( string( pRegistration->_pName ) );
                    _listCheckBox.push_back( checkBox->getID() );
                    _listLastChecked.push_back( bShown ? SW_TRUE : SW_FALSE );
                    (void)pSections->addChild( std::move( checkBox ) );
                }
            }

        private:
            vector<string>   _listSectionName;
            vector<WidgetID> _listCheckBox;
            vector<uint8>    _listLastChecked; ///< 지난 틱의 체크 — 다르면 사용자가 바꾼 것
            uint64           _seenRevision;
        };

        /** @brief 지금 동작 중인 조종자(Engine 이미지 — 핫 리로드와 무관)입니다. */
        DebugHUD* s_pActiveHUD = nullptr;

        struct DebugHUDCommandInternal
        {
            static void appendSectionList( string& outReply )
            {
                for ( const DebugHUDSectionRegistration* pRegistration : DebugHUDRegistry::get().getSections() )
                {
                    if ( outReply.empty() == false )
                        outReply += ", ";
                    outReply += pRegistration->_pName;
                    outReply += DebugHUD::isSectionShown( *pRegistration ) ? " on" : " off";
                }
            }

            [[nodiscard]] static bool readOnOff( const string& word, bool& outValue )
            {
                if ( StringUtil::equals( word, "on", true ) )
                {
                    outValue = true;
                    return true;
                }
                if ( StringUtil::equals( word, "off", true ) )
                {
                    outValue = false;
                    return true;
                }
                return false;
            }

            static bool runSectionCommand( DebugHUD& hud, const vector<string>& listArgument, string& outReply )
            {
                const DebugHUDSectionRegistration* pRegistration = DebugHUDRegistry::get().findSection( listArgument[0] );
                if ( pRegistration == nullptr )
                {
                    outReply = "unknown section '" + listArgument[0] + "' - sections: ";
                    appendSectionList( outReply );
                    return false;
                }
                bool bShown = DebugHUD::isSectionShown( *pRegistration ) == false;
                if ( listArgument.size() >= 2 && readOnOff( listArgument[1], bShown ) == false )
                    return false;
                (void)hud.setSectionShown( pRegistration->_pName, bShown ); // 방금 등록부에서 찾은 이름이다
                if ( bShown )
                    hud.setShown( true ); // 섹션을 켜면 HUD 도 보인다(언리얼 `stat fps`)
                outReply = string( "debug HUD section " ) + pRegistration->_pName + ( bShown ? " on" : " off" );
                return true;
            }

            static bool runCommand( const vector<string>& listArgument, string& outReply )
            {
                DebugHUD* pHUD = DebugHUD::findActive();
                if ( pHUD == nullptr )
                {
                    outReply = "no debug HUD in this run (no runtime UI)";
                    return false;
                }
                bool bShown = pHUD->isShown() == false;
                if ( listArgument.empty() || readOnOff( listArgument[0], bShown ) )
                {
                    pHUD->setShown( bShown );
                    outReply = bShown ? "debug HUD on" : "debug HUD off";
                    return true;
                }
                const string& word = listArgument[0];
                if ( StringUtil::equals( word, "list", true ) )
                {
                    appendSectionList( outReply );
                    return true;
                }
                if ( StringUtil::equals( word, "window", true ) )
                {
                    outReply = "debug HUD window";
                    return pHUD->openWindow();
                }
                if ( StringUtil::equals( word, "corner", true ) )
                {
                    DebugHUDCorner corner = DebugHUDCorner::TopLeft;
                    if ( listArgument.size() < 2 || DebugHUD::tryParseCorner( listArgument[1], corner ) == false )
                        return false;
                    pHUD->setCorner( corner );
                    outReply = string( "debug HUD corner " ) + DebugHUD::getCornerName( corner );
                    return true;
                }
                if ( StringUtil::equals( word, "opacity", true ) )
                {
                    float32 opacity = 0.0f;
                    if ( listArgument.size() < 2 || StringUtil::parseFloat( listArgument[1], opacity ) == false )
                        return false;
                    pHUD->setOpacity( opacity );
                    outReply = "debug HUD opacity " + DebugHUDInternal::makeFloatText( DebugHUD::getOpacity() );
                    return true;
                }
                return runSectionCommand( *pHUD, listArgument, outReply );
            }
        };

        struct DebugHUDProbeInternal
        {
            [[nodiscard]] static bool readShown( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const DebugHUD* pHUD = DebugHUD::findActive();
                outValue             = pHUD != nullptr && pHUD->isScreenOpen() ? 1.0 : 0.0;
                return pHUD != nullptr;
            }

            [[nodiscard]] static bool readWindowOpen( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const DebugHUD* pHUD = DebugHUD::findActive();
                outValue             = pHUD != nullptr && pHUD->isWindowOpen() ? 1.0 : 0.0;
                return pHUD != nullptr;
            }

            [[nodiscard]] static bool readCorner( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const DebugHUD* pHUD   = DebugHUD::findActive();
                DebugHUDCorner  corner = DebugHUDCorner::TopLeft;
                if ( pHUD == nullptr || pHUD->tryGetArrangedCorner( corner ) == false )
                    return false;
                outValue = static_cast<float64>( corner );
                return true;
            }

            [[nodiscard]] static bool readSectionCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const DebugHUD* pHUD = DebugHUD::findActive();
                outValue             = pHUD != nullptr ? static_cast<float64>( pHUD->getDrawnSectionCount() ) : 0.0;
                return pHUD != nullptr;
            }

            [[nodiscard]] static bool readSectionShown( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const DebugHUD* pHUD = DebugHUD::findActive();
                outValue             = pHUD != nullptr && pHUD->isSectionDrawn( gv_debugHUDProbeSection ) ? 1.0 : 0.0;
                return pHUD != nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( debugHUD, "hud", "hud [on|off|list|window] | hud <section> [on|off] | hud corner <topLeft|topRight|bottomLeft|bottomRight> | hud opacity <0.2..1>",
                    "Show or hide the debug HUD and its sections (Ctrl+F3 toggles it)", &DebugHUDCommandInternal::runCommand );

    SW_AUTOMATION_PROBE( debugHUDShown, "DebugHUD.Shown", "1 while the debug HUD screen is open", &DebugHUDProbeInternal::readShown );
    SW_AUTOMATION_PROBE( debugHUDWindowOpen, "DebugHUD.WindowOpen", "1 while the debug HUD settings window is open", &DebugHUDProbeInternal::readWindowOpen );
    SW_AUTOMATION_PROBE( debugHUDCorner, "DebugHUD.Corner", "Corner the HUD panel was laid out in (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right)",
                         &DebugHUDProbeInternal::readCorner );
    SW_AUTOMATION_PROBE( debugHUDSectionCount, "DebugHUD.SectionCount", "Sections the debug HUD screen draws right now", &DebugHUDProbeInternal::readSectionCount );
    SW_AUTOMATION_PROBE( debugHUDSectionShown, "DebugHUD.SectionShown", "1 while the debug HUD draws the section named by gv_debugHUDProbeSection",
                         &DebugHUDProbeInternal::readSectionShown );
} // namespace sw

namespace sw
{
    DebugHUD::DebugHUD()
        : _pUI{ nullptr }
        , _pSettings{ nullptr }
        , _toggleAction{ kToggleActionName }
        , _screen{ kInvalidUIScreenHandle }
        , _window{ kInvalidUIScreenHandle }
        , _bProfilerEnabledByHUD{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    DebugHUD::~DebugHUD()
    {
        shutdown();
    }

    void DebugHUD::initialize( UISystem& ui, UserSettingsManager* pSettings )
    {
        shutdown();
        _pUI       = &ui;
        _pSettings = pSettings;
        if ( s_pActiveHUD == nullptr )
            s_pActiveHUD = this;
    }

    void DebugHUD::shutdown()
    {
        if ( _pUI != nullptr )
        {
            if ( _screen != kInvalidUIScreenHandle )
                _pUI->closeScreen( _screen );
            if ( _window != kInvalidUIScreenHandle )
                _pUI->closeScreen( _window );
        }
        _screen = kInvalidUIScreenHandle;
        _window = kInvalidUIScreenHandle;
        if ( _bProfilerEnabledByHUD == SW_TRUE && engine::areEngineServicesBound() )
            engine::getFrameProfiler().setEnabled( false );
        _bProfilerEnabledByHUD = SW_FALSE;
        _pUI                   = nullptr;
        _pSettings             = nullptr;
        if ( s_pActiveHUD == this )
            s_pActiveHUD = nullptr;
    }

    void DebugHUD::update( const InputMap* pShellMap )
    {
        SW_PROFILE_SCOPE( "GT.DebugHUD" ); // 꺼진 상태의 비용을 재는 구간(검증과 측정 2절) — 섹션 본문은 GT.DebugHUD.Sections
        if ( _pUI == nullptr )
            return;
        if ( pShellMap != nullptr && pShellMap->wasActionTriggered( _toggleAction ) )
            setShown( isShown() == false );
        const bool bScreenOpen = _screen != kInvalidUIScreenHandle;
        if ( isShown() != bScreenOpen )
            syncScreen();
        if ( _screen != kInvalidUIScreenHandle || _bProfilerEnabledByHUD == SW_TRUE )
            syncFrameProfiler();
        // 창이 닫혔다(닫기 단추 · 뒤로) — 창의 설정 바인딩이 넣은 보류 값을 확정해 사용자 파일에 남긴다.
        if ( _window != kInvalidUIScreenHandle && _pUI->findScreen( _window ) == nullptr )
        {
            _window = kInvalidUIScreenHandle;
            if ( _pSettings != nullptr && _pSettings->hasPendingChanges() )
                (void)_pSettings->applyPending(); // 결과의 저장 실패는 매니저가 로그에 남긴다
        }
    }

    bool DebugHUD::isShown() const
    {
        return gv_debugHUD;
    }

    void DebugHUD::setShown( bool bShown )
    {
        writeSetting( DebugHUDInternal::getShownSettingID(), "gv_debugHUD", bShown ? "true" : "false" );
    }

    bool DebugHUD::isSectionShown( const DebugHUDSectionRegistration& registration )
    {
        return DebugHUDSectionState::isSectionShown( gv_debugHUDSections, registration );
    }

    bool DebugHUD::setSectionShown( string_view name, bool bShown )
    {
        const DebugHUDSectionRegistration* pRegistration = DebugHUDRegistry::get().findSection( name );
        if ( pRegistration == nullptr )
            return false;
        const bool   bDefaultShown = ( pRegistration->_flags & DebugHUDSectionFlag::kDefaultOn ) != 0;
        const string stateText     = DebugHUDSectionState::makeStateText( gv_debugHUDSections, pRegistration->_pName, bShown, bDefaultShown );
        writeSetting( DebugHUDInternal::getSectionsSettingID(), "gv_debugHUDSections", stateText );
        return true;
    }

    uint32 DebugHUD::getShownSectionCount()
    {
        uint32 count = 0;
        for ( const DebugHUDSectionRegistration* pRegistration : DebugHUDRegistry::get().getSections() )
        {
            if ( isSectionShown( *pRegistration ) )
                ++count;
        }
        return count;
    }

    DebugHUDCorner DebugHUD::getCorner()
    {
        return DebugHUDInternal::clampCorner( gv_debugHUDCorner );
    }

    void DebugHUD::setCorner( DebugHUDCorner corner )
    {
        writeSetting( DebugHUDInternal::getCornerSettingID(), "gv_debugHUDCorner", getCornerName( corner ) );
    }

    float32 DebugHUD::getOpacity()
    {
        return MathUtil::clamp( static_cast<float32>( gv_debugHUDOpacity ), kMinOpacity, kMaxOpacity );
    }

    void DebugHUD::setOpacity( float32 opacity )
    {
        const float32 clamped = MathUtil::clamp( opacity, kMinOpacity, kMaxOpacity );
        writeSetting( DebugHUDInternal::getOpacitySettingID(), "gv_debugHUDOpacity", DebugHUDInternal::makeFloatText( clamped ) );
    }

    bool DebugHUD::openWindow()
    {
        if ( _pUI == nullptr )
            return false;
        if ( isWindowOpen() )
            return true;
        _window = _pUI->openScreen<DebugHUDWindowScreen>( kWindowDocumentPath );
        return _window != kInvalidUIScreenHandle;
    }

    void DebugHUD::closeWindow()
    {
        if ( _pUI != nullptr && _window != kInvalidUIScreenHandle )
            _pUI->closeScreen( _window );
    }

    bool DebugHUD::isWindowOpen() const
    {
        return _pUI != nullptr && _window != kInvalidUIScreenHandle && _pUI->findScreen( _window ) != nullptr;
    }

    bool DebugHUD::isScreenOpen() const
    {
        return _pUI != nullptr && _screen != kInvalidUIScreenHandle && _pUI->findScreen( _screen ) != nullptr;
    }

    uint32 DebugHUD::getDrawnSectionCount() const
    {
        // 이 번호의 화면은 `syncScreen` 이 DebugHUDScreen 으로 지은 것이다(코드 화면이라 castTo 로 풀 타입 정보가 없다).
        const UIScreen* pScreen = isScreenOpen() ? _pUI->findScreen( _screen ) : nullptr;
        return pScreen != nullptr ? static_cast<const DebugHUDScreen*>( pScreen )->getSectionViewCount() : 0;
    }

    bool DebugHUD::isSectionDrawn( string_view name ) const
    {
        const UIScreen* pScreen = isScreenOpen() ? _pUI->findScreen( _screen ) : nullptr;
        return pScreen != nullptr && static_cast<const DebugHUDScreen*>( pScreen )->hasSectionView( name );
    }

    bool DebugHUD::tryGetArrangedCorner( DebugHUDCorner& outCorner ) const
    {
        if ( isScreenOpen() == false )
            return false;
        const UIScreen* pScreen = _pUI->findScreen( _screen );
        const Widget*   pPanel  = pScreen->getTree().findWidget<Widget>( hashed_string( kPanelName ) );
        const float2&   size    = _pUI->getViewport()._size;
        if ( pPanel == nullptr || pPanel->getGeometry()._size._x <= 0.0f || size._x <= 0.0f )
            return false;
        const WidgetGeometry& geometry = pPanel->getGeometry();
        const float2          center{ geometry._position._x + geometry._size._x * 0.5f, geometry._position._y + geometry._size._y * 0.5f };
        const bool            bRight  = center._x > size._x * 0.5f;
        const bool            bBottom = center._y > size._y * 0.5f;
        outCorner                     = static_cast<DebugHUDCorner>( ( bBottom ? 2 : 0 ) + ( bRight ? 1 : 0 ) );
        return true;
    }

    DebugHUD* DebugHUD::findActive()
    {
        return s_pActiveHUD;
    }

    bool DebugHUD::tryParseCorner( string_view text, DebugHUDCorner& outCorner )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( DebugHUDCorner::Count ); ++index )
        {
            const bool bDigit = text.size() == 1 && text[0] == static_cast<utf8>( '0' + index );
            if ( bDigit || StringUtil::equals( text, DebugHUDInternal::kArrCornerName[index], true ) )
            {
                outCorner = static_cast<DebugHUDCorner>( index );
                return true;
            }
        }
        return false;
    }

    const utf8* DebugHUD::getCornerName( DebugHUDCorner corner )
    {
        const uint32 index = static_cast<uint32>( DebugHUDInternal::clampCorner( static_cast<int32>( corner ) ) );
        return DebugHUDInternal::kArrCornerName[index];
    }

    void DebugHUD::writeSetting( const hashed_string& settingID, const utf8* pVariableName, string_view value )
    {
        if ( _pSettings != nullptr && _pSettings->findSetting( settingID ) != nullptr )
        {
            (void)_pSettings->setPendingValue( settingID, value ); // 범위 밖이면 매니저가 맞춰 받는다
            (void)_pSettings->applyPending();                      // 저장 실패는 매니저가 로그에 남긴다
            return;
        }
        GlobalVariableInfo* pVariable = engine::getGlobalVariableManager().findVariable( pVariableName );
        if ( pVariable == nullptr || pVariable->setValueFromString( value ) == false )
            SW_LOG_WARNING( "Debug HUD could not write %# = '%#'", pVariableName, value );
    }

    void DebugHUD::syncScreen()
    {
        if ( isShown() )
        {
            _screen = _pUI->pushScreen( DebugHUDScreen::create() );
            return;
        }
        _pUI->closeScreen( _screen );
        _screen = kInvalidUIScreenHandle;
    }

    void DebugHUD::syncFrameProfiler()
    {
        bool bNeedsProfiler = false;
        if ( _screen != kInvalidUIScreenHandle )
        {
            for ( const DebugHUDSectionRegistration* pRegistration : DebugHUDRegistry::get().getSections() )
            {
                const bool bUsesProfiler = ( pRegistration->_flags & DebugHUDSectionFlag::kUsesFrameProfiler ) != 0;
                bNeedsProfiler           = bNeedsProfiler || ( bUsesProfiler && isSectionShown( *pRegistration ) );
            }
        }
        FrameProfiler& profiler = engine::getFrameProfiler();
        if ( bNeedsProfiler && profiler.isEnabled() == false )
        {
            profiler.setEnabled( true );
            _bProfilerEnabledByHUD = SW_TRUE;
        }
        else if ( bNeedsProfiler == false && _bProfilerEnabledByHUD == SW_TRUE )
        {
            profiler.setEnabled( false );
            _bProfilerEnabledByHUD = SW_FALSE;
        }
    }
} // namespace sw

#endif
