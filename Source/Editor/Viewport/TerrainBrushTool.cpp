#include "pch.h"

#include "Editor/Viewport/TerrainBrushTool.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/LayerWeightsAsset.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <cmath>
#include <imgui.h>

namespace sw::editor
{
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
            static constexpr uint32  kMaxResolution = 4097; ///< 해상도 고르기의 상한(4096 셀 + 1). 높이 배열이 1,600 만 샘플(64 MB)이다

            /**
             * @brief 드래그 하나의 되돌리기 자료입니다. 닿은 사각형 안의 편집 전 · 후 값만 담습니다(행 우선, 사각형 너비).
             * @details 되돌리기 · 다시 하기 대리자 둘이 같은 자료를 나눠 쥔다.
             */
            struct EditRecord
            {
                GameObjectHandle            _object{};
                TerrainSampleRect           _rect{};
                vector<float32>             _listHeightBefore;
                vector<float32>             _listHeightAfter;
                vector<SurfaceLayerWeights> _listWeightBefore;
                vector<SurfaceLayerWeights> _listWeightAfter;
                vector<uint8>               _listHoleBefore;
                vector<uint8>               _listHoleAfter;
                TerrainBrushTarget          _target{ TerrainBrushTarget::Height };
            };

            /**
             * @brief 크기 바꾸기 하나의 되돌리기 자료입니다. 사각형으로 담을 수 없어(배열 크기가 바뀐다) 전후 해상도의 높이 · 구멍 전체를 담는다.
             * @details 되돌리기와 다시 하기 모두 `TerrainComponent::replaceHeightfield` 한 길로 쓴다.
             */
            struct ResizeRecord
            {
                GameObjectHandle _object{};
                uint32           _resolutionBefore{ 0 };
                uint32           _resolutionAfter{ 0 };
                vector<float32>  _listHeightBefore;
                vector<float32>  _listHeightAfter;
                vector<uint8>    _listHoleBefore;
                vector<uint8>    _listHoleAfter;
            };

            /** @brief 진행 중인 드래그입니다. 사본은 지형 전체이고 `_rect` 는 드래그가 지금까지 닿은 사각형입니다(구멍은 셀 좌표). */
            struct Stroke
            {
                GameObjectHandle            _object{};
                TerrainSampleRect           _rect{};
                vector<float32>             _listHeightBefore;
                vector<float32>             _listHeight;
                vector<SurfaceLayerWeights> _listWeightBefore;
                vector<SurfaceLayerWeights> _listWeight;
                vector<uint8>               _listHoleBefore;
                vector<uint8>               _listHole;
                float32                     _flattenHeight{ 0.0f };
                TerrainBrushTarget          _target{ TerrainBrushTarget::Height };
            };

            /** @brief 레이 위 점 @p t 가 지형 위로 얼마나 떠 있는지 봅니다. 지형 밖이면 false 입니다. */
            [[nodiscard]] static bool findSignedHeight( const GameObjectManager& manager, const Ray& ray, float32 t, float32& outAbove )
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
            static bool traceTerrain( const GameObjectManager& manager, const Ray& ray, float3& outHit )
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

            /** @brief 브러시 격자입니다. 높이는 샘플 격자, 구멍은 셀 중심 격자, 칠하기는 스플랫 텍셀 격자(텍셀 중심이 지형 가장자리에 맞는다)입니다. */
            static bool makeGrid( const TerrainHeightfield& heightfield, TerrainBrushTarget target, TerrainBrushGrid& outGrid )
            {
                outGrid._origin = float2{ heightfield.getOrigin()._x, heightfield.getOrigin()._z };
                if ( target != TerrainBrushTarget::LayerWeight )
                {
                    outGrid._cellSize   = heightfield.getCellSize();
                    outGrid._resolution = heightfield.getResolution();
                    if ( target == TerrainBrushTarget::Hole )
                        outGrid = TerrainBrush::makeHoleGrid( outGrid );
                    return true;
                }
                // 브러시는 정사각형 스플랫만 다룬다.
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

            /**
             * @brief 사각형 값 @p listRect 를 전체 배열 @p inoutListValue(너비 @p resolution)의 같은 위치에 넣습니다.
             * @return 사각형이 배열을 벗어나거나 값 수가 맞지 않으면(크기를 바꾼 지형) false 이고 아무것도 바꾸지 않습니다.
             */
            template <typename T>
            [[nodiscard]] static bool pasteRect( const vector<T>& listRect, uint32 resolution, const TerrainSampleRect& rect, vector<T>& inoutListValue )
            {
                const bool bInside   = rect.isEmpty() == false && rect._maxX < resolution && inoutListValue.size() >= static_cast<size_t>( rect._maxZ + 1 ) * resolution;
                const bool bCountFit = listRect.size() == static_cast<size_t>( rect._maxX - rect._minX + 1 ) * ( rect._maxZ - rect._minZ + 1 );
                if ( bInside == false || bCountFit == false )
                    return false;
                size_t index{ 0 };
                for ( uint32 z = rect._minZ; z <= rect._maxZ; ++z )
                {
                    for ( uint32 x = rect._minX; x <= rect._maxX; ++x )
                    {
                        inoutListValue[static_cast<size_t>( z ) * resolution + x] = listRect[index++];
                    }
                }
                return true;
            }

            /** @brief 지금 높이를 `.heightfield` 형식으로 절대 경로에 씁니다(높이는 지형의 높이 범위로 양자화). 높이장이 없으면 false 입니다. */
            [[nodiscard]] static bool saveHeightfield( const TerrainComponent& terrain, string_view filePath )
            {
                const TerrainHeightfield& heightfield = terrain.getHeightfield();
                if ( heightfield.isValid() == false )
                    return false;
                HeightfieldAsset data;
                heightfield.makeHeightfieldAsset( terrain.getHeightMin(), terrain.getHeightMax(), data );
                const optional<AbsolutePath> heightfieldFile = FileUtil::requireAbsolutePath( filePath );
                return heightfieldFile.has_value() && data.saveToFile( *heightfieldFile );
            }

            /** @brief 지금 CPU 가중치를 `.layerweights` 형식(`LayerWeightsAsset`)으로 절대 경로에 씁니다. 가중치가 없으면 false 입니다. */
            [[nodiscard]] static bool saveSplat( const TerrainComponent& terrain, string_view filePath )
            {
                const TerrainHeightfield& heightfield = terrain.getHeightfield();
                if ( heightfield.getSplatWidth() == 0 )
                    return false;
                LayerWeightsAsset data;
                data._width                            = heightfield.getSplatWidth();
                data._height                           = heightfield.getSplatHeight();
                data._bytes                            = heightfield.getSplatBytes();
                const optional<AbsolutePath> splatFile = FileUtil::requireAbsolutePath( filePath );
                return splatFile.has_value() && data.saveToFile( *splatFile );
            }
        };

        /**
         * @class TerrainBrushToolState
         * @brief 브러시 도구의 상태와 그 상태를 바꾸는 일(드래그 · 크기 바꾸기 · 저장 · 설정 창)입니다. 에디터 범위의 상태 저장소가 소유한다(`get`).
         */
        class TerrainBrushToolState
        {
        public:
            TerrainBrushToolState();

            /** @brief 지금 에디터 범위의 브러시 도구 상태입니다. */
            static TerrainBrushToolState& get() { return getEditorState<TerrainBrushToolState>(); }

            /** @brief 브러시 원 · 설정 창을 그리고 드래그를 처리합니다(`TerrainBrushTool::process`). */
            void process( const EditorViewportVisualizerArgs& args )
            {
                _bHit              = SW_FALSE;
                _activeFrame       = ImGui::GetFrameCount();
                const bool bUsable = args._pObjectManager != nullptr && args._pViewProj != nullptr && args._bFlat2D == false;
                if ( bUsable == false )
                {
                    if ( _bStroking )
                        endStroke();
                    return;
                }

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bInsideX     = mouse.x >= args._canvasPos._x && mouse.x <= args._canvasPos._x + args._canvasSize._x;
                const bool   bInsideY     = mouse.y >= args._canvasPos._y && mouse.y <= args._canvasPos._y + args._canvasSize._y;
                const bool   bMouseInside = bInsideX && bInsideY && ImGui::IsWindowHovered();
                const bool   bEditable    = EditorPlaySession::areSceneEditsAllowed();

                Ray        ray;
                float3     hit{};
                const bool bRay = EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
                _bHit           = ( bRay && TerrainBrushToolInternal::traceTerrain( *args._pObjectManager, ray, hit ) ) ? SW_TRUE : SW_FALSE;
                if ( _bHit )
                {
                    const TerrainComponent* pHovered = TerrainComponent::findTerrainAt( *args._pObjectManager, hit._x, hit._z );
                    if ( pHovered != nullptr && pHovered->getOwner() != nullptr )
                        _lastHovered = pHovered->getOwner()->getHandle();
                }

                if ( _bHit )
                {
                    EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
                    const float4                color{ 1.0f, 0.85f, 0.2f, 1.0f };
                    TerrainBrushToolInternal::drawCircle( renderer, *args._pObjectManager, hit, _settings._radius, color );
                    const float32 inner = _settings._radius * ( 1.0f - MathUtil::saturate( _settings._falloff ) );
                    if ( inner > 0.0f )
                        TerrainBrushToolInternal::drawCircle( renderer, *args._pObjectManager, hit, inner, float4{ color._x, color._y, color._z, 0.5f } );
                }

                // 지형 위에 있거나 칠하는 중이면 마우스를 가져간다 — 클릭 선택과 기즈모가 물러난다.
                const bool bClaim = bEditable && ( _bStroking || ( bMouseInside && _bHit && args._bGizmoDragging == false ) );
                if ( bClaim )
                    EditorViewportMouseClaim::acquire( this );
                else
                    EditorViewportMouseClaim::release( this );

                // 칠하는 양은 세기 × 경과 시간입니다. 경과는 엔진 프레임의 실제 경과를 씁니다(씬 뷰 카메라와 같은 시계). ImGui 의 경과는 벽시계라
                // 고정 프레임 시간으로 도는 시나리오에서도 프레임 비용을 따라 바뀌고, 프레임이 빠른 Release 에서는 같은 드래그가 덜 칠합니다.
                const float32 deltaTime = editor::getSubsystem<GameTimeScale>().getUnscaledDeltaTime();
                if ( _bStroking )
                {
                    TerrainComponent* pTerrain   = TerrainBrushToolInternal::findTerrain( _stroke._object );
                    const bool        bStillDown = ImGui::IsMouseDown( ImGuiMouseButton_Left ) && bEditable && pTerrain != nullptr;
                    if ( bStillDown == false )
                        endStroke();
                    else if ( _bHit )
                        applyStroke( *pTerrain, hit, deltaTime );
                }
                else if ( bClaim && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                {
                    TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( *args._pObjectManager, hit._x, hit._z );
                    if ( pTerrain != nullptr && beginStroke( *pTerrain, hit ) )
                        applyStroke( *pTerrain, hit, deltaTime );
                }

                drawSettingsWindow( args );
            }

            TerrainBrushSettings& getSettings() { return _settings; }

            /** @brief 되돌리기 자료의 한쪽(@p bAfter)을 지형에 다시 씁니다. 지형이 사라졌으면 아무것도 하지 않습니다. */
            static void applyRecord( const TerrainBrushToolInternal::EditRecord& record, bool bAfter )
            {
                TerrainComponent* pTerrain = TerrainBrushToolInternal::findTerrain( record._object );
                if ( pTerrain == nullptr )
                    return;
                // 크기가 바뀐 지형(다시 읽은 에셋 · 크기 바꾸기 뒤)이면 사각형이 맞지 않아 쓰지 않는다.
                const TerrainHeightfield& heightfield = pTerrain->getHeightfield();
                bool                      bWritten    = false;
                switch ( record._target )
                {
                    case TerrainBrushTarget::LayerWeight:
                    {
                        vector<SurfaceLayerWeights> listWeight;
                        bWritten = heightfield.readSplatWeights( listWeight ) &&
                                   TerrainBrushToolInternal::pasteRect( bAfter ? record._listWeightAfter : record._listWeightBefore, heightfield.getSplatWidth(), record._rect, listWeight ) &&
                                   pTerrain->writeLayerWeights( listWeight, record._rect );
                        break;
                    }
                    case TerrainBrushTarget::Hole:
                    {
                        vector<uint8> listHole;
                        bWritten = heightfield.readHoleCells( listHole ) &&
                                   TerrainBrushToolInternal::pasteRect( bAfter ? record._listHoleAfter : record._listHoleBefore, heightfield.getResolution() - 1, record._rect, listHole ) &&
                                   pTerrain->writeHoles( listHole, record._rect );
                        break;
                    }
                    case TerrainBrushTarget::Height:
                    {
                        vector<float32> listHeight = heightfield.getHeightSamples();
                        bWritten                   = TerrainBrushToolInternal::pasteRect( bAfter ? record._listHeightAfter : record._listHeightBefore, heightfield.getResolution(), record._rect, listHeight ) &&
                                   pTerrain->writeHeights( listHeight, record._rect );
                        break;
                    }
                }
                if ( bWritten == false )
                {
                    SW_LOG_WARNING_ID( "TerrainBrushTool.TerrainEditNotUndone", "Terrain edit was not undone or redone - the terrain no longer has the edited size" );
                    return;
                }
                get()._bUnsaved = SW_TRUE;
            }

            /** @brief 크기 바꾸기 자료의 한쪽(@p bAfter)을 지형에 씁니다. 지형이 사라졌으면 아무것도 하지 않습니다. */
            static void applyResizeRecord( const TerrainBrushToolInternal::ResizeRecord& record, bool bAfter )
            {
                TerrainComponent* pTerrain = TerrainBrushToolInternal::findTerrain( record._object );
                if ( pTerrain == nullptr )
                    return;
                const bool bWritten = bAfter ? pTerrain->replaceHeightfield( record._resolutionAfter, record._listHeightAfter, record._listHoleAfter )
                                             : pTerrain->replaceHeightfield( record._resolutionBefore, record._listHeightBefore, record._listHoleBefore );
                if ( bWritten )
                    get()._bUnsaved = SW_TRUE;
            }

            [[nodiscard]] static bool readStrokeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( get()._strokeCount );
                return true;
            }

            [[nodiscard]] static bool readHit( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = get()._bHit ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readToolActive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = get()._activeFrame + 1 >= ImGui::GetFrameCount() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 크기 바꾸기 대상 지형의 높이장 해상도입니다(대상이 없으면 0). 시나리오가 `terrain.resize` 가 받아들여졌는지 읽는다. */
            [[nodiscard]] static bool readResolution( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const TerrainComponent* pTerrain = get().findResizeTarget();
                outValue                         = pTerrain != nullptr ? static_cast<float64>( pTerrain->getHeightfield().getResolution() ) : 0.0;
                return true;
            }

            /** @brief `terrain.brush <raise|lower|flatten|paint|smooth|hole|fill> [radius] [strength] [layer]` — 시나리오가 브러시를 고정 값으로 맞춥니다. */
            static bool runBrush( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 4 )
                    return false;
                static const utf8* const s_arrModeName[] = { "raise", "lower", "flatten", "paint", "smooth", "hole", "fill" };
                static_assert( static_cast<size_t>( IM_ARRAYSIZE( s_arrModeName ) ) == static_cast<size_t>( TerrainBrushMode::Count ), "Terrain brush command names follow TerrainBrushMode" );
                TerrainBrushSettings settings   = get()._settings;
                bool                 bKnownMode = false;
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
                const bool bLayerBad    = listArgument.size() > 3 && ( StringUtil::parseFloat( listArgument[3], layer ) == false || layer < 0.0f || layer >= static_cast<float32>( TerrainBrushToolInternal::kLayerCount ) );
                const bool bAnyBad      = bRadiusBad || bStrengthBad || bLayerBad;
                if ( bAnyBad )
                    return false;
                settings._layerIndex = static_cast<uint32>( layer );
                get()._settings      = settings;
                outReply             = "terrain brush " + listArgument[0];
                return true;
            }

            /** @brief `terrain.resize <resolution>` — 크기 바꾸기 대상 지형(`findResizeTarget`)을 새 해상도로 다시 표본화합니다(되돌리기 하나를 남긴다). */
            static bool runResize( const vector<string>& listArgument, string& outReply )
            {
                float32    value{ 0.0f };
                const bool bParsed = listArgument.size() == 1 && StringUtil::parseFloat( listArgument[0], value ) && value >= 2.0f;
                if ( bParsed == false )
                    return false;
                TerrainBrushToolState& state    = get();
                TerrainComponent*      pTerrain = state.findResizeTarget();
                if ( pTerrain == nullptr )
                {
                    outReply = "no terrain to resize (brush or hover a terrain first)";
                    return false;
                }
                const uint32 resolution = static_cast<uint32>( value );
                if ( state.resizeTerrain( *pTerrain, resolution ) == false )
                {
                    outReply = "terrain resize to " + listArgument[0] + " failed (resolution - 1 must be a multiple of the chunk cells)";
                    return false;
                }
                state._resizeResolution = resolution;
                outReply                = "terrain resized to " + listArgument[0];
                return true;
            }

        private:
            /** @brief 지형 @p terrain 을 해상도 @p resolution 으로 다시 표본화하고 되돌리기 하나(`Resize Terrain`)를 남깁니다. */
            bool resizeTerrain( TerrainComponent& terrain, uint32 resolution )
            {
                EditorTransaction                                  transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
                const TerrainHeightfield&                          heightfield = terrain.getHeightfield();
                shared_ptr<TerrainBrushToolInternal::ResizeRecord> record      = make_shared<TerrainBrushToolInternal::ResizeRecord>();
                record->_object                                                = terrain.getOwner()->getHandle();
                record->_resolutionBefore                                      = heightfield.getResolution();
                record->_listHeightBefore                                      = heightfield.getHeightSamples();
                record->_listHoleBefore                                        = heightfield.getHoleCells();
                if ( terrain.resampleHeightfield( resolution ) == false )
                    return false;
                record->_resolutionAfter = heightfield.getResolution();
                record->_listHeightAfter = heightfield.getHeightSamples();
                record->_listHoleAfter   = heightfield.getHoleCells();
                transaction.push( SW_UNDO_STEP( TerrainBrushToolState::applyResizeRecord, *record, false ),
                                  SW_UNDO_STEP( TerrainBrushToolState::applyResizeRecord, *record, true ), "Resize Terrain" );
                _lastEdited = record->_object;
                _bUnsaved   = SW_TRUE;
                return true;
            }

            /** @brief 드래그를 시작합니다. 지형 값 전부를 사본과 편집 전 사본으로 복사합니다. */
            bool beginStroke( TerrainComponent& terrain, const float3& hit )
            {
                const TerrainBrushTarget          target      = TerrainBrush::getTarget( _settings._mode );
                const TerrainHeightfield&         heightfield = terrain.getHeightfield();
                TerrainBrushToolInternal::Stroke& stroke      = _stroke;
                stroke                                        = TerrainBrushToolInternal::Stroke{};
                stroke._object                                = terrain.getOwner()->getHandle();
                stroke._target                                = target;
                stroke._flattenHeight                         = hit._y;
                switch ( target )
                {
                    case TerrainBrushTarget::LayerWeight:
                    {
                        if ( heightfield.readSplatWeights( stroke._listWeight ) == false )
                            return false;
                        stroke._listWeightBefore = stroke._listWeight;
                        break;
                    }
                    case TerrainBrushTarget::Hole:
                    {
                        if ( heightfield.readHoleCells( stroke._listHole ) == false )
                            return false;
                        stroke._listHoleBefore = stroke._listHole;
                        break;
                    }
                    case TerrainBrushTarget::Height:
                    {
                        stroke._listHeight       = heightfield.getHeightSamples();
                        stroke._listHeightBefore = stroke._listHeight;
                        break;
                    }
                }
                _bStroking = SW_TRUE;
                return true;
            }

            /** @brief 이번 프레임의 브러시를 사본에 적용하고 바뀐 사각형만 지형에 씁니다. */
            void applyStroke( TerrainComponent& terrain, const float3& hit, float32 deltaTime )
            {
                TerrainBrushToolInternal::Stroke& stroke = _stroke;
                TerrainBrushGrid                  grid{};
                if ( TerrainBrushToolInternal::makeGrid( terrain.getHeightfield(), stroke._target, grid ) == false )
                    return;
                TerrainBrush::collectSamples( float2{ hit._x, hit._z }, grid, _settings, _listSample );
                if ( _listSample.empty() )
                    return;
                TerrainSampleRect frameRect{};
                for ( const TerrainBrushSample& sample : _listSample )
                {
                    frameRect.include( sample._sampleX, sample._sampleZ );
                }
                bool bChanged = false;
                switch ( stroke._target )
                {
                    case TerrainBrushTarget::LayerWeight:
                    {
                        bChanged = TerrainBrush::applyLayerWeight( stroke._listWeight, grid._resolution, _listSample, _settings, deltaTime ) &&
                                   terrain.writeLayerWeights( stroke._listWeight, frameRect );
                        break;
                    }
                    case TerrainBrushTarget::Hole:
                    {
                        bChanged = TerrainBrush::applyHole( stroke._listHole, grid._resolution, _listSample, _settings ) && terrain.writeHoles( stroke._listHole, frameRect );
                        break;
                    }
                    case TerrainBrushTarget::Height:
                    {
                        bChanged = TerrainBrush::applyHeight( stroke._listHeight, grid._resolution, _listSample, _settings, stroke._flattenHeight, deltaTime ) &&
                                   terrain.writeHeights( stroke._listHeight, frameRect );
                        break;
                    }
                }
                if ( bChanged == false )
                    return;
                stroke._rect.include( frameRect._minX, frameRect._minZ );
                stroke._rect.include( frameRect._maxX, frameRect._maxZ );
                _lastEdited = stroke._object;
                _bUnsaved   = SW_TRUE;
            }

            /** @brief 드래그를 마칩니다. 바뀐 것이 있으면 닿은 사각형의 전 · 후 값으로 되돌리기 하나를 남깁니다. */
            void endStroke()
            {
                EditorTransaction                 transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
                TerrainBrushToolInternal::Stroke& stroke  = _stroke;
                const bool                        bRecord = stroke._rect.isEmpty() == false && TerrainBrushToolInternal::findTerrain( stroke._object ) != nullptr;
                if ( bRecord )
                {
                    shared_ptr<TerrainBrushToolInternal::EditRecord> record = make_shared<TerrainBrushToolInternal::EditRecord>();
                    record->_object                                         = stroke._object;
                    record->_rect                                           = stroke._rect;
                    record->_target                                         = stroke._target;
                    const TerrainHeightfield& hf                            = TerrainBrushToolInternal::findTerrain( stroke._object )->getHeightfield();
                    const utf8*               pName{ "Sculpt Terrain" };
                    switch ( stroke._target )
                    {
                        case TerrainBrushTarget::LayerWeight:
                        {
                            TerrainBrushToolInternal::extractRect( stroke._listWeightBefore, hf.getSplatWidth(), stroke._rect, record->_listWeightBefore );
                            TerrainBrushToolInternal::extractRect( stroke._listWeight, hf.getSplatWidth(), stroke._rect, record->_listWeightAfter );
                            pName = "Paint Terrain";
                            break;
                        }
                        case TerrainBrushTarget::Hole:
                        {
                            TerrainBrushToolInternal::extractRect( stroke._listHoleBefore, hf.getResolution() - 1, stroke._rect, record->_listHoleBefore );
                            TerrainBrushToolInternal::extractRect( stroke._listHole, hf.getResolution() - 1, stroke._rect, record->_listHoleAfter );
                            pName = "Paint Terrain Holes";
                            break;
                        }
                        case TerrainBrushTarget::Height:
                        {
                            TerrainBrushToolInternal::extractRect( stroke._listHeightBefore, hf.getResolution(), stroke._rect, record->_listHeightBefore );
                            TerrainBrushToolInternal::extractRect( stroke._listHeight, hf.getResolution(), stroke._rect, record->_listHeightAfter );
                            break;
                        }
                    }
                    transaction.push( SW_UNDO_STEP( TerrainBrushToolState::applyRecord, *record, false ),
                                      SW_UNDO_STEP( TerrainBrushToolState::applyRecord, *record, true ), pName );
                    ++_strokeCount;
                }
                stroke     = TerrainBrushToolInternal::Stroke{};
                _bStroking = SW_FALSE;
            }

            /** @brief 마지막으로 고친 지형의 높이장과 스플랫을 원래 에셋 경로에 씁니다. */
            void saveLastEdited()
            {
                TerrainComponent* pTerrain = TerrainBrushToolInternal::findTerrain( _lastEdited );
                if ( pTerrain == nullptr )
                    return;
                bool                         bSaved          = true;
                const optional<AbsolutePath> heightfieldPath = editor::getSubsystem<AssetPaths>().toDiskPath( pTerrain->getHeightfieldPath() ); // 쓰기다 - 읽기 해석이 아니라 쓸 자리
                if ( heightfieldPath.has_value() == false || TerrainBrushToolInternal::saveHeightfield( *pTerrain, heightfieldPath->str() ) == false )
                {
                    SW_LOG_ERROR_ID( "TerrainBrushTool.TerrainHeightfieldNotSaved", "Terrain heightfield '%#' was not saved", pTerrain->getHeightfieldPath().c_str() );
                    bSaved = false;
                }
                if ( pTerrain->getLayerWeightsPath().empty() == false )
                {
                    const optional<AbsolutePath> splatPath = editor::getSubsystem<AssetPaths>().toDiskPath( pTerrain->getLayerWeightsPath() );
                    if ( splatPath.has_value() == false || TerrainBrushToolInternal::saveSplat( *pTerrain, splatPath->str() ) == false )
                    {
                        SW_LOG_ERROR_ID( "TerrainBrushTool.TerrainSplatNotSaved", "Terrain splat '%#' was not saved", pTerrain->getLayerWeightsPath().c_str() );
                        bSaved = false;
                    }
                }
                if ( bSaved )
                {
                    _bUnsaved = SW_FALSE;
                    SW_LOG_INFO_ID( "TerrainBrushTool.TerrainSaved", "Terrain saved: %#", pTerrain->getHeightfieldPath().c_str() );
                }
            }

            /** @brief 크기 바꾸기 대상입니다. 마지막으로 고친 지형, 없으면 마지막으로 브러시가 닿은 지형입니다. */
            TerrainComponent* findResizeTarget() const
            {
                TerrainComponent* pEdited = TerrainBrushToolInternal::findTerrain( _lastEdited );
                return pEdited != nullptr ? pEdited : TerrainBrushToolInternal::findTerrain( _lastHovered );
            }

            /**
             * @brief 해상도 줄입니다. 대상 지형(`findResizeTarget`, 없으면 비활성)의 해상도를 고르고 `Resize` 로 다시 표본화합니다.
             * @details 고를 수 있는 해상도는 청크 칸의 배수 + 1 가운데 `TerrainComponent::isResolutionUsable` 이 받는 것뿐이다 — 나뉘지 않는 값을 쓸 수 없다.
             */
            void drawResizeControls()
            {
                TerrainComponent* pTerrain = findResizeTarget();
                ImGui::BeginDisabled( pTerrain == nullptr || _bStroking );
                const uint32 current = pTerrain != nullptr ? pTerrain->getHeightfield().getResolution() : 0u;
                if ( _resizeResolution == 0 || pTerrain == nullptr )
                    _resizeResolution = current;
                const string preview = _resizeResolution > 0 ? to_string( _resizeResolution ) : string{ "-" };
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Resolution" ), "Resolution" ), preview.c_str() ) )
                {
                    for ( uint32 resolution = 2; pTerrain != nullptr && resolution <= TerrainBrushToolInternal::kMaxResolution; ++resolution )
                    {
                        if ( pTerrain->isResolutionUsable( resolution ) == false )
                            continue;
                        const string label = to_string( resolution ) + ( resolution == current ? " (current)" : "" );
                        if ( ImGui::Selectable( label.c_str(), resolution == _resizeResolution ) )
                            _resizeResolution = resolution;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled( _resizeResolution == current );
                if ( ImGui::Button( SW_LOCKEY( "EdText", "Resize" ) ) && pTerrain != nullptr )
                    (void)resizeTerrain( *pTerrain, _resizeResolution ); // 실패는 지형이 알린다
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                    ImGui::SetTooltip( "%s", SW_LOCKEY( "EdTip", "TerrainResample" ) );
            }

            /** @brief 브러시 설정 창입니다(모드 · 반경 · 세기 · 감쇠 · 레이어 · 해상도 · 저장). */
            void drawSettingsWindow( const EditorViewportVisualizerArgs& args )
            {
                ImGui::SetNextWindowPos( ImVec2{ args._canvasPos._x + 12.0f, args._canvasPos._y + 48.0f }, ImGuiCond_FirstUseEver );
                if ( ImGui::Begin( "Terrain Brush###TerrainBrushTool", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    static const utf8* const s_arrModeName[] = { "Raise", "Lower", "Flatten", "Paint Layer", "Smooth", "Cut Hole", "Fill Hole" };
                    static_assert( static_cast<size_t>( IM_ARRAYSIZE( s_arrModeName ) ) == static_cast<size_t>( TerrainBrushMode::Count ), "Terrain brush mode names follow TerrainBrushMode" );
                    int32 mode = static_cast<int32>( _settings._mode );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Mode" ) );
                    if ( EditorWidgets::combo( "##Mode", &mode, s_arrModeName, IM_ARRAYSIZE( s_arrModeName ) ) )
                        _settings._mode = static_cast<TerrainBrushMode>( mode );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Radius" ) );
                    EditorWidgets::sliderFloat( "##Radius", &_settings._radius, 0.5f, 64.0f, "%.1f m" );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Strength" ) );
                    EditorWidgets::sliderFloat( "##Strength", &_settings._strength, 0.05f, 60.0f, "%.2f /s", ImGuiSliderFlags_Logarithmic );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Falloff" ) );
                    EditorWidgets::sliderFloat( "##Falloff", &_settings._falloff, 0.0f, 1.0f, "%.2f" );
                    if ( _settings._mode == TerrainBrushMode::PaintLayer )
                    {
                        int32 layer = static_cast<int32>( _settings._layerIndex );
                        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Layer" ) );
                        if ( EditorWidgets::sliderInt( "##Layer", &layer, 0, static_cast<int32>( TerrainBrushToolInternal::kLayerCount ) - 1 ) )
                            _settings._layerIndex = static_cast<uint32>( layer );
                    }
                    drawResizeControls();
                    ImGui::BeginDisabled( TerrainBrushToolInternal::findTerrain( _lastEdited ) == nullptr );
                    if ( ImGui::Button( _bUnsaved ? "Save Terrain *" : "Save Terrain" ) )
                        saveLastEdited();
                    ImGui::EndDisabled();
                    if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                        ImGui::SetTooltip( "%s", SW_LOCKEY( "EdTip", "TerrainWriteAssets" ) );
                }
                ImGui::End();
            }

        private:
            TerrainBrushSettings             _settings;
            TerrainBrushToolInternal::Stroke _stroke;
            vector<TerrainBrushSample>       _listSample; ///< 프레임마다 지우고 채우는 재사용 버퍼(UI 스레드 하나만 쓴다)
            GameObjectHandle                 _lastEdited;
            GameObjectHandle                 _lastHovered;      ///< 마지막으로 브러시 레이가 닿은 지형(크기 바꾸기 대상의 다음 후보)
            uint32                           _resizeResolution; ///< 설정 창이 고른 새 해상도(0 이면 지금 해상도를 보인다)
            int32                            _activeFrame;      ///< 도구가 마지막으로 돈 ImGui 프레임(감시 값 `Editor.TerrainBrushToolActive`)
            uint32                           _strokeCount;
            uint8                            _bStroking : 1;
            uint8                            _bUnsaved  : 1;
            uint8                            _bHit      : 1;
        };

        TerrainBrushToolState::TerrainBrushToolState()
            : _settings{}
            , _stroke{}
            , _listSample{}
            , _lastEdited{}
            , _lastHovered{}
            , _resizeResolution{ 0 }
            , _activeFrame{ -1 }
            , _strokeCount{ 0 }
            , _bStroking{ 0 }
            , _bUnsaved{ 0 }
            , _bHit{ 0 }
        {
        }

        /** @brief 붓질 기록을 되돌리기 단계 인자로 씁니다(`UndoStep` — 리로드를 넘는 데이터). */
        void writeUndoArg( Archive& archive, const TerrainBrushToolInternal::EditRecord& record )
        {
            editor::writeUndoArg( archive, record._object );
            editor::writeUndoArg( archive, record._rect );
            editor::writeUndoArg( archive, record._listHeightBefore );
            editor::writeUndoArg( archive, record._listHeightAfter );
            editor::writeUndoArg( archive, record._listWeightBefore );
            editor::writeUndoArg( archive, record._listWeightAfter );
            editor::writeUndoArg( archive, record._listHoleBefore );
            editor::writeUndoArg( archive, record._listHoleAfter );
            editor::writeUndoArg( archive, record._target );
        }

        [[nodiscard]] bool readUndoArg( Archive& archive, TerrainBrushToolInternal::EditRecord& outRecord )
        {
            return editor::readUndoArg( archive, outRecord._object ) && editor::readUndoArg( archive, outRecord._rect ) && editor::readUndoArg( archive, outRecord._listHeightBefore ) &&
                   editor::readUndoArg( archive, outRecord._listHeightAfter ) && editor::readUndoArg( archive, outRecord._listWeightBefore ) &&
                   editor::readUndoArg( archive, outRecord._listWeightAfter ) && editor::readUndoArg( archive, outRecord._listHoleBefore ) &&
                   editor::readUndoArg( archive, outRecord._listHoleAfter ) && editor::readUndoArg( archive, outRecord._target );
        }

        /** @brief 크기 바꾸기 기록을 되돌리기 단계 인자로 씁니다. */
        void writeUndoArg( Archive& archive, const TerrainBrushToolInternal::ResizeRecord& record )
        {
            editor::writeUndoArg( archive, record._object );
            editor::writeUndoArg( archive, record._resolutionBefore );
            editor::writeUndoArg( archive, record._resolutionAfter );
            editor::writeUndoArg( archive, record._listHeightBefore );
            editor::writeUndoArg( archive, record._listHeightAfter );
            editor::writeUndoArg( archive, record._listHoleBefore );
            editor::writeUndoArg( archive, record._listHoleAfter );
        }

        [[nodiscard]] bool readUndoArg( Archive& archive, TerrainBrushToolInternal::ResizeRecord& outRecord )
        {
            return editor::readUndoArg( archive, outRecord._object ) && editor::readUndoArg( archive, outRecord._resolutionBefore ) &&
                   editor::readUndoArg( archive, outRecord._resolutionAfter ) && editor::readUndoArg( archive, outRecord._listHeightBefore ) &&
                   editor::readUndoArg( archive, outRecord._listHeightAfter ) && editor::readUndoArg( archive, outRecord._listHoleBefore ) &&
                   editor::readUndoArg( archive, outRecord._listHoleAfter );
        }
    } // namespace

    TerrainBrushSettings& TerrainBrushTool::getSettings()
    {
        return TerrainBrushToolState::get().getSettings();
    }

    void TerrainBrushTool::process( const EditorViewportVisualizerArgs& args )
    {
        TerrainBrushToolState::get().process( args );
    }

    SW_AUTOMATION_WATCH( editorTerrainBrushStrokeCount, "Editor.TerrainBrushStrokeCount", "Terrain brush drags that finished and left an undo entry",
                         &TerrainBrushToolState::readStrokeCount );
    SW_AUTOMATION_WATCH( editorTerrainBrushHit, "Editor.TerrainBrushHit", "1 when the mouse ray hit a terrain in the last terrain tool frame",
                         &TerrainBrushToolState::readHit );
    SW_AUTOMATION_WATCH( editorTerrainBrushToolActive, "Editor.TerrainBrushToolActive", "1 when the terrain tool (scene view Terrain toggle) ran this or the last frame",
                         &TerrainBrushToolState::readToolActive );
    SW_AUTOMATION_WATCH( editorTerrainResolution, "Editor.TerrainResolution", "Height samples per side of the terrain the resize control targets (0 when there is none)",
                         &TerrainBrushToolState::readResolution );
    SW_DEV_COMMAND( TerrainBrush, "terrain.brush", "terrain.brush <raise|lower|flatten|paint|smooth|hole|fill> [radius] [strength] [layer]",
                    "Set the terrain brush of the scene view terrain tool", &TerrainBrushToolState::runBrush );
    SW_DEV_COMMAND( TerrainResize, "terrain.resize", "terrain.resize <resolution>", "Resample the last edited (or hovered) terrain to a new resolution, undoable",
                    &TerrainBrushToolState::runResize );
} // namespace sw::editor
