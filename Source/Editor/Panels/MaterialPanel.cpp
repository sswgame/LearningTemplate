#include "pch.h"

#include "Editor/Panels/MaterialPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/MaterialPreviewShading.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Resource/AssetManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MaterialPanelInternal
        {
            static void writeFloats( string& out, const float32* pVal, uint32 count )
            {
                fixed_string<constant::kMaxBuffer128> buf;
                if ( count == 1 )
                    formatstring( buf.data(), buf.capacity(), "%g", pVal[0] );
                else if ( count == 2 )
                    formatstring( buf.data(), buf.capacity(), "%g,%g", pVal[0], pVal[1] );
                else if ( count == 3 )
                    formatstring( buf.data(), buf.capacity(), "%g,%g,%g", pVal[0], pVal[1], pVal[2] );
                else if ( count >= 4 )
                    formatstring( buf.data(), buf.capacity(), "%g,%g,%g,%g", pVal[0], pVal[1], pVal[2], pVal[3] );
                out = buf.c_str();
            }

            static bool drawTypedProperty( MaterialProperty& prop )
            {
                const utf8* pLabel = prop._displayName.empty() ? prop._name.c_str() : prop._displayName.c_str();
                if ( prop._value.empty() )
                    prop._value = prop._defaultValue;

                switch ( prop._type )
                {
                    case MaterialPropertyType::Bool:
                    {
                        bool bVal = StringUtil::parseBool( prop._value, false );
                        if ( ImGui::Checkbox( pLabel, &bVal ) )
                        {
                            prop._value = bVal ? "true" : "false";
                            return true;
                        }
                        return false;
                    }
                    case MaterialPropertyType::Float:
                    case MaterialPropertyType::Range:
                    {
                        float32 fVal{ 0.0f };
                        MaterialPreviewShading::parseFloats( prop._value, &fVal, 1 );
                        bool bChanged{ false };
                        if ( prop._type == MaterialPropertyType::Range && prop._min < prop._max )
                            bChanged = ImGui::SliderFloat( pLabel, &fVal, prop._min, prop._max );
                        else
                            bChanged = ImGui::DragFloat( pLabel, &fVal, 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, &fVal, 1 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float2:
                    {
                        float32 arrVal[2]{ 0.0f, 0.0f };
                        MaterialPreviewShading::parseFloats( prop._value, arrVal, 2 );
                        const bool bChanged = ImGui::DragFloat2( pLabel, arrVal, 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, arrVal, 2 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float3:
                    {
                        float32 arrVal[3]{ 0.0f, 0.0f, 0.0f };
                        MaterialPreviewShading::parseFloats( prop._value, arrVal, 3 );
                        const bool bChanged = ImGui::DragFloat3( pLabel, arrVal, 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, arrVal, 3 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Color:
                    case MaterialPropertyType::Float4:
                    {
                        float32 arrVal[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
                        MaterialPreviewShading::parseFloats( prop._value, arrVal, 4 );
                        Color4     color{ arrVal[0], arrVal[1], arrVal[2], arrVal[3] };
                        const bool bChanged = EditorWidgets::drawColorEdit( pLabel, color );
                        if ( bChanged )
                        {
                            arrVal[0] = color._r;
                            arrVal[1] = color._g;
                            arrVal[2] = color._b;
                            arrVal[3] = color._a;
                            writeFloats( prop._value, arrVal, 4 );
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
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                const MaterialPanel* pPanel = static_cast<const MaterialPanel*>( pContext->getPanelManager().findPanel( "material" ) );
                float32              value{ 0.0f };
                if ( pPanel == nullptr || pPanel->isOpen() == false || pPanel->findPreviewRedMinusBlue( value ) == false )
                    return false;
                outValue = static_cast<float64>( value );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "MaterialPanel" );
    SW_EDITOR_PANEL( MaterialPanel, "material", EditorPanelCategory::Tool, 1300 );
    SW_AUTOMATION_PROBE( editorMaterialPreviewRedMinusBlue, "Editor.MaterialPreviewRedMinusBlue",
                         "Mean (R - B) of the Material panel's sphere preview in the last frame (0..255; red materials are high)",
                         &MaterialPanelInternal::readPreviewRedMinusBlue );

    MaterialPanel::MaterialPanel()
        : EditorDocumentPanel{ EditorAssetType::Material, false }
        , _material{ Material::create() }
        , _name{}
        , _shaderPath{}
        , _status{}
        , _previewRedMinusBlue{ 0.0f }
        , _bPreviewDrawn{ false }
    {
    }

    void MaterialPanel::drawContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "material" );
        _bPreviewDrawn = false;

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( "Open a material (Open..., Quick Open or a Content Browser double-click) to edit." );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( "Reload" ) )
            reloadDocument();
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다(읽지 못한 문서는 막힌다) — 실패는 그것이 알린다
        ImGui::SameLine();
        if ( ImGui::Button( "Apply to Selection" ) )
            applyLivePreview();
        drawPreview();

        ImGui::InputText( "Name", _name.data(), _name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _material->getDesc()._name = _name.c_str();
            notifyDocumentEdited( "Edit Material Name", "material-name" );
            applyLivePreview();
        }
        string shaderPath = _shaderPath.c_str();
        if ( EditorWidgets::drawAssetSlot( "Shader", shaderPath, ".hlsl" ) )
        {
            _shaderPath = shaderPath;
            _material->setShaderPath( _shaderPath.c_str() );
            notifyDocumentEdited( "Edit Material Shader", "material-shader" );
        }

        ImGui::Separator();
        ImGui::TextUnformatted( "Properties" );
        const vector<MaterialProperty>& listProp = _material->getProperties();
        for ( uint32 propIndex = 0; propIndex < static_cast<uint32>( listProp.size() ); ++propIndex )
        {
            const MaterialProperty& src = listProp[propIndex];
            if ( src._bHidden == SW_TRUE )
                continue;
            MaterialProperty* pProp = _material->findProperty( hashed_string( src._name.c_str() ) );
            if ( pProp == nullptr )
                continue;
            ImGui::PushID( static_cast<int32>( propIndex ) );
            const bool bChanged = MaterialPanelInternal::drawTypedProperty( *pProp );
            if ( bChanged )
            {
                fixed_string<constant::kMaxBuffer128> key;
                formatstring( key.data(), key.capacity(), "material-prop:%s", pProp->_name.c_str() );
                notifyDocumentEdited( "Edit Material Property", key.c_str() );
                applyLivePreview();
            }
            ImGui::PopID();
        }

        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    void MaterialPanel::drawPreview()
    {
        // 구를 고리 · 조각으로 나눈 삼각형 부채에 꼭짓점마다 셈한 색을 칠한다(ImGui 가 꼭짓점 색을 보간한다).
        constexpr int32 kRingCount    = 20;
        constexpr int32 kSegmentCount = 48;

        const MaterialPreviewInputs inputs = MaterialPreviewShading::readInputs( _material->getProperties() );
        const float32               side   = 160.0f * EditorThemeUtil::getDpiScale();
        const ImVec2                minPos = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( side, side ) );
        EditorSelfTestMarks::note( "material.preview" );
        EditorWidgets::drawTooltip( "머티리얼 값(기본색 · 거칠기 · 금속성 · 방출)으로 셈한 미리보기입니다. 셰이더 코드는 반영하지 않습니다 — Apply to Selection 으로 씬에서 확인합니다" );

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 radius    = side * 0.5f - 2.0f;
        const ImVec2  center( minPos.x + side * 0.5f, minPos.y + side * 0.5f );
        pDrawList->AddRectFilled( minPos, ImVec2( minPos.x + side, minPos.y + side ), IM_COL32( 38, 38, 44, 255 ), 4.0f );

        auto toColor = [&inputs]( float32 nx, float32 ny )
        {
            const float3 color = MaterialPreviewShading::shadePoint( inputs, nx, ny );
            return ImGui::ColorConvertFloat4ToU32( ImVec4( color._x, color._y, color._z, 1.0f ) );
        };

        const ImVec2 whiteUv  = ImGui::GetFontTexUvWhitePixel();
        const int32  vtxCount = 1 + kRingCount * kSegmentCount;
        const int32  idxCount = 3 * kSegmentCount + 6 * kSegmentCount * ( kRingCount - 1 );
        pDrawList->PrimReserve( idxCount, vtxCount );
        const ImDrawIdx baseIndex = static_cast<ImDrawIdx>( pDrawList->_VtxCurrentIdx );
        pDrawList->PrimWriteVtx( center, whiteUv, toColor( 0.0f, 0.0f ) );
        for ( int32 ring = 1; ring <= kRingCount; ++ring )
        {
            // 가장자리일수록 촘촘하게 — 빛이 꺾이는 테두리가 매끈하다.
            const float32 ringRatio = MathUtil::sin( static_cast<float32>( ring ) / static_cast<float32>( kRingCount ) * MathUtil::kHalfPi ) * 0.999f;
            for ( int32 segment = 0; segment < kSegmentCount; ++segment )
            {
                const float32 angle = static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount ) * MathUtil::kTwoPi;
                const float32 nx    = MathUtil::cos( angle ) * ringRatio;
                const float32 ny    = MathUtil::sin( angle ) * ringRatio;
                pDrawList->PrimWriteVtx( ImVec2( center.x + nx * radius, center.y + ny * radius ), whiteUv, toColor( nx, ny ) );
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

        _previewRedMinusBlue = MaterialPreviewShading::computeMeanRedMinusBlue( inputs );
        _bPreviewDrawn       = true;
    }

    bool MaterialPanel::findPreviewRedMinusBlue( float32& outValue ) const
    {
        if ( _bPreviewDrawn == false )
            return false;
        outValue = _previewRedMinusBlue;
        return true;
    }

    void MaterialPanel::applyLivePreview()
    {
        EditorViewportPreview::applyMaterial( _material.get(), getLoadedAssetPath() );
    }

    bool MaterialPanel::saveDocument()
    {
        if ( getLoadedAssetPath().empty() )
            return false;
        _material->getDesc()._name = _name.c_str();
        _material->setShaderPath( _shaderPath.c_str() );
        if ( _material->saveToFile( getLoadedAssetPath() ) == false )
        {
            _status = "Save failed";
            return false;
        }
        AssetManager*  pResources = editor::getService<AssetManager>();
        EditorContext* pContext   = EditorContext::get();
        if ( pResources != nullptr && pContext != nullptr )
            pResources->getMaterialCache().reload( getLoadedAssetPath(), pContext->getRHIDevice() );
        applyLivePreview();
        _status = "Saved";
        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    ToolAssetLoadResult MaterialPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        if ( _material->loadFromFile( path ) == false )
        {
            _status = "Load failed - saving is disabled so the file is not overwritten";
            return ToolAssetLoadResult::Malformed;
        }
        syncNameBuffers();
        _status = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    void MaterialPanel::syncNameBuffers()
    {
        _name       = _material->getName().c_str();
        _shaderPath = _material->getShaderPath().c_str();
    }

    string MaterialPanel::captureDocumentText() const
    {
        return _material->saveToString();
    }

    void MaterialPanel::applyDocumentText( string_view text )
    {
        if ( text.empty() )
            return;
        if ( _material->loadFromXML( text ) == false )
            SW_LOG_WARNING( "Material undo snapshot could not be read - the material is left as it was" );
        syncNameBuffers();
        applyLivePreview();
    }
} // namespace sw::editor
