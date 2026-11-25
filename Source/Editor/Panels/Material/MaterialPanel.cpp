#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/Material/Model/MaterialPreviewInputs.h"
#include "Editor/Panels/Material/Model/MaterialViewModel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    /**
     * @brief 마스터 머티리얼 XML을 편집합니다.
     * @details 미리보기는 둘이다 — 렌더러가 그리는 미리보기 씬의 구(`EditorPreviewPanel`, 메인 씬과 섞이지 않는다)와,
     *          값으로 CPU 에서 셰이딩한 작은 구(`MaterialPreviewInputs` — 렌더 타깃이 없을 때의 폴백이자 감시 값 `Editor.MaterialPreviewRedMinusBlue` 의 출처).
     *          편집 사본 · 입력란 · 읽기 · 쓰기 · 미리보기 구 걸기는 `MaterialViewModel`(Model/, ImGui 없음)이 들고, 이 창은 위젯 · CPU 구 그리기와 문서 dirty · 되돌리기 기록만 맡는다.
     */
    class MaterialPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "material";

        MaterialPanel();

        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        /** @brief 지난 그리기의 미리보기 평균 (R - B)(0..255)입니다. 지난 프레임에 그리지 않았으면 false 입니다(감시 값 `Editor.MaterialPreviewRedMinusBlue`). */
        [[nodiscard]] bool findPreviewRedMinusBlue( float32& outValue ) const { return _model.findPreviewRedMinusBlue( outValue ); }
        /** @brief 미저장 편집 때문에 "그래도 바꿀까요?" 대답을 기다리는 중이면 true 입니다(감시 값 `Editor.MaterialSwitchPending`). */
        bool isWaitingForSwitchAnswer() const { return isSwitchConfirmPending(); }

        /** @brief 기본값과 다른 프로퍼티 수입니다(감시 값 `Editor.MaterialModifiedPropertyCount`). */
        uint32 countModifiedProperties() const;

    private:
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        /** @brief 편집 사본을 선택 오브젝트와 캐시 사본에 바로 보입니다. */
        void applyLivePreview();
        /** @brief 머티리얼 값으로 셰이딩한 구 미리보기를 그립니다(`MaterialPreviewInputs`). */
        void drawPreview();
        /** @brief 창이 접히거나 탭이 가려져 내용을 그리지 않은 프레임입니다 — 지난 미리보기 값은 더 이상 지금 문서의 것이 아니다. */
        void onPanelCollapsed() override { _model.beginFrame(); }
        /** @brief 미리보기 씬에 구 하나(`PreviewSubject`)를 스폰하고 카메라를 맞춥니다. */
        void populatePreviewScene( Scene& scene ) override;
        /** @brief 미리보기 구가 지금 문서의 머티리얼을 쓰게 합니다(경로가 바뀌었을 때만). */
        void syncPreviewSubjectMaterial();
        /** @brief 프로퍼티 목록(검색 · 행마다 기본값 되돌리기)을 그립니다. 언리얼 머티리얼 인스턴스 에디터의 파라미터 목록과 같은 자리다. */
        void drawPropertyList();

    private:
        MaterialViewModel                      _model;
        fixed_string<constant::buffer::kMax64> _propertyFilter; ///< 프로퍼티 검색 글(이름 · 표시 이름 부분 일치)
    };

    namespace
    {
        struct MaterialPanelInternal
        {
            /** @brief 값이 기본값과 다르면 true 입니다. 기본값이 없는 칸(텍스처 슬롯)은 되돌릴 곳이 없어 늘 false 다. */
            static bool isModified( const MaterialProperty& prop ) { return prop._defaultValue.empty() == false && prop._value != prop._defaultValue; }

            static constexpr Color kPreviewBg{ Color::fromBytes( 38, 38, 44, 255 ) };
            /// @brief 미리보기 한 변(96 DPI 기준 픽셀)입니다. CPU 구와 미리보기 씬 뷰포트의 높이가 같다.
            static constexpr float32 kPreviewSide = 160.0f;
            /// @brief 미리보기 씬 뷰포트의 가로 / 세로 비율입니다.
            static constexpr float32 kPreviewSceneAspect = 1.5f;
            /**
             * @brief 값 글을 형식대로 읽지 못한 행입니다. 0 으로 메운 슬라이더 대신 경고 표시와 원문 칸을 보여 사용자가 글을 고치게 한다.
             * @return 원문 칸을 고쳤으면 true 입니다(다른 행과 같이 편집으로 알린다).
             */
            static bool drawUnreadableValue( const utf8* pLabel, MaterialProperty& prop )
            {
                ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) ), "!" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "ThisValueDoesNotMatchItsType" ) );
                ImGui::SameLine();
                return EditorWidgets::drawTextField( pLabel, prop._value );
            }

            /**
             * @brief 머티리얼 프로퍼티 하나를 타입에 맞는 위젯으로 그립니다. 바뀌었으면 true 입니다.
             * @details 경우마다 짧은 `switch` 디스패처라 나누지 않습니다(`LongFunctions` 의 유지 기준). 타입을 더하면 `switch` 에 경우가 없다는 경고가 서므로 빠뜨릴 수 없습니다.
             */
            static bool drawTypedProperty( MaterialProperty& prop )
            {
                const string                            label = EditorDisplayNameUtil::chooseLabel( prop._displayName, prop._name ); // 인스펙터와 같은 이름표 규칙
                fixed_string<constant::buffer::kMax256> id;
                formatstring( id.data(), id.capacity(), "##%#", label );
                const utf8*          pLabel   = label.c_str();
                const EditorWidgetID widgetID = EditorWidgetID::fromString( id.c_str() );
                const utf8*          pID      = id.c_str(); // 이름표는 왼쪽 열(`drawPropertyRowLabel`)에 둔다 — 셰이더 · 텍스처 슬롯과 같은 쪽이다(예전에는 값 오른쪽에 붙었다)
                if ( prop._value.empty() )
                    prop._value = prop._defaultValue;

                switch ( prop._type )
                {
                    case MaterialPropertyKind::Bool:
                    {
                        bool bVal{ false };
                        if ( StringUtil::tryParseBool( prop._value, bVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        if ( ImGui::Checkbox( pID, &bVal ) )
                        {
                            prop._value = bVal ? "true" : "false";
                            return true;
                        }
                        return false;
                    }
                    case MaterialPropertyKind::Float:
                    case MaterialPropertyKind::Range:
                    {
                        float32 arrVal[1]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        bool bChanged{ false };
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        if ( prop._type == MaterialPropertyKind::Range && prop._min < prop._max )
                            bChanged = EditorWidgets::sliderFloat( widgetID, &arrVal[0], prop._min, prop._max );
                        else
                            bChanged = EditorWidgets::dragFloat( widgetID, &arrVal[0], 0.01f );
                        if ( bChanged )
                            MaterialViewModel::writeFloats( prop._value, arrVal, 1 );
                        return bChanged;
                    }
                    case MaterialPropertyKind::Float2:
                    {
                        float32 arrVal[2]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        const bool bChanged = EditorWidgets::dragFloat2( widgetID, arrVal, 0.01f );
                        if ( bChanged )
                            MaterialViewModel::writeFloats( prop._value, arrVal, 2 );
                        return bChanged;
                    }
                    case MaterialPropertyKind::Float3:
                    {
                        float32 arrVal[3]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        const bool bChanged = EditorWidgets::dragFloat3( widgetID, arrVal, 0.01f );
                        if ( bChanged )
                            MaterialViewModel::writeFloats( prop._value, arrVal, 3 );
                        return bChanged;
                    }
                    case MaterialPropertyKind::Color:
                    case MaterialPropertyKind::Float4:
                    {
                        float32    arrVal[4]{};
                        const bool bRead = prop._type == MaterialPropertyKind::Color ? MaterialPreviewInputs::parseColor( prop._value, arrVal )
                                                                                     : MaterialPreviewInputs::parseFloats( prop._value, arrVal );
                        if ( bRead == false )
                            return drawUnreadableValue( pLabel, prop );
                        Color      color{ arrVal[0], arrVal[1], arrVal[2], arrVal[3] };
                        const bool bChanged = EditorWidgets::drawColorEdit( pLabel, color );
                        if ( bChanged )
                        {
                            arrVal[0] = color._r;
                            arrVal[1] = color._g;
                            arrVal[2] = color._b;
                            arrVal[3] = color._a;
                            MaterialViewModel::writeFloats( prop._value, arrVal, 4 );
                        }
                        return bChanged;
                    }
                    case MaterialPropertyKind::Int:
                    {
                        int32 iVal{ 0 };
                        (void)StringUtil::parseInt( prop._value, iVal ); // 화면 표시 — 못 읽으면 0
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        const bool bChanged = EditorWidgets::dragInt( widgetID, &iVal );
                        if ( bChanged )
                            prop._value = to_string( iVal );
                        return bChanged;
                    }
                    case MaterialPropertyKind::Enum:
                    {
                        int32 selected{ 0 };
                        (void)StringUtil::parseInt( prop._value, selected ); // 화면 표시 — 못 읽으면 첫 항목
                        const utf8* pPreview = prop._value.c_str();
                        for ( const MaterialEnumEntry& entry : prop._listEnumEntry )
                        {
                            if ( static_cast<int32>( entry._value ) == selected )
                                pPreview = entry._name.c_str();
                        }
                        bool bChanged{ false };
                        if ( EditorWidgets::beginCombo( pLabel, pPreview ) )
                        {
                            for ( const MaterialEnumEntry& entry : prop._listEnumEntry )
                            {
                                const bool bSelected = ( static_cast<int32>( entry._value ) == selected );
                                if ( ImGui::Selectable( entry._name.c_str(), bSelected ) )
                                {
                                    prop._value = to_string( entry._value );
                                    bChanged    = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        return bChanged;
                    }
                    case MaterialPropertyKind::Texture2D:
                    case MaterialPropertyKind::TextureCube:
                    case MaterialPropertyKind::Texture3D:
                    case MaterialPropertyKind::Texture2DArray:
                    {
                        // 텍스처는 값 글이 아니라 에셋 칸 하나다 — 파일의 칸(`MaterialPropertyParams::_assetPath`)이 받는 쿠킹본(.dds)만 놓는다.
                        return EditorWidgets::drawAssetSlot( pLabel, prop._assetPath, Texture2D::kArrAssetSuffix[0]._text.data() );
                    }
                    case MaterialPropertyKind::Float4x4:
                    case MaterialPropertyKind::Uint:
                    case MaterialPropertyKind::Uint2:
                    case MaterialPropertyKind::Uint3:
                    case MaterialPropertyKind::Uint4:
                    case MaterialPropertyKind::Int2:
                    case MaterialPropertyKind::Int3:
                    case MaterialPropertyKind::Int4:
                    case MaterialPropertyKind::BitFlag:
                    case MaterialPropertyKind::ChannelMask:
                    case MaterialPropertyKind::Keyword:
                    case MaterialPropertyKind::Unknown:
                    {
                        return EditorWidgets::drawTextField( pLabel, prop._value );
                    }
                }
            }

            [[nodiscard]] static bool readPreviewRedMinusBlue( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = EditorPanelManager::findPanelInEditorContext<MaterialPanel>();
                float32              value{ 0.0f };
                const bool           bPreviewValueUnavailable = pPanel == nullptr || pPanel->isOpen() == false || pPanel->findPreviewRedMinusBlue( value ) == false;
                if ( bPreviewValueUnavailable )
                    return false;
                outValue = static_cast<float64>( value );
                return true;
            }

            /** @brief 열린 Material 패널입니다. 닫혔으면 nullptr 입니다. */
            [[nodiscard]] static const MaterialPanel* findOpenPanel()
            {
                return EditorPanelManager::findOpenPanelInEditorContext<MaterialPanel>();
            }

            /** @brief 패널이 지금 연 문서가 `gv_editorWatchAsset`(리소스 id)이면 1 입니다 — 문서 교체와 미리보기 계산을 나눠 보는 길입니다. */
            [[nodiscard]] static bool readDocumentIsWatchAsset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                return pPanel != nullptr && pPanel->readDocumentIsWatchAsset( outValue );
            }

            [[nodiscard]] static bool readSwitchPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->isWaitingForSwitchAnswer() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 기본값과 다른(되돌리기 단추가 붙는) 프로퍼티 수입니다. */
            [[nodiscard]] static bool readModifiedPropertyCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->countModifiedProperties() );
                return true;
            }

            [[nodiscard]] static bool readPreviewSceneFrames( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getPreviewViewport().getSubmittedFrameCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( MaterialPanel, MaterialPanel::kPanelID, EditorPanelCategory::Tool, 1300 );
    SW_AUTOMATION_WATCH( editorMaterialPreviewRedMinusBlue, "Editor.MaterialPreviewRedMinusBlue",
                         "Mean (R - B) of the Material panel's sphere preview in the last frame (0..255; red materials are high)",
                         &MaterialPanelInternal::readPreviewRedMinusBlue );
    SW_AUTOMATION_WATCH( editorMaterialDocumentIsWatchAsset, "Editor.MaterialDocumentIsWatchAsset",
                         "1 when the document open in the Material panel is gv_editorWatchAsset (tells a document switch that did not happen from a preview that did not follow)",
                         &MaterialPanelInternal::readDocumentIsWatchAsset );
    SW_AUTOMATION_WATCH( editorMaterialSwitchPending, "Editor.MaterialSwitchPending",
                         "1 while the Material panel waits for the 'unsaved changes, switch anyway?' answer - the new document is not opened until then",
                         &MaterialPanelInternal::readSwitchPending );
    SW_AUTOMATION_WATCH( editorMaterialPreviewSceneFrames, "Editor.MaterialPreviewSceneFrames",
                         "Frames the Material panel asked the renderer to draw its preview scene (0 until the panel shows a document)",
                         &MaterialPanelInternal::readPreviewSceneFrames );

    SW_AUTOMATION_WATCH( editorMaterialModifiedPropertyCount, "Editor.MaterialModifiedPropertyCount",
                         "Properties of the Material panel's document whose value differs from the default (each shows a reset button)",
                         &MaterialPanelInternal::readModifiedPropertyCount );

    MaterialPanel::MaterialPanel()
        : EditorPreviewPanel{ EditorAssetType{ Material::kAssetType }, false }
        , _model{}
    {
        _model.bindDocumentEditRecorder( getDocumentEditRecorder() ); // 문서 편집 알림은 모델이 낸다(감사 E-16)
    }

    void MaterialPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "material" );
        _model.beginFrame();

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMaterial", "OpenMaterialHint" ) );
            return;
        }

        // 문서 경로는 위의 문서 고르기 칸이 보인다 — 같은 경로를 한 줄 더 쓰지 않는다.
        // 글 단추 셋(다시 읽기 · 저장 · 선택에 적용)은 아이콘 단추 + 툴팁으로 줄인다(언리얼 머티리얼 에디터 툴바 · Godot 인스펙터 머리 줄과 같은 모양).
        if ( EditorCompactControls::drawIconButton( "##materialReload", SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EditorMaterial", "ReloadTip" ) ) )
            reloadDocument();
        EditorSelfTestMarks::record( "material.reload" );
        ImGui::SameLine();
        if ( EditorCompactControls::drawIconButton( "##materialSave", SW_EDITOR_ICON( "Save" ), SW_LOCKEY( "EditorMaterial", "SaveTip" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다(읽지 못한 문서는 막힌다) — 실패는 그것이 알린다
        ImGui::SameLine();
        if ( EditorCompactControls::drawIconButton( "##materialApply", SW_EDITOR_ICON( "Check" ), SW_LOCKEY( "EditorMaterial", "ApplyToSelectionTip" ) ) )
            applyLivePreview();
        drawPreview();
        // 렌더러가 그리는 미리보기 씬의 구 — 메인 뷰포트의 선택 오브젝트를 빌리지 않는다.
        syncPreviewSubjectMaterial();
        ImGui::SameLine();
        const float32 previewSide = MaterialPanelInternal::kPreviewSide * EditorThemeUtil::getDPIScale();
        drawPreviewViewport( float2{ previewSide * MaterialPanelInternal::kPreviewSceneAspect, previewSide }, false ); // 기즈모가 없다
        EditorSelfTestMarks::record( "material.previewScene" );

        fixed_string<constant::buffer::kMax128>& name = _model.editNameBuffer();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        EditorWidgets::inputText( SW_WIDGET_ID( "##name" ), name.data(), name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _model.commitName();
            applyLivePreview();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "NameTip" ) );
        string shaderPath = _model.getShaderPath();
        ImGui::BeginGroup(); // 슬롯은 항목 여럿이라 툴팁을 그룹 전체에 붙인다
        if ( EditorWidgets::drawAssetSlot( SW_LOCKEY( "EdWidget", "Shader" ), shaderPath, ".hlsl" ) )
        {
            _model.setShaderPath( shaderPath );
        }
        ImGui::EndGroup();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "ShaderTip" ) );

        ImGui::Separator();
        drawPropertyList();

        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void MaterialPanel::drawPropertyList()
    {
        // 머티리얼은 프로퍼티가 수십 개라 이름으로 찾는 칸을 둔다(언리얼 머티리얼 인스턴스 에디터 · 유니티 머티리얼 인스펙터의 검색과 같은 자리).
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Properties" ) );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##materialPropFilter", _propertyFilter, SW_LOCKEY( "EditorInspector", "SearchPropertiesHint" ), 0.0f, false, "material.propertyFilter" );

        const EditorListFilter          filter{ _propertyFilter.c_str() };
        const Material&                 material = _model.getMaterial();
        const vector<MaterialProperty>& listProp = material.getProperties();
        uint32                          shownCount{ 0 };
        for ( uint32 propIndex = 0; propIndex < static_cast<uint32>( listProp.size() ); ++propIndex )
        {
            const MaterialProperty& src = listProp[propIndex];
            if ( src._bHidden == SW_TRUE || filter.matchesAny( { src._name, src._displayName } ) == false )
                continue;
            // 위젯은 프로퍼티 사본을 고친다 — 사본에 쓰는 것은 모델 연산이다(되돌리기 이름 · 알림도 모델이 정한다, 감사 E-16)
            MaterialProperty        edited = src;
            MaterialProperty* const pProp  = &edited;
            ++shownCount;
            ImGui::PushID( static_cast<int32>( propIndex ) );
            // 값 칸 휠클릭 = 기본값으로(R10) — 되돌리기 단추와 같은 조건 · 같은 모델 연산이다.
            const bool bModified = MaterialPanelInternal::isModified( *pProp );
            EditorWidgets::beginResetToDefaultGroup();
            bool       bChanged      = MaterialPanelInternal::drawTypedProperty( edited );
            bool       bHovered      = false;
            const bool bResetByWheel = EditorWidgets::endResetToDefaultGroup( bModified, bHovered );
            EditorSelfTestMarks::recordFormat( "material.property.%s", pProp->_name.c_str() );
            // 기본값과 다른 행만 되돌리기 단추를 둔다 — 인스펙터와 같은 공용 단추라 모양 · 툴팁이 같다.
            if ( bModified )
            {
                const bool bResetPressed = EditorCompactControls::drawResetButton( "##reset", SW_LOCKEY( "EditorInspector", "ResetToDefaultTooltip" ) );
                if ( ( bResetPressed || bResetByWheel ) && _model.resetProperty( hashed_string( pProp->_name.c_str() ) ) )
                {
                    applyLivePreview();
                    bChanged = false; // 되돌린 값이 사본의 값이다 — 위젯이 고친 사본을 다시 쓰지 않는다
                }
                EditorSelfTestMarks::recordFormat( "material.reset.%s", pProp->_name.c_str() );
            }
            if ( bChanged && _model.setProperty( edited ) )
                applyLivePreview();
            ImGui::PopID();
        }
        if ( shownCount == 0 && filter.isActive() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
    }

    uint32 MaterialPanel::countModifiedProperties() const
    {
        uint32 count{ 0 };
        for ( const MaterialProperty& prop : _model.getMaterial().getProperties() )
        {
            if ( prop._bHidden != SW_TRUE && MaterialPanelInternal::isModified( prop ) )
                ++count;
        }
        return count;
    }

    void MaterialPanel::drawPreview()
    {
        // 구를 고리 · 조각으로 나눈 삼각형 부채에 꼭짓점마다 셈한 색을 칠한다(ImGui 가 꼭짓점 색을 보간한다).
        constexpr int32 kRingCount    = 20;
        constexpr int32 kSegmentCount = 48;

        const MaterialPreviewInputs inputs = MaterialPreviewInputs::readInputs( _model.getMaterial().getProperties() );
        const float32               side   = MaterialPanelInternal::kPreviewSide * EditorThemeUtil::getDPIScale();
        const ImVec2                minPos = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( side, side ) );
        EditorSelfTestMarks::record( "material.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "PreviewTip" ) );

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 radius    = side * 0.5f - 2.0f;
        const ImVec2  center( minPos.x + side * 0.5f, minPos.y + side * 0.5f );
        pDrawList->AddRectFilled( minPos, ImVec2( minPos.x + side, minPos.y + side ), EditorColorUtil::toImU32( MaterialPanelInternal::kPreviewBg ), 4.0f );

        auto toColor = [&inputs]( float32 nx, float32 ny )
        {
            const float3 color = MaterialPreviewInputs::shadePoint( inputs, nx, ny );
            return EditorColorUtil::toImU32( Color{ color._x, color._y, color._z, 1.0f } );
        };

        const ImVec2 whiteUV  = ImGui::GetFontTexUvWhitePixel();
        const int32  vtxCount = 1 + kRingCount * kSegmentCount;
        const int32  idxCount = 3 * kSegmentCount + 6 * kSegmentCount * ( kRingCount - 1 );
        pDrawList->PrimReserve( idxCount, vtxCount );
        const ImDrawIdx baseIndex = static_cast<ImDrawIdx>( pDrawList->_VtxCurrentIdx );
        pDrawList->PrimWriteVtx( center, whiteUV, toColor( 0.0f, 0.0f ) );
        for ( int32 ring = 1; ring <= kRingCount; ++ring )
        {
            // 가장자리일수록 촘촘하게 — 빛이 꺾이는 테두리가 매끈하다.
            const float32 ringRatio = MathUtil::sin( static_cast<float32>( ring ) / static_cast<float32>( kRingCount ) * MathUtil::kHalfPi ) * 0.999f;
            for ( int32 segment = 0; segment < kSegmentCount; ++segment )
            {
                const float32 angle = static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount ) * MathUtil::kTwoPi;
                const float32 nx    = MathUtil::cos( angle ) * ringRatio;
                const float32 ny    = MathUtil::sin( angle ) * ringRatio;
                pDrawList->PrimWriteVtx( ImVec2( center.x + nx * radius, center.y + ny * radius ), whiteUV, toColor( nx, ny ) );
            }
        }
        for ( int32 segment = 0; segment < kSegmentCount; ++segment )
        {
            const int32 next = ( segment + 1 ) % kSegmentCount;
            pDrawList->PrimWriteIdx( baseIndex );
            pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 1 + segment ) );
            pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 1 + next ) );
        }
        for ( int32 ring = 1; ring < kRingCount; ++ring )
        {
            const int32 inner = 1 + ( ring - 1 ) * kSegmentCount;
            const int32 outer = 1 + ring * kSegmentCount;
            for ( int32 segment = 0; segment < kSegmentCount; ++segment )
            {
                const int32 next = ( segment + 1 ) % kSegmentCount;
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + inner + segment ) );
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + outer + segment ) );
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + outer + next ) );
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + inner + segment ) );
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + outer + next ) );
                pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + inner + next ) );
            }
        }

        _model.recordPreviewDrawn( MaterialPreviewInputs::computeMeanRedMinusBlue( inputs ) );
    }

    void MaterialPanel::populatePreviewScene( Scene& scene )
    {
        _model.populateScene( scene );
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, 0.75f );
    }

    void MaterialPanel::syncPreviewSubjectMaterial()
    {
        if ( isDocumentLoadFailed() )
            return;
        _model.syncPreviewSubject( getPreviewScene(), getLoadedAssetPath() );
    }

    void MaterialPanel::applyLivePreview()
    {
        _model.applyLivePreview( getLoadedAssetPath() );
    }

    bool MaterialPanel::saveDocument()
    {
        if ( _model.saveDocument( getLoadedAssetPath() ) == false )
            return false;
        syncDocumentUndoBaseline();
        return true;
    }

    ToolAssetLoadResult MaterialPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        return _model.loadDocument( path );
    }

    string MaterialPanel::captureDocumentText() const
    {
        return _model.captureText();
    }

    void MaterialPanel::applyDocumentText( string_view text )
    {
        _model.applyText( text, getLoadedAssetPath() );
    }
} // namespace sw::editor
