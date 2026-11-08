#include "pch.h"

#include "Editor/Panels/Material/MaterialPanel.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Panels/Material/Model/MaterialPreviewInputs.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
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
                const string label  = EditorDisplayNameUtil::chooseLabel( prop._displayName, prop._name ); // 인스펙터와 같은 이름표 규칙
                const string id     = "##" + label;
                const utf8*  pLabel = label.c_str();
                const utf8*  pID    = id.c_str(); // 이름표는 왼쪽 열(`drawPropertyRowLabel`)에 둔다 — 셰이더 · 텍스처 슬롯과 같은 쪽이다(예전에는 값 오른쪽에 붙었다)
                if ( prop._value.empty() )
                    prop._value = prop._defaultValue;

                switch ( prop._type )
                {
                    case MaterialPropertyType::Bool:
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
                    case MaterialPropertyType::Float:
                    case MaterialPropertyType::Range:
                    {
                        float32 arrVal[1]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        bool bChanged{ false };
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        if ( prop._type == MaterialPropertyType::Range && prop._min < prop._max )
                            bChanged = ImGui::SliderFloat( pID, &arrVal[0], prop._min, prop._max );
                        else
                            bChanged = ImGui::DragFloat( pID, &arrVal[0], 0.01f );
                        if ( bChanged )
                            MaterialModel::writeFloats( prop._value, arrVal, 1 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float2:
                    {
                        float32 arrVal[2]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        const bool bChanged = ImGui::DragFloat2( pID, arrVal, 0.01f );
                        if ( bChanged )
                            MaterialModel::writeFloats( prop._value, arrVal, 2 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float3:
                    {
                        float32 arrVal[3]{};
                        if ( MaterialPreviewInputs::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        EditorWidgets::drawPropertyRowLabel( pLabel );
                        const bool bChanged = ImGui::DragFloat3( pID, arrVal, 0.01f );
                        if ( bChanged )
                            MaterialModel::writeFloats( prop._value, arrVal, 3 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Color:
                    case MaterialPropertyType::Float4:
                    {
                        float32    arrVal[4]{};
                        const bool bRead = prop._type == MaterialPropertyType::Color ? MaterialPreviewInputs::parseColor( prop._value, arrVal )
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
                            MaterialModel::writeFloats( prop._value, arrVal, 4 );
                        }
                        return bChanged;
                    }
                    case MaterialPropertyType::Int:
                    {
                        int32 iVal{ 0 };
                        (void)StringUtil::parseInt( prop._value, iVal ); // 화면 표시 — 못 읽으면 0
                        const bool bChanged = ImGui::DragInt( pLabel, &iVal );
                        if ( bChanged )
                            prop._value = to_string( iVal );
                        return bChanged;
                    }
                    case MaterialPropertyType::Enum:
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
                        if ( ImGui::BeginCombo( pLabel, pPreview ) )
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
                    case MaterialPropertyType::Texture2D:
                    case MaterialPropertyType::TextureCube:
                    case MaterialPropertyType::Texture3D:
                    case MaterialPropertyType::Texture2DArray:
                    {
                        string     path     = prop._assetPath.empty() ? prop._value : prop._assetPath;
                        const bool bChanged = EditorWidgets::drawAssetSlot( pLabel, path, ".png" );
                        if ( bChanged )
                        {
                            prop._assetPath = path;
                            prop._value     = path;
                        }
                        return bChanged;
                    }
                    case MaterialPropertyType::Float4x4:
                    case MaterialPropertyType::Uint:
                    case MaterialPropertyType::Uint2:
                    case MaterialPropertyType::Uint3:
                    case MaterialPropertyType::Uint4:
                    case MaterialPropertyType::Int2:
                    case MaterialPropertyType::Int3:
                    case MaterialPropertyType::Int4:
                    case MaterialPropertyType::BitFlag:
                    case MaterialPropertyType::ChannelMask:
                    case MaterialPropertyType::Keyword:
                    case MaterialPropertyType::Unknown:
                    {
                        return EditorWidgets::drawTextField( pLabel, prop._value );
                    }
                }
            }

            [[nodiscard]] static bool readPreviewRedMinusBlue( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return false;
                const MaterialPanel* pPanel = pPanels->findPanel<MaterialPanel>();
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
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const MaterialPanel* pPanel = pPanels->findPanel<MaterialPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
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

        fixed_string<constant::buffer::kMax128>& name = _model.editName();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "NameAlt" ) );
        ImGui::InputText( "##name", name.data(), name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _model.commitName();
            notifyDocumentEdited( "Edit Material Name", "material-name" );
            applyLivePreview();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "NameTip" ) );
        string shaderPath = _model.getShaderPath();
        ImGui::BeginGroup(); // 슬롯은 항목 여럿이라 툴팁을 그룹 전체에 붙인다
        if ( EditorWidgets::drawAssetSlot( SW_LOCKEY( "EdWidget", "Shader" ), shaderPath, ".hlsl" ) )
        {
            _model.setShaderPath( shaderPath );
            notifyDocumentEdited( "Edit Material Shader", "material-shader" );
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
        Material&                       material = _model.editMaterial();
        const vector<MaterialProperty>& listProp = material.getProperties();
        uint32                          shownCount{ 0 };
        for ( uint32 propIndex = 0; propIndex < static_cast<uint32>( listProp.size() ); ++propIndex )
        {
            const MaterialProperty& src = listProp[propIndex];
            if ( src._bHidden == SW_TRUE || filter.matchesAny( { src._name, src._displayName } ) == false )
                continue;
            MaterialProperty* pProp = material.findProperty( hashed_string( src._name.c_str() ) );
            if ( pProp == nullptr )
                continue;
            ++shownCount;
            ImGui::PushID( static_cast<int32>( propIndex ) );
            bool                                    bChanged = MaterialPanelInternal::drawTypedProperty( *pProp );
            fixed_string<constant::buffer::kMax128> propertyMark;
            formatstring( propertyMark.data(), propertyMark.capacity(), "material.property.%s", pProp->_name.c_str() );
            EditorSelfTestMarks::record( propertyMark.c_str() );
            // 기본값과 다른 행만 되돌리기 단추를 둔다 — 인스펙터와 같은 공용 단추라 모양 · 툴팁이 같다.
            if ( MaterialPanelInternal::isModified( *pProp ) )
            {
                if ( EditorCompactControls::drawResetButton( "##reset", SW_LOCKEY( "EditorInspector", "ResetToDefaultTooltip" ) ) )
                {
                    pProp->_assetPath.clear(); // 텍스처 칸은 경로가 값보다 앞선다 — 비워야 기본값 글이 보인다
                    bChanged = material.resetParameterToDefault( hashed_string( pProp->_name.c_str() ) ) || bChanged;
                }
                fixed_string<constant::buffer::kMax128> resetMark;
                formatstring( resetMark.data(), resetMark.capacity(), "material.reset.%s", pProp->_name.c_str() );
                EditorSelfTestMarks::record( resetMark.c_str() );
            }
            if ( bChanged )
            {
                fixed_string<constant::buffer::kMax128> key;
                formatstring( key.data(), key.capacity(), "material-prop:%s", pProp->_name.c_str() );
                notifyDocumentEdited( "Edit Material Property", key.c_str() );
                applyLivePreview();
            }
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
