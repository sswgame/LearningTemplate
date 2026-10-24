#include "pch.h"

#include "Editor/Viewport/EditorViewportBillboard.h"

#include "Core/Math/MatrixMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorComponentIcon.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportBillboardInternal
        {
            static constexpr Color kBadgeBackground{ Color::fromBytes( 20, 22, 28, 190 ) };

            static constexpr float32 kDiameter = 26.0f; ///< 배지 지름(배율 1)

            /** @brief 컴포넌트의 월드 위치입니다. 씬 컴포넌트면 그것, 아니면 오브젝트의 주 씬 컴포넌트입니다. */
            static bool findWorldPosition( GameObject& object, Component& component, float3& outPosition )
            {
                const SceneComponent* pScene = isA<SceneComponent>( &component ) ? static_cast<const SceneComponent*>( &component ) : object.getPrimarySceneComponent();
                if ( pScene == nullptr )
                    return false;
                outPosition = pScene->getWorldPosition();
                return true;
            }

            /** @brief 수집이 컴포넌트마다 되풀이해 쓰는 값입니다. */
            struct CollectContext
            {
                const float4x4&        _viewProj;
                const float2&          _canvasPos;
                const float2&          _canvasSize;
                const CameraComponent* _pSkipCamera;
            };

            /** @brief 컴포넌트가 배지를 가질 수 있고 캔버스 안에 보이면 `outListItem` 에 더합니다. 오브젝트마다 하나만 둡니다. */
            static void addComponent( const CollectContext& context, Component* pComponent, vector<EditorViewportBillboardItem>& outListItem )
            {
                if ( pComponent == nullptr || pComponent == context._pSkipCamera )
                    return;
                GameObject* pObject           = pComponent->getOwner();
                const bool  bObjectNotVisible = pObject == nullptr || pObject->isPendingDestroy() || pObject->isActiveInHierarchy() == false;
                if ( bObjectNotVisible )
                    return;
                for ( const EditorViewportBillboardItem& item : outListItem )
                {
                    if ( item._pObject == pObject )
                        return; // 오브젝트마다 하나 — 빛과 카메라가 같이 있어도 배지가 겹치지 않게
                }
                const EditorComponentIconRow row = EditorComponentIcon::findRow( pComponent->getTypeInfo() );
                float3                       position{};
                ImVec2                       screen{};
                const bool                   bBillboardNotProjectable =
                    row._bBillboard == false || findWorldPosition( *pObject, *pComponent, position ) == false ||
                    EditorViewportProjectionUtil::projectPoint( context._viewProj, position, context._canvasPos, context._canvasSize, screen ) == false;
                if ( bBillboardNotProjectable )
                    return;
                const float2& canvasPos  = context._canvasPos;
                const float2& canvasSize = context._canvasSize;
                const bool    bInside =
                    screen.x >= canvasPos._x && screen.y >= canvasPos._y && screen.x <= canvasPos._x + canvasSize._x && screen.y <= canvasPos._y + canvasSize._y;
                if ( bInside )
                    outListItem.push_back( EditorViewportBillboardItem{
                        pObject, pComponent, row, float2{ screen.x, screen.y }
                    } );
            }

            /** @brief 지난 그리기에서 오브젝트마다 넘긴 글리프입니다(탐침이 그린 값을 본다 — 수집 결과가 아니라). */
            struct DrawnGlyph
            {
                const GameObject* _pObject{ nullptr };
                uint32            _codePoint{ 0 };
            };

            /** @brief 위 목록을 담은 상태입니다. 에디터 범위의 상태 저장소가 소유한다(UI 스레드 하나만 쓴다). */
            struct DrawnState
            {
                vector<DrawnGlyph> _list;
            };

            static vector<DrawnGlyph>& getDrawnList() { return getEditorState<DrawnState>()._list; }

            /** @brief 한 글자짜리 UTF-8 글의 코드 포인트입니다. 두 글자 이상이면 0 입니다. */
            static uint32 decodeSingleGlyph( const utf8* pGlyph )
            {
                const uint32 codePoint = EditorComponentIcon::decodeGlyph( pGlyph );
                if ( codePoint == 0 )
                    return 0;
                const size_t sequenceLength = codePoint < 0x80 ? 1u : ( codePoint < 0x800 ? 2u : 3u );
                return std::char_traits<utf8>::length( pGlyph ) == sequenceLength ? codePoint : 0u;
            }

            /** @brief 빌보드를 그립니다(배지 + 글리프, 이름 `viewport.billboard.<오브젝트 이름>`). */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.billboards" );
                const GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                vector<EditorViewportBillboardItem> listItem;
                EditorViewportBillboard::collect( *pManager, *args._pViewProj, args._canvasPos, args._canvasSize, args._pActiveCamera, listItem );
                const float32 radius   = EditorViewportBillboard::getDiameter() * 0.5f;
                ImFont*       pFont    = ImGui::GetFont();
                const float32 fontSize = radius * 1.15f;
                getDrawnList().clear();
                for ( const EditorViewportBillboardItem& item : listItem )
                {
                    getDrawnList().push_back( DrawnGlyph{ item._pObject, decodeSingleGlyph( item._row._pGlyph ) } );
                    const Color& color = item._row._color;
                    const ImVec2 center{ item._screen._x, item._screen._y };
                    args._pDrawList->AddCircleFilled( center, radius, EditorColorUtil::toImU32( EditorViewportBillboardInternal::kBadgeBackground ) );
                    args._pDrawList->AddCircle( center, radius, EditorColorUtil::toImU32( color.withAlpha( 0.9f ) ), 0, 1.5f );
                    const ImVec2 glyphSize = pFont->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, item._row._pGlyph );
                    args._pDrawList->AddText( pFont, fontSize, ImVec2{ center.x - glyphSize.x * 0.5f, center.y - glyphSize.y * 0.5f },
                                              EditorColorUtil::toImU32( color.withAlpha( 1.0f ) ), item._row._pGlyph );
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        // 상호작용 없는 항목으로 공간만 남긴다 — 캔버스의 누름(피킹)을 뺏지 않는다.
                        ImGui::ItemAdd( ImRect{
                                            ImVec2{center.x - radius, center.y - radius},
                                            ImVec2{center.x + radius, center.y + radius}
                        },
                                        0, nullptr, ImGuiItemFlags_NoNav );
                        fixed_string<constant::kMaxBuffer128> mark;
                        formatstring( mark.data(), mark.capacity(), "viewport.billboard.%#", item._pObject->getName().c_str() );
                        EditorSelfTestMarks::note( mark.c_str() );
                    }
                }
            }
        };
    } // namespace

    void EditorViewportBillboard::collect( const GameObjectManager& manager, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize,
                                           const CameraComponent* pSkipCamera, vector<EditorViewportBillboardItem>& outListItem )
    {
        outListItem.clear();
        const EditorViewportBillboardInternal::CollectContext context{ viewProj, canvasPos, canvasSize, pSkipCamera };
        // 씬 전체를 훑지 않고 레지스트리만 본다(오브젝트 수가 아니라 그 종류의 수만큼 든다).
        for ( CameraComponent* pCamera : manager.getCameraRegistry().getAll() )
        {
            EditorViewportBillboardInternal::addComponent( context, pCamera, outListItem );
        }
        const ComponentRegistry& registry = manager.getComponentRegistry();
        for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
        {
            for ( LightComponent* pLight : registry.getAll<LightComponent>( lightType ) )
            {
                EditorViewportBillboardInternal::addComponent( context, pLight, outListItem );
            }
        }
        for ( WindComponent* pWind : registry.getAll<WindComponent>() )
        {
            EditorViewportBillboardInternal::addComponent( context, pWind, outListItem );
        }
    }

    uint32 EditorViewportBillboard::findAt( const vector<EditorViewportBillboardItem>& listItem, const float2& point )
    {
        const float32 radius       = getDiameter() * 0.5f;
        uint32        bestIndex    = invalid_index::kUint32;
        float32       bestDistance = radius * radius;
        for ( size_t index = 0; index < listItem.size(); ++index )
        {
            const float32 distance = float2::getDistanceSquared( listItem[index]._screen, point );
            if ( distance > bestDistance )
                continue;
            bestDistance = distance;
            bestIndex    = static_cast<uint32>( index );
        }
        return bestIndex;
    }

    float32 EditorViewportBillboard::getDiameter()
    {
        return EditorViewportBillboardInternal::kDiameter * EditorThemeUtil::getDpiScale();
    }

    uint32 EditorViewportBillboard::findDrawnGlyph( const GameObject& object )
    {
        for ( const EditorViewportBillboardInternal::DrawnGlyph& drawn : EditorViewportBillboardInternal::getDrawnList() )
        {
            if ( drawn._pObject == &object )
                return drawn._codePoint;
        }
        return 0;
    }

    SW_EDITOR_VISUALIZER( Billboard, EditorViewportBillboard::kVisualizerID, 50, editoricon::kLightPoint, "Icons", "빛 · 카메라 · 오디오처럼 메시가 없는 컴포넌트를 아이콘으로 표시하고 눌러 고르게 합니다", true,
                          &EditorViewportBillboardInternal::draw );
} // namespace sw::editor
