#include "pch.h"

#include "Editor/Viewport/TerrainBrushTool.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportMouseClaim.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportRayPick.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourcePaths.h"

#include <cmath>
#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "TerrainBrushTool" );

    namespace
    {
        struct TerrainBrushToolInternal
        {
            static constexpr float32 kMarchStep     = 0.5f; ///< 레이 전진 간격(m)
            static constexpr float32 kMarchDistance = 2000.0f;
            static constexpr uint32  kRefineSteps   = 10;
            static constexpr uint32  kCircleSegment = 48;
            static constexpr float32 kLift          = 0.05f; ///< 선이 지형에 묻히지 않게 띄우는 높이
            static constexpr uint32  kLayerCount    = TerrainComponent::kMaxLayerCount;

            /**
             * @brief 드래그 하나의 되돌리기 자료입니다. 닿은 사각형 안의 편집 전 · 후 값만 담습니다(행 우선, 사각형 너비).
             * @details 되돌리기 · 다시 하기 대리자 둘이 같은 자료를 나눠 쥔다.
             */
            struct EditRecord
            {
                GameObjectHandle  _object{};
                TerrainSampleRect _rect{};
                vector<float32>   _listHeightBefore;
                vector<float32>   _listHeightAfter;
                vector<float4>    _listWeightBefore;
                vector<float4>    _listWeightAfter;
                bool              _bPaint{ false };
            };

            /** @brief 진행 중인 드래그입니다. 사본은 지형 전체이고 `_rect` 는 드래그가 지금까지 닿은 사각형입니다. */
            struct Stroke
            {
                GameObjectHandle  _object{};
                TerrainSampleRect _rect{};
                vector<float32>   _listHeightBefore;
                vector<float32>   _listHeight;
                vector<float4>    _listWeightBefore;
                vector<float4>    _listWeight;
                float32           _flattenHeight{ 0.0f };
                bool              _bPaint{ false };
            };

            struct State
            {
                TerrainBrushSettings       _settings{};
                Stroke                     _stroke{};
                vector<TerrainBrushSample> _listSample; ///< 프레임마다 지우고 채우는 재사용 버퍼(UI 스레드 하나만 쓴다)
                GameObjectHandle           _lastEdited{};
                bool                       _bStroking{ false };
                bool                       _bUnsaved{ false };
                bool                       _bHit{ false };
                int32                      _activeFrame{ -1 }; ///< 도구가 마지막으로 돈 ImGui 프레임(탐침 `Editor.TerrainBrushToolActive`)
                uint32                     _strokeCount{ 0 };
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }

            /** @brief 레이 위 점 @p t 가 지형 위로 얼마나 떠 있는지 봅니다. 지형 밖이면 false 입니다. */
            static bool findSignedHeight( const GameObjectManager& manager, const EditorPickRay& ray, float32 t, float32& outAbove )
            {
                const float3      point    = ray._origin + ray._direction * t;
                TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, point._x, point._z );
                float32           height   = 0.0f;
                if ( pTerrain == nullptr || pTerrain->getHeightfield().findHeightAt( point._x, point._z, height ) == false )
                    return false;
                outAbove = point._y - height;
                return true;
            }

            /** @brief 레이를 전진시켜 처음 지형 아래로 들어가는 점을 찾고 이분법으로 다듬습니다. */
            static bool traceTerrain( const GameObjectManager& manager, const EditorPickRay& ray, float3& outHit )
            {
                float32 previousT     = 0.0f;
                float32 previousAbove = 0.0f;
                bool    bPrevious     = findSignedHeight( manager, ray, 0.0f, previousAbove );
                for ( float32 t = kMarchStep; t <= kMarchDistance; t += kMarchStep )
                {
                    float32 above = 0.0f;
                    if ( findSignedHeight( manager, ray, t, above ) == false )
                    {
                        bPrevious = false;
                        continue;
                    }
                    const bool bCrossed = above <= 0.0f && bPrevious && previousAbove > 0.0f;
                    if ( bCrossed )
                    {
                        float32 low  = previousT;
                        float32 high = t;
                        for ( uint32 step = 0; step < kRefineSteps; ++step )
                        {
                            const float32 middle      = ( low + high ) * 0.5f;
                            float32       middleAbove = 0.0f;
                            if ( findSignedHeight( manager, ray, middle, middleAbove ) && middleAbove > 0.0f )
                                low = middle;
                            else
                                high = middle;
                        }
                        outHit = ray._origin + ray._direction * high;
                        return true;
                    }
                    previousT     = t;
                    previousAbove = above;
                    bPrevious     = true;
                }
                return false;
            }

            /** @brief 지형 높이를 따라 반경 @p radius 의 원을 긋습니다. */
            static void drawCircle( EditorProjectedLineRenderer& renderer, const GameObjectManager& manager, const float3& center, float32 radius, const float4& color )
            {
                float3 previous{};
                for ( uint32 segment = 0; segment <= kCircleSegment; ++segment )
                {
                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( kCircleSegment );
                    float3        point{ center._x + std::cos( angle ) * radius, center._y, center._z + std::sin( angle ) * radius };
                    if ( const TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, point._x, point._z ) )
                    {
                        float32 height = 0.0f;
                        if ( pTerrain->getHeightfield().findHeightAt( point._x, point._z, height ) )
                            point._y = height;
                    }
                    point._y += kLift;
                    if ( segment > 0 )
                        renderer.drawLine( previous, point, color );
                    previous = point;
                }
            }

            /** @brief 오브젝트의 지형입니다. 오브젝트가 사라졌거나 지형이 비었으면 nullptr 입니다. */
            static TerrainComponent* findTerrain( GameObjectHandle handle )
            {
                GameObject*       pObject  = editor::findGameObject( handle );
                TerrainComponent* pTerrain = pObject != nullptr ? pObject->getComponent<TerrainComponent>() : nullptr;
                return pTerrain != nullptr && pTerrain->getHeightfield().isValid() ? pTerrain : nullptr;
            }

            /** @brief 브러시 격자입니다. 높이는 샘플 격자, 칠하기는 스플랫 텍셀 격자(텍셀 중심이 지형 가장자리에 맞는다)입니다. */
            static bool makeGrid( const TerrainHeightfield& heightfield, bool bPaint, TerrainBrushGrid& outGrid )
            {
                outGrid._origin = float2{ heightfield.getOrigin()._x, heightfield.getOrigin()._z };
                if ( bPaint == false )
                {
                    outGrid._cellSize   = heightfield.getCellSize();
                    outGrid._resolution = heightfield.getResolution();
                    return true;
                }
                // 브러시는 정사각형 스플랫만 다룬다(결정 기록 5-5).
                const uint32 splatWidth = heightfield.getSplatWidth();
                if ( splatWidth < 2 || splatWidth != heightfield.getSplatHeight() )
                    return false;
                const float32 cells = static_cast<float32>( splatWidth - 1 );
                outGrid._cellSize   = float2{ heightfield.getSize()._x / cells, heightfield.getSize()._y / cells };
                outGrid._resolution = splatWidth;
                return true;
            }

            /** @brief 사각형 @p rect 안의 값을 전체 배열 @p listSource(너비 @p resolution)에서 꺼냅니다. */
            template <typename T>
            static void extractRect( const vector<T>& listSource, uint32 resolution, const TerrainSampleRect& rect, vector<T>& outList )
            {
                outList.clear();
                for ( uint32 z = rect._minZ; z <= rect._maxZ; ++z )
                {
                    for ( uint32 x = rect._minX; x <= rect._maxX; ++x )
                    {
                        outList.push_back( listSource[static_cast<size_t>( z ) * resolution + x] );
                    }
                }
            }

            /** @brief 사각형 값 @p listRect 를 전체 배열 @p inoutListValue(너비 @p resolution)의 같은 자리에 넣습니다. */
            template <typename T>
            static void pasteRect( const vector<T>& listRect, uint32 resolution, const TerrainSampleRect& rect, vector<T>& inoutListValue )
            {
                size_t index{ 0 };
                for ( uint32 z = rect._minZ; z <= rect._maxZ; ++z )
                {
                    for ( uint32 x = rect._minX; x <= rect._maxX; ++x )
                    {
                        inoutListValue[static_cast<size_t>( z ) * resolution + x] = listRect[index++];
                    }
                }
            }

            /** @brief 되돌리기 자료의 한쪽(@p bAfter)을 지형에 다시 씁니다. 지형이 사라졌으면 아무것도 하지 않습니다. */
            static void applyRecord( const EditRecord& record, bool bAfter )
            {
                TerrainComponent* pTerrain = findTerrain( record._object );
                if ( pTerrain == nullptr )
                    return;
                const TerrainHeightfield& heightfield = pTerrain->getHeightfield();
                if ( record._bPaint )
                {
                    vector<float4> listWeight;
                    if ( heightfield.readSplatWeights( listWeight ) == false )
                        return;
                    pasteRect( bAfter ? record._listWeightAfter : record._listWeightBefore, heightfield.getSplatWidth(), record._rect, listWeight );
                    (void)pTerrain->writeLayerWeights( listWeight, record._rect ); // 크기가 바뀐 지형(다시 읽은 에셋)이면 거절된다
                }
                else
                {
                    vector<float32> listHeight = heightfield.getHeightSamples();
                    pasteRect( bAfter ? record._listHeightAfter : record._listHeightBefore, heightfield.getResolution(), record._rect, listHeight );
                    (void)pTerrain->writeHeights( listHeight, record._rect ); // 크기가 바뀐 지형이면 거절된다
                }
                getState()._bUnsaved = true;
            }

            /** @brief 드래그를 시작합니다. 지형 값 전부를 사본과 편집 전 사본으로 복사합니다. */
            static bool beginStroke( State& state, TerrainComponent& terrain, const float3& hit )
            {
                const bool                bPaint      = state._settings._mode == TerrainBrushMode::PaintLayer;
                const TerrainHeightfield& heightfield = terrain.getHeightfield();
                Stroke&                   stroke      = state._stroke;
                stroke                                = Stroke{};
                stroke._object                        = terrain.getOwner()->getHandle();
                stroke._bPaint                        = bPaint;
                stroke._flattenHeight                 = hit._y;
                if ( bPaint )
                {
                    if ( heightfield.readSplatWeights( stroke._listWeight ) == false )
                        return false;
                    stroke._listWeightBefore = stroke._listWeight;
                }
                else
                {
                    stroke._listHeight       = heightfield.getHeightSamples();
                    stroke._listHeightBefore = stroke._listHeight;
                }
                state._bStroking = true;
                return true;
            }

            /** @brief 이번 프레임의 브러시를 사본에 적용하고 바뀐 사각형만 지형에 씁니다. */
            static void applyStroke( State& state, TerrainComponent& terrain, const float3& hit, float32 deltaTime )
            {
                Stroke&          stroke = state._stroke;
                TerrainBrushGrid grid{};
                if ( makeGrid( terrain.getHeightfield(), stroke._bPaint, grid ) == false )
                    return;
                TerrainBrush::collectSamples( float2{ hit._x, hit._z }, grid, state._settings, state._listSample );
                if ( state._listSample.empty() )
                    return;
                TerrainSampleRect frameRect{};
                for ( const TerrainBrushSample& sample : state._listSample )
                {
                    frameRect.include( sample._sampleX, sample._sampleZ );
                }
                bool bChanged = false;
                if ( stroke._bPaint )
                {
                    bChanged = TerrainBrush::applyLayerWeight( stroke._listWeight, grid._resolution, state._listSample, state._settings, deltaTime ) &&
                               terrain.writeLayerWeights( stroke._listWeight, frameRect );
                }
                else
                {
                    bChanged = TerrainBrush::applyHeight( stroke._listHeight, grid._resolution, state._listSample, state._settings, stroke._flattenHeight, deltaTime ) &&
                               terrain.writeHeights( stroke._listHeight, frameRect );
                }
                if ( bChanged == false )
                    return;
                stroke._rect.include( frameRect._minX, frameRect._minZ );
                stroke._rect.include( frameRect._maxX, frameRect._maxZ );
                state._lastEdited = stroke._object;
                state._bUnsaved   = true;
            }

            /** @brief 드래그를 마칩니다. 바뀐 것이 있으면 닿은 사각형의 전 · 후 값으로 되돌리기 하나를 남깁니다. */
            static void endStroke( State& state )
            {
                Stroke&    stroke  = state._stroke;
                const bool bRecord = stroke._rect.isEmpty() == false && findTerrain( stroke._object ) != nullptr;
                if ( bRecord )
                {
                    shared_ptr<EditRecord> record = make_shared<EditRecord>();
                    record->_object               = stroke._object;
                    record->_rect                 = stroke._rect;
                    record->_bPaint               = stroke._bPaint;
                    const TerrainHeightfield& hf  = findTerrain( stroke._object )->getHeightfield();
                    if ( stroke._bPaint )
                    {
                        extractRect( stroke._listWeightBefore, hf.getSplatWidth(), stroke._rect, record->_listWeightBefore );
                        extractRect( stroke._listWeight, hf.getSplatWidth(), stroke._rect, record->_listWeightAfter );
                    }
                    else
                    {
                        extractRect( stroke._listHeightBefore, hf.getResolution(), stroke._rect, record->_listHeightBefore );
                        extractRect( stroke._listHeight, hf.getResolution(), stroke._rect, record->_listHeightAfter );
                    }
                    EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [record]()
                    { applyRecord( *record, false ); } ),
                                             SW_DELEGATE_LAMBDA( Delegate<void()>, [record]()
                    { applyRecord( *record, true ); } ),
                                             stroke._bPaint ? "Paint Terrain" : "Sculpt Terrain" );
                    ++state._strokeCount;
                }
                stroke           = Stroke{};
                state._bStroking = false;
            }

            /** @brief 마지막으로 고친 지형의 높이장과 스플랫을 원래 에셋 경로에 씁니다. */
            static void saveLastEdited( State& state )
            {
                TerrainComponent* pTerrain = findTerrain( state._lastEdited );
                if ( pTerrain == nullptr )
                    return;
                bool         bSaved          = true;
                const string heightfieldPath = ResourcePaths::getResourcePath( pTerrain->getHeightfieldPath() );
                if ( heightfieldPath.empty() || pTerrain->saveHeightfield( heightfieldPath ) == false )
                {
                    SW_LOG_ERROR( "Terrain heightfield '%#' was not saved", pTerrain->getHeightfieldPath().c_str() );
                    bSaved = false;
                }
                if ( pTerrain->getSplatTexturePath().empty() == false )
                {
                    const string splatPath = ResourcePaths::getResourcePath( pTerrain->getSplatTexturePath() );
                    if ( splatPath.empty() || pTerrain->saveSplat( splatPath ) == false )
                    {
                        SW_LOG_ERROR( "Terrain splat '%#' was not saved", pTerrain->getSplatTexturePath().c_str() );
                        bSaved = false;
                    }
                }
                if ( bSaved )
                {
                    state._bUnsaved = false;
                    SW_LOG_INFO( "Terrain saved: %#", pTerrain->getHeightfieldPath().c_str() );
                }
            }

            /** @brief 브러시 설정 창입니다(모드 · 반경 · 세기 · 감쇠 · 레이어 · 저장). */
            static void drawSettingsWindow( State& state, const EditorViewportVisualizerArgs& args )
            {
                ImGui::SetNextWindowPos( ImVec2{ args._canvasPos._x + 12.0f, args._canvasPos._y + 48.0f }, ImGuiCond_FirstUseEver );
                if ( ImGui::Begin( "Terrain Brush###TerrainBrushTool", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    static const utf8* const s_arrModeName[] = { "Raise", "Lower", "Flatten", "Paint Layer" };
                    int32                    mode            = static_cast<int32>( state._settings._mode );
                    if ( ImGui::Combo( "Mode", &mode, s_arrModeName, IM_ARRAYSIZE( s_arrModeName ) ) )
                        state._settings._mode = static_cast<TerrainBrushMode>( mode );
                    ImGui::SliderFloat( "Radius", &state._settings._radius, 0.5f, 64.0f, "%.1f m" );
                    ImGui::SliderFloat( "Strength", &state._settings._strength, 0.05f, 60.0f, "%.2f /s", ImGuiSliderFlags_Logarithmic );
                    ImGui::SliderFloat( "Falloff", &state._settings._falloff, 0.0f, 1.0f, "%.2f" );
                    if ( state._settings._mode == TerrainBrushMode::PaintLayer )
                    {
                        int32 layer = static_cast<int32>( state._settings._layerIndex );
                        if ( ImGui::SliderInt( "Layer", &layer, 0, static_cast<int32>( kLayerCount ) - 1 ) )
                            state._settings._layerIndex = static_cast<uint32>( layer );
                    }
                    ImGui::BeginDisabled( findTerrain( state._lastEdited ) == nullptr );
                    if ( ImGui::Button( state._bUnsaved ? "Save Terrain *" : "Save Terrain" ) )
                        saveLastEdited( state );
                    ImGui::EndDisabled();
                    if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                        ImGui::SetTooltip( "Write the heightfield and splat of the last edited terrain to their asset files" );
                }
                ImGui::End();
            }

            [[nodiscard]] static bool readStrokeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getState()._strokeCount );
                return true;
            }

            [[nodiscard]] static bool readHit( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = getState()._bHit ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readToolActive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = getState()._activeFrame + 1 >= ImGui::GetFrameCount() ? 1.0 : 0.0;
                return true;
            }

            /** @brief `terrain.brush <raise|lower|flatten|paint> [radius] [strength] [layer]` — 시나리오가 브러시를 고정 값으로 맞춥니다. */
            static bool runBrush( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 4 )
                    return false;
                static const utf8* const s_arrModeName[] = { "raise", "lower", "flatten", "paint" };
                TerrainBrushSettings     settings        = getState()._settings;
                bool                     bKnownMode      = false;
                for ( uint32 index = 0; index < IM_ARRAYSIZE( s_arrModeName ); ++index )
                {
                    if ( listArgument[0] == s_arrModeName[index] )
                    {
                        settings._mode = static_cast<TerrainBrushMode>( index );
                        bKnownMode     = true;
                    }
                }
                if ( bKnownMode == false )
                {
                    outReply = "unknown brush mode '" + listArgument[0] + "'";
                    return false;
                }
                const bool bRadiusBad   = listArgument.size() > 1 && StringUtil::parseFloat( listArgument[1], settings._radius ) == false;
                const bool bStrengthBad = listArgument.size() > 2 && StringUtil::parseFloat( listArgument[2], settings._strength ) == false;
                float32    layer        = static_cast<float32>( settings._layerIndex );
                const bool bLayerBad    = listArgument.size() > 3 && ( StringUtil::parseFloat( listArgument[3], layer ) == false || layer < 0.0f || layer >= static_cast<float32>( kLayerCount ) );
                const bool bAnyBad      = bRadiusBad || bStrengthBad || bLayerBad;
                if ( bAnyBad )
                    return false;
                settings._layerIndex = static_cast<uint32>( layer );
                getState()._settings = settings;
                outReply             = "terrain brush " + listArgument[0];
                return true;
            }
        };
    } // namespace

    TerrainBrushSettings& TerrainBrushTool::getSettings()
    {
        return TerrainBrushToolInternal::getState()._settings;
    }

    uint32 TerrainBrushTool::getStrokeCount()
    {
        return TerrainBrushToolInternal::getState()._strokeCount;
    }

    void TerrainBrushTool::process( const EditorViewportVisualizerArgs& args )
    {
        using Internal         = TerrainBrushToolInternal;
        Internal::State& state = Internal::getState();
        state._bHit            = false;
        state._activeFrame     = ImGui::GetFrameCount();
        const bool bUsable     = args._pObjectManager != nullptr && args._pViewProj != nullptr && args._bFlat2D == false;
        if ( bUsable == false )
        {
            if ( state._bStroking )
                Internal::endStroke( state );
            return;
        }

        const ImVec2 mouse        = ImGui::GetMousePos();
        const bool   bInsideX     = mouse.x >= args._canvasPos._x && mouse.x <= args._canvasPos._x + args._canvasSize._x;
        const bool   bInsideY     = mouse.y >= args._canvasPos._y && mouse.y <= args._canvasPos._y + args._canvasSize._y;
        const bool   bMouseInside = bInsideX && bInsideY && ImGui::IsWindowHovered();
        const bool   bEditable    = EditorPlaySession::areSceneEditsAllowed();

        EditorPickRay ray;
        float3        hit{};
        const bool    bRay = EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
        state._bHit        = bRay && Internal::traceTerrain( *args._pObjectManager, ray, hit );

        if ( state._bHit )
        {
            EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
            const float4                color{ 1.0f, 0.85f, 0.2f, 1.0f };
            Internal::drawCircle( renderer, *args._pObjectManager, hit, state._settings._radius, color );
            const float32 inner = state._settings._radius * ( 1.0f - MathUtil::saturate( state._settings._falloff ) );
            if ( inner > 0.0f )
                Internal::drawCircle( renderer, *args._pObjectManager, hit, inner, float4{ color._x, color._y, color._z, 0.5f } );
        }

        // 지형 위에 있거나 칠하는 중이면 마우스를 가져간다 — 클릭 선택과 기즈모가 물러난다.
        const bool bClaim = bEditable && ( state._bStroking || ( bMouseInside && state._bHit && args._bGizmoDragging == false ) );
        if ( bClaim )
            EditorViewportMouseClaim::acquire( &state );
        else
            EditorViewportMouseClaim::release( &state );

        if ( state._bStroking )
        {
            TerrainComponent* pTerrain   = Internal::findTerrain( state._stroke._object );
            const bool        bStillDown = ImGui::IsMouseDown( ImGuiMouseButton_Left ) && bEditable && pTerrain != nullptr;
            if ( bStillDown == false )
                Internal::endStroke( state );
            else if ( state._bHit )
                Internal::applyStroke( state, *pTerrain, hit, ImGui::GetIO().DeltaTime );
        }
        else if ( bClaim && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( *args._pObjectManager, hit._x, hit._z );
            if ( pTerrain != nullptr && Internal::beginStroke( state, *pTerrain, hit ) )
                Internal::applyStroke( state, *pTerrain, hit, ImGui::GetIO().DeltaTime );
        }

        Internal::drawSettingsWindow( state, args );
    }

    SW_AUTOMATION_PROBE( editorTerrainBrushStrokeCount, "Editor.TerrainBrushStrokeCount", "Terrain brush drags that finished and left an undo entry",
                         &TerrainBrushToolInternal::readStrokeCount );
    SW_AUTOMATION_PROBE( editorTerrainBrushHit, "Editor.TerrainBrushHit", "1 when the mouse ray hit a terrain in the last terrain tool frame",
                         &TerrainBrushToolInternal::readHit );
    SW_AUTOMATION_PROBE( editorTerrainBrushToolActive, "Editor.TerrainBrushToolActive", "1 when the terrain tool (scene view Terrain toggle) ran this or the last frame",
                         &TerrainBrushToolInternal::readToolActive );
    SW_DEV_COMMAND( TerrainBrush, "terrain.brush", "terrain.brush <raise|lower|flatten|paint> [radius] [strength] [layer]",
                    "Set the terrain brush of the scene view terrain tool", &TerrainBrushToolInternal::runBrush );
} // namespace sw::editor
