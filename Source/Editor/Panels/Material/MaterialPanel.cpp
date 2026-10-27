#include "pch.h"

#include "Editor/Panels/Material/MaterialPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorDisplayNameUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Material/Model/MaterialPreviewShading.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MaterialPanelInternal
        {
            static constexpr Color kPreviewBg{ Color::fromBytes( 38, 38, 44, 255 ) };
            /// @brief 미리보기 한 변(96 DPI 기준 픽셀)입니다. CPU 구와 미리보기 씬 뷰포트의 높이가 같다.
            static constexpr float32 kPreviewSide = 160.0f;
            /// @brief 미리보기 씬 뷰포트의 가로 / 세로 비율입니다.
            static constexpr float32 kPreviewSceneAspect = 1.5f;
            /// @brief 미리보기 씬에 스폰하는 구 오브젝트 이름입니다.
            static constexpr const utf8* kPreviewSubjectName = "PreviewSubject";

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

            /**
             * @brief 값 글을 형식대로 읽지 못한 행입니다. 0 으로 메운 슬라이더 대신 경고 표시와 원문 칸을 보여 사용자가 글을 고치게 한다.
             * @return 원문 칸을 고쳤으면 true 입니다(다른 행과 같이 편집으로 알린다).
             */
            static bool drawUnreadableValue( const utf8* pLabel, MaterialProperty& prop )
            {
                ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getWarningColor() ), "!" );
                EditorWidgets::drawTooltip( "This value does not match its type (wrong count of numbers or a non-number) - fix the text" );
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
                const utf8*  pLabel = label.c_str();
                if ( prop._value.empty() )
                    prop._value = prop._defaultValue;

                switch ( prop._type )
                {
                    case MaterialPropertyType::Bool:
                    {
                        bool bVal{ false };
                        if ( StringUtil::tryParseBool( prop._value, bVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
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
                        float32 arrVal[1]{};
                        if ( MaterialPreviewShading::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        bool bChanged{ false };
                        if ( prop._type == MaterialPropertyType::Range && prop._min < prop._max )
                            bChanged = ImGui::SliderFloat( pLabel, &arrVal[0], prop._min, prop._max );
                        else
                            bChanged = ImGui::DragFloat( pLabel, &arrVal[0], 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, arrVal, 1 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float2:
                    {
                        float32 arrVal[2]{};
                        if ( MaterialPreviewShading::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        const bool bChanged = ImGui::DragFloat2( pLabel, arrVal, 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, arrVal, 2 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Float3:
                    {
                        float32 arrVal[3]{};
                        if ( MaterialPreviewShading::parseFloats( prop._value, arrVal ) == false )
                            return drawUnreadableValue( pLabel, prop );
                        const bool bChanged = ImGui::DragFloat3( pLabel, arrVal, 0.01f );
                        if ( bChanged )
                            writeFloats( prop._value, arrVal, 3 );
                        return bChanged;
                    }
                    case MaterialPropertyType::Color:
                    case MaterialPropertyType::Float4:
                    {
                        float32    arrVal[4]{};
                        const bool bRead = prop._type == MaterialPropertyType::Color ? MaterialPreviewShading::parseColor( prop._value, arrVal )
                                                                                     : MaterialPreviewShading::parseFloats( prop._value, arrVal );
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
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                const MaterialPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<MaterialPanel>();
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
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                const MaterialPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<MaterialPanel>();
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            /** @brief 패널이 지금 연 문서가 `gv_editorProbeAsset`(리소스 id)이면 1 입니다 — 문서 교체와 미리보기 계산을 나눠 보는 길입니다. */
            [[nodiscard]] static bool readDocumentIsProbeAsset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                return pPanel != nullptr && pPanel->readDocumentIsProbeAsset( outValue );
            }

            [[nodiscard]] static bool readSwitchPending( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MaterialPanel* pPanel = findOpenPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->isWaitingForSwitchAnswer() ? 1.0 : 0.0;
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
    SW_LOG_CALLER( "MaterialPanel" );
    SW_EDITOR_PANEL( MaterialPanel, MaterialPanel::kPanelID, EditorPanelCategory::Tool, 1300 );
    SW_AUTOMATION_PROBE( editorMaterialPreviewRedMinusBlue, "Editor.MaterialPreviewRedMinusBlue",
                         "Mean (R - B) of the Material panel's sphere preview in the last frame (0..255; red materials are high)",
                         &MaterialPanelInternal::readPreviewRedMinusBlue );
    SW_AUTOMATION_PROBE( editorMaterialDocumentIsProbeAsset, "Editor.MaterialDocumentIsProbeAsset",
                         "1 when the document open in the Material panel is gv_editorProbeAsset (tells a document switch that did not happen from a preview that did not follow)",
                         &MaterialPanelInternal::readDocumentIsProbeAsset );
    SW_AUTOMATION_PROBE( editorMaterialSwitchPending, "Editor.MaterialSwitchPending",
                         "1 while the Material panel waits for the 'unsaved changes, switch anyway?' answer - the new document is not opened until then",
                         &MaterialPanelInternal::readSwitchPending );
    SW_AUTOMATION_PROBE( editorMaterialPreviewSceneFrames, "Editor.MaterialPreviewSceneFrames",
                         "Frames the Material panel asked the renderer to draw its preview scene (0 until the panel shows a document)",
                         &MaterialPanelInternal::readPreviewSceneFrames );

    MaterialPanel::MaterialPanel()
        : EditorPreviewPanel{ EditorAssetType::Material, false }
        , _material{ Material::create() }
        , _name{}
        , _shaderPath{}
        , _status{}
        , _previewMaterialPath{}
        , _previewRedMinusBlue{ 0.0f }
        , _bPreviewDrawn{ false }
    {
    }

    void MaterialPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "material" );
        _bPreviewDrawn = false;

        if ( getLoadedAssetPath().empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMaterial", "OpenMaterialHint" ) );
            return;
        }

        ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
        if ( ImGui::Button( "Reload" ) )
            reloadDocument();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "ReloadTip" ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다(읽지 못한 문서는 막힌다) — 실패는 그것이 알린다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "SaveTip" ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Apply to Selection" ) )
            applyLivePreview();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "ApplyToSelectionTip" ) );
        drawPreview();
        // 렌더러가 그리는 미리보기 씬의 구 — 메인 뷰포트의 선택 오브젝트를 빌리지 않는다.
        syncPreviewSubjectMaterial();
        ImGui::SameLine();
        const float32 previewSide = MaterialPanelInternal::kPreviewSide * EditorThemeUtil::getDpiScale();
        drawPreviewViewport( float2{ previewSide * MaterialPanelInternal::kPreviewSceneAspect, previewSide }, false ); // 기즈모가 없다
        EditorSelfTestMarks::note( "material.previewScene" );

        ImGui::InputText( "Name", _name.data(), _name.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            _material->getDesc()._name = _name.c_str();
            notifyDocumentEdited( "Edit Material Name", "material-name" );
            applyLivePreview();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "NameTip" ) );
        string shaderPath = _shaderPath.c_str();
        ImGui::BeginGroup(); // 슬롯은 항목 여럿이라 툴팁을 그룹 전체에 붙인다
        if ( EditorWidgets::drawAssetSlot( "Shader", shaderPath, ".hlsl" ) )
        {
            _shaderPath = shaderPath;
            _material->setShaderPath( _shaderPath.c_str() );
            notifyDocumentEdited( "Edit Material Shader", "material-shader" );
        }
        ImGui::EndGroup();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "ShaderTip" ) );

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
        const float32               side   = MaterialPanelInternal::kPreviewSide * EditorThemeUtil::getDpiScale();
        const ImVec2                minPos = ImGui::GetCursorScreenPos();
        ImGui::Dummy( ImVec2( side, side ) );
        EditorSelfTestMarks::note( "material.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMaterial", "PreviewTip" ) );

        ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
        const float32 radius    = side * 0.5f - 2.0f;
        const ImVec2  center( minPos.x + side * 0.5f, minPos.y + side * 0.5f );
        pDrawList->AddRectFilled( minPos, ImVec2( minPos.x + side, minPos.y + side ), EditorColorUtil::toImU32( MaterialPanelInternal::kPreviewBg ), 4.0f );

        auto toColor = [&inputs]( float32 nx, float32 ny )
        {
            const float3 color = MaterialPreviewShading::shadePoint( inputs, nx, ny );
            return EditorColorUtil::toImU32( Color{ color._x, color._y, color._z, 1.0f } );
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

    void MaterialPanel::populatePreviewScene( Scene& scene )
    {
        GameObjectManager* pManager = scene.getObjectManager();
        GameObject*        pSubject = pManager != nullptr ? pManager->createGameObject( hashed_string( MaterialPanelInternal::kPreviewSubjectName ) ) : nullptr;
        MeshComponent*     pMesh    = pSubject != nullptr ? pSubject->addComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setMeshID( "Sphere" );
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, 0.75f );
        _previewMaterialPath.clear(); // 새 씬의 구에는 아직 머티리얼이 없다
    }

    MeshComponent* MaterialPanel::findPreviewSubject() const
    {
        Scene*             pScene   = getPreviewScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        GameObject*        pSubject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( MaterialPanelInternal::kPreviewSubjectName ) ) : nullptr;
        return pSubject != nullptr ? pSubject->getComponent<MeshComponent>() : nullptr;
    }

    void MaterialPanel::syncPreviewSubjectMaterial()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
        if ( bDocumentReady == false || _previewMaterialPath == getLoadedAssetPath() )
            return;
        MeshComponent* pMesh = findPreviewSubject();
        if ( pMesh == nullptr )
            return;
        // 캐시의 머티리얼을 바인딩한다 — 편집은 `applyLivePreview` 가 캐시 사본에 옮기므로 구도 따라온다.
        pMesh->setMaterialPath( getLoadedAssetPath() );
        _previewMaterialPath = getLoadedAssetPath();
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
            _status = SW_LOCKEY( "EditorMaterial", "SaveFailedStatus" );
            return false;
        }
        editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().reload( ResourceID{ getLoadedAssetPath() }, editor::getSubsystem<EditorRenderViews>().getRHIDevice() );
        applyLivePreview();
        _status = SW_LOCKEY( "EditorMaterial", "SavedStatus" );
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
        if ( _material->loadFromFile( ResourceID{ path } ) == false )
        {
            _status = SW_LOCKEY( "EditorMaterial", "LoadFailedStatus" );
            return ToolAssetLoadResult::Malformed;
        }
        syncNameBuffers();
        _status = SW_LOCKEY( "EditorMaterial", "LoadedStatus" );
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
