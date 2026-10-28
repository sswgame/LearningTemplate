/**
 * @file MeshModifiersVisualizer.cpp
 * @brief 뷰포트 시각화 meshmodifiers.preview(툴바 "Modifiers") — 변형 결과 선(CPU 미리보기), 고른 모디파이어의 기즈모 · 범위, 격자 제어점을 그리고 뷰포트에서 끌어 고칩니다.
 * @details 미리보기는 대상 메시의 정점 버퍼를 바꾸지 않고 선으로만 겹쳐 그린다(실시간 GPU 미리보기는 2 단계 — 컴퓨트 셰이더).
 *          편집은 콜라이더 손잡이(`EditorColliderHandleTool`)와 같은 방식이다: 그리기 목록에 손잡이를 직접 그리고, 마우스 레이를 만들어
 *          순수 계산(`ModifierGizmoLogic`)으로 손잡이를 집고 끈다. 끌기 하나가 되돌리기 하나다(`MeshModifierSession::recordUndo` 를 첫 변경 직전에 한 번).
 *          - 기즈모: 중심의 기준 축 화살표 셋(이동)과 기준 축 둘레 고리 셋(회전 — 고리 k 는 오일러 성분 k 를 바꾼다. Z 는 정확하고 X · Y 는 다른 성분이 0 일 때 정확하다).
 *          - 범위: 매개변수 정의의 힌트 · 이름으로 반경(구) · 하한/상한(주축 단면 사각형) · 중심(점) · 방향(화살표)을 그린다(`ModifierGizmoLogic::computeRangeKind`).
 *          - 제어점: 격자(lattice · ffd2 · ffd3 · ffd4)와 ffd2d 의 점을 클릭으로 고르고(세션의 `setSelectedControlPoint`), 점을 끌면 카메라를 마주 보는 평면에서,
 *            축 화살표를 끌면 그 축으로 옮긴다. 세션의 대칭 설정이 켜져 있으면 짝 점도 함께 옮긴다.
 *          meshmodifiers.stage(툴바 "Stage")는 고른 모디파이어까지만 건 결과를 다른 색으로 겹쳐 그린다(변형 영향 미리보기).
 *          마우스 양보(`EditorViewportMouseClaim`): 손잡이를 집는 순간 잡고 끄는 동안 매 프레임 다시 잡는다. 뷰포트는 시각화를 그린 뒤에 클릭 선택을 처리하므로 첫 클릭도 새지 않고,
 *          끄는 중에 시각화 토글을 꺼도 다음 프레임 시작에 양보가 거둬진다(`EditorViewportMouseClaim` 헤더의 프레임 순서 계약).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/ModifierGizmoLogic.h"

#include <cmath>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshModifiersVisualizerInternal
        {
            static constexpr ImU32       kPreviewColor       = IM_COL32( 120, 220, 255, 110 );
            static constexpr ImU32       kStageColor         = IM_COL32( 255, 120, 200, 130 );
            static constexpr ImU32       kAxisColor          = IM_COL32( 255, 200, 60, 255 );
            static constexpr ImU32       kHoverColor         = IM_COL32( 255, 255, 255, 255 );
            static constexpr ImU32       kRangeColor         = IM_COL32( 120, 255, 160, 200 );
            static constexpr ImU32       kLatticeLineColor   = IM_COL32( 255, 140, 40, 200 );
            static constexpr ImU32       kLatticePointColor  = IM_COL32( 255, 240, 120, 255 );
            static constexpr ImU32       kSelectedPointColor = IM_COL32( 255, 60, 60, 255 );
            static constexpr ImU32       kArrAxisColor[3]    = { IM_COL32( 235, 70, 70, 255 ), IM_COL32( 90, 220, 90, 255 ), IM_COL32( 80, 140, 255, 255 ) };
            static constexpr uint32      kRingSegmentCount   = 48;
            static constexpr float32     kPickWidthRatio     = 0.06f; ///< 손잡이 길이 대비 집는 두께
            static constexpr float32     kPointPickRatio     = 0.04f; ///< 원본 상자 대각선(월드) 대비 제어점 구 반경
            static constexpr float32     kGizmoSizeRatio     = 0.35f; ///< 원본 상자 대각선(월드) 대비 기즈모 손잡이 길이
            static constexpr float32     kPointHandleRatio   = 0.15f; ///< 원본 상자 대각선(월드) 대비 제어점 축 손잡이 길이
            static constexpr const utf8* kFFD2DTypeID        = "ffd2d";

            enum class DragKind : uint8
            {
                None,
                GizmoMove,   ///< 기즈모 중심을 기준 축 하나로
                GizmoRotate, ///< 기즈모 오일러 성분 하나를 고리로
                PointAxis,   ///< 고른 제어점을 모디파이어 축 하나로
                PointFree,   ///< 고른 제어점을 카메라를 마주 보는 평면에서
            };

            /** @brief 이번 프레임에 집을 수 있는 손잡이 하나입니다(월드). */
            struct Handle
            {
                DragKind _kind{ DragKind::None };
                uint32   _axis{ 0 };
                float3   _origin{ 0.0f, 0.0f, 0.0f };
                float3   _direction{ 0.0f, 0.0f, 0.0f }; ///< 이동은 끝점까지의 벡터, 회전은 고리 법선(단위)
                float32  _radius{ 0.0f };                ///< 회전 고리 반경
            };

            /** @brief 끌기 하나의 상태입니다. UI 스레드 하나만 씁니다. */
            struct DragState
            {
                Handle        _handle{};
                EditorPickRay _startRay{};
                float3        _startCenter{ 0.0f, 0.0f, 0.0f };   ///< 기즈모 중심(기준 공간)
                float3        _startRotation{ 0.0f, 0.0f, 0.0f }; ///< 기즈모 오일러 각(도)
                float3        _startVector{ 0.0f, 0.0f, 0.0f };   ///< 회전 고리 평면 위 시작 방향
                float3        _appliedDelta{ 0.0f, 0.0f, 0.0f };  ///< 제어점에 이미 건 정규 좌표 이동량
                float32       _startParameter{ 0.0f };
                int32         _modifierIndex{ -1 };
                int32         _pointIndex{ -1 };
                bool          _bDragging{ false };
                bool          _bUndoRecorded{ false };
            };

            /** @brief 고른 모디파이어 한 프레임의 공간 정보입니다. */
            struct Frame
            {
                MeshModifier*      _pModifier{ nullptr };
                float4x4           _modifierToWorld{};
                float4x4           _worldToModifier{};
                MeshModifierBounds _modifierBox{}; ///< 입력 메시 상자 모서리를 모디파이어 공간으로 옮긴 근사 상자
                float3             _centerWorld{ 0.0f, 0.0f, 0.0f };
                float32            _worldSize{ 1.0f }; ///< 원본 상자 대각선의 월드 길이
                uint32             _axisIndex{ 1 };
            };

            /** @brief 제어점 편집 대상입니다(격자 또는 ffd2d). */
            struct PointSet
            {
                LatticeModifier*   _pLattice{ nullptr };
                MeshModifier*      _pFFD2D{ nullptr };
                MeshModifierBounds _box{}; ///< 모디파이어 공간 격자 상자
                vector<float3>     _listWorld;
                int32              _countX{ 0 };
                int32              _countY{ 0 };
                int32              _countZ{ 1 };
                uint32             _offsetsParam{ 0 };

                bool isValid() const { return _pLattice != nullptr || _pFFD2D != nullptr; }
            };

            /**
             * @brief 이 확장의 에디터 범위 상태입니다(UI 스레드 하나만 쓴다). 이 확장이 기여한 서브시스템(`MeshModifiersVisualizerState`)이 소유한다 — 정의는 아래 서브시스템 옆에 있다.
             * @details 끌기 상태와, 프레임마다 다시 채우는 재사용 버퍼들이다. 전에는 버퍼마다 프로세스 정적 소유자(`ScopeOwned`)가 따로 있었다.
             */
            struct State
            {
                DragState      _drag{};
                vector<Handle> _listHandle;       ///< 이번 프레임에 집을 수 있는 손잡이
                vector<float3> _listSegmentPoint; ///< 변형 결과 미리보기 선분 끝점
                vector<float3> _listStagePoint;   ///< 고른 모디파이어까지의 결과 선분 끝점
                PointSet       _pointSet;         ///< 제어점 월드 목록
            };
            static State& getState();

            /** @brief 뷰포트 마우스 양보의 주인 식별자입니다(주소만 쓴다). 상태 객체의 주소다 — 에디터 범위마다 다르다. */
            static const void* getMouseClaimOwner() { return &getState(); }

            /** @brief 끌기를 끝냅니다. 핸들을 집고 있었으면 뷰포트 마우스 양보도 돌려준다(놓기 · 취소 모두). */
            static void endDrag( DragState& state )
            {
                if ( state._bDragging )
                    EditorViewportMouseClaim::release( getMouseClaimOwner() );
                state = DragState{};
            }

            static void drawSegment( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) )
                    args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
            }

            static void drawSegments( const EditorViewportVisualizerArgs& args, const vector<float3>& listSegmentPoint, ImU32 color )
            {
                for ( size_t index = 0; index + 1 < listSegmentPoint.size(); index += 2 )
                {
                    drawSegment( args, listSegmentPoint[index], listSegmentPoint[index + 1], color, 1.0f );
                }
            }

            /** @brief 화살표(선 + 화면 공간 머리)를 그립니다. */
            static void drawArrow( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) == false )
                    return;
                args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
                const float32 dx     = screenTo.x - screenFrom.x;
                const float32 dy     = screenTo.y - screenFrom.y;
                const float32 length = ::sqrtf( dx * dx + dy * dy );
                if ( length < 4.0f )
                    return;
                const float32 unitX = dx / length;
                const float32 unitY = dy / length;
                const float32 head  = MathUtil::min( 10.0f, length * 0.3f );
                const ImVec2  left{ screenTo.x - unitX * head - unitY * head * 0.5f, screenTo.y - unitY * head + unitX * head * 0.5f };
                const ImVec2  right{ screenTo.x - unitX * head + unitY * head * 0.5f, screenTo.y - unitY * head - unitX * head * 0.5f };
                args._pDrawList->AddTriangleFilled( screenTo, left, right, color );
            }

            /** @brief @p center 둘레 원을 @p axisU · @p axisV(단위, 서로 수직) 평면에 그립니다. */
            static void drawCircle( const EditorViewportVisualizerArgs& args, const float3& center, const float3& axisU, const float3& axisV, float32 radius, ImU32 color,
                                    float32 thickness )
            {
                float3 previous = center + axisU * radius;
                for ( uint32 segment = 1; segment <= kRingSegmentCount; ++segment )
                {
                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( kRingSegmentCount );
                    const float3  next  = center + axisU * ( ::cosf( angle ) * radius ) + axisV * ( ::sinf( angle ) * radius );
                    drawSegment( args, previous, next, color, thickness );
                    previous = next;
                }
            }

            static void drawMarker( const EditorViewportVisualizerArgs& args, const float3& point, ImU32 color )
            {
                ImVec2 screen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) == false )
                    return;
                args._pDrawList->AddLine( ImVec2{ screen.x - 6.0f, screen.y }, ImVec2{ screen.x + 6.0f, screen.y }, color, 2.0f );
                args._pDrawList->AddLine( ImVec2{ screen.x, screen.y - 6.0f }, ImVec2{ screen.x, screen.y + 6.0f }, color, 2.0f );
            }

            static float3 makeUnit( uint32 axisIndex )
            {
                float3 unit{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( unit, axisIndex, 1.0f );
                return unit;
            }

            /** @brief 기즈모 기준 공간의 축 @p axisIndex 를 월드 방향(기준 공간 길이 1 = 반환 길이)으로 옮깁니다. */
            static float3 computeBaseAxisWorld( const Frame& frame, uint32 axisIndex )
            {
                const float3 modifierDirection = frame._pModifier->getGizmo().toModifierDirection( makeUnit( axisIndex ) );
                return float3::transformVector( modifierDirection, frame._modifierToWorld );
            }

            static bool makeFrame( MeshModifierSession& session, MeshModifier& modifier, Frame& outFrame )
            {
                MeshModifierEvaluation evaluation{};
                evaluation._meshToWorld          = session.computeMeshToWorld();
                evaluation._pInputs              = &session;
                const float4x4 localToModifier   = modifier.computeLocalToModifier( evaluation );
                outFrame._pModifier              = &modifier;
                outFrame._modifierToWorld        = localToModifier.invert() * evaluation._meshToWorld;
                outFrame._worldToModifier        = outFrame._modifierToWorld.invert();
                outFrame._centerWorld            = float3::transform( float3{ 0.0f, 0.0f, 0.0f }, outFrame._modifierToWorld );
                outFrame._axisIndex              = MathUtil::min( modifier.getGizmo()._axisIndex, 2u );
                const MeshModifierBounds& source = session.getSourceBounds();
                if ( source.isValid() == false )
                    return false;
                // 상자는 입력 메시(앞선 켜진 모디파이어까지 건 결과)의 로컬 상자 모서리를 모디파이어 공간으로 옮겨 근사한다.
                // 손잡이 크기(_worldSize)는 끌기 중 흔들리지 않도록 원본 상자로 둔다
                const MeshModifierBounds& input = session.getSelectedModifierInputBounds();
                vector<float3>            listCorner;
                listCorner.reserve( 8 );
                for ( uint32 corner = 0; corner < 8; ++corner )
                {
                    const float3 local{ ( corner & 1u ) != 0 ? input._max._x : input._min._x, ( corner & 2u ) != 0 ? input._max._y : input._min._y,
                                        ( corner & 4u ) != 0 ? input._max._z : input._min._z };
                    listCorner.push_back( float3::transform( local, localToModifier ) );
                }
                outFrame._modifierBox = MeshModifierBounds::makeFromPoints( listCorner );
                const float3 worldMin = float3::transform( source._min, evaluation._meshToWorld );
                const float3 worldMax = float3::transform( source._max, evaluation._meshToWorld );
                outFrame._worldSize   = MathUtil::max( ( worldMax - worldMin ).getLength(), 0.1f );
                return true;
            }

            static float3 toWorld( const Frame& frame, const float3& modifierPoint )
            {
                return float3::transform( modifierPoint, frame._modifierToWorld );
            }

            static void drawRanges( const EditorViewportVisualizerArgs& args, const Frame& frame )
            {
                const MeshModifier& modifier = *frame._pModifier;
                const uint32        count    = modifier.getParamCount();
                float3              center{ 0.0f, 0.0f, 0.0f };
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( ModifierGizmoLogic::computeRangeKind( modifier.getParamDefs()[index] ) == ModifierRangeKind::Center )
                    {
                        center = modifier.getVector3( index );
                        break;
                    }
                }
                const float32  arrowLength = frame._worldSize * kGizmoSizeRatio;
                vector<float3> listCorner;
                for ( uint32 index = 0; index < count; ++index )
                {
                    const MeshModifierParamDef& def = modifier.getParamDefs()[index];
                    switch ( ModifierGizmoLogic::computeRangeKind( def ) )
                    {
                        case ModifierRangeKind::Radius:
                        {
                            const float32 radius = modifier.getFloat( index );
                            if ( radius <= MathUtil::kEpsilon )
                                break;
                            // 모디파이어 공간에서 세 평면의 원을 만들어 월드로 옮긴다(비균등 스케일이면 타원이 된다)
                            for ( uint32 plane = 0; plane < 3; ++plane )
                            {
                                const float3 axisU    = makeUnit( ( plane + 1 ) % 3 );
                                const float3 axisV    = makeUnit( ( plane + 2 ) % 3 );
                                float3       previous = toWorld( frame, center + axisU * radius );
                                for ( uint32 segment = 1; segment <= kRingSegmentCount; ++segment )
                                {
                                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( kRingSegmentCount );
                                    const float3  next  = toWorld( frame, center + axisU * ( ::cosf( angle ) * radius ) + axisV * ( ::sinf( angle ) * radius ) );
                                    drawSegment( args, previous, next, kRangeColor, 1.0f );
                                    previous = next;
                                }
                            }
                            break;
                        }
                        case ModifierRangeKind::LowerLimit:
                        case ModifierRangeKind::UpperLimit:
                        {
                            if ( ModifierGizmoLogic::tryMakeLimitQuad( frame._modifierBox, frame._axisIndex, modifier.getFloat( index ), listCorner ) == false )
                                break;
                            for ( size_t corner = 0; corner < listCorner.size(); ++corner )
                            {
                                drawSegment( args, toWorld( frame, listCorner[corner] ), toWorld( frame, listCorner[( corner + 1 ) % listCorner.size()] ), kRangeColor, 1.5f );
                            }
                            break;
                        }
                        case ModifierRangeKind::Center:
                        {
                            drawMarker( args, toWorld( frame, modifier.getVector3( index ) ), kRangeColor );
                            break;
                        }
                        case ModifierRangeKind::Direction:
                        {
                            const float3 modifierDirection = def._kind == MeshModifierParamKind::Vector3
                                                               ? MeshModifierUtil::normalizeOr( modifier.getVector3( index ), float3{ 0.0f, 0.0f, 0.0f } )
                                                               : ModifierGizmoLogic::computeDirectionVector( frame._axisIndex, modifier.getFloat( index ) );
                            const float3 worldFrom         = toWorld( frame, center );
                            const float3 worldStep         = float3::transformVector( modifierDirection, frame._modifierToWorld );
                            if ( worldStep.getLengthSquared() <= MathUtil::kEpsilonSquared )
                                break;
                            drawArrow( args, worldFrom, worldFrom + worldStep.normalize() * arrowLength, kRangeColor, 2.0f );
                            break;
                        }
                        case ModifierRangeKind::None:
                        {
                            break;
                        }
                    }
                }
            }

            /** @brief 고른 모디파이어가 격자 · ffd2d 면 제어점을 월드로 채웁니다. */
            static bool makePointSet( const Frame& frame, PointSet& outSet )
            {
                MeshModifier& modifier = *frame._pModifier;
                outSet._listWorld.clear();
                outSet._pLattice = LatticeModifier::findFrom( modifier );
                if ( outSet._pLattice != nullptr )
                {
                    const LatticeModifier& lattice = *outSet._pLattice;
                    outSet._box                    = lattice.getEffectiveBox( frame._modifierBox );
                    outSet._countX                 = lattice.getResolutionX();
                    outSet._countY                 = lattice.getResolutionY();
                    outSet._countZ                 = lattice.getResolutionZ();
                    const float3 size              = outSet._box.getSize();
                    outSet._listWorld.reserve( static_cast<size_t>( lattice.getControlPointCount() ) );
                    for ( int32 index = 0; index < lattice.getControlPointCount(); ++index )
                    {
                        const float3& normalized = lattice.getControlPoint( index );
                        const float3  local{ outSet._box._min._x + normalized._x * size._x, outSet._box._min._y + normalized._y * size._y,
                                            outSet._box._min._z + normalized._z * size._z };
                        outSet._listWorld.push_back( toWorld( frame, local ) );
                    }
                    return true;
                }
                if ( string_view( modifier.getTypeID() ) != string_view( kFFD2DTypeID ) )
                    return false;
                uint32     resolutionXParam  = 0;
                uint32     resolutionYParam  = 0;
                const bool bInvalidFFDParams = modifier.tryGetParamIndex( "Resolution X", resolutionXParam ) == false || modifier.tryGetParamIndex( "Resolution Y", resolutionYParam ) == false ||
                                               modifier.tryGetParamIndex( "Offsets", outSet._offsetsParam ) == false || frame._modifierBox.isValid() == false;
                if ( bInvalidFFDParams )
                    return false;
                outSet._pFFD2D                    = &modifier;
                outSet._box                       = frame._modifierBox; // ffd2d 는 입력 정점 상자의 XY 를 격자로 쓴다
                outSet._countX                    = MathUtil::clamp( modifier.getInt( resolutionXParam ), 2, 8 );
                outSet._countY                    = MathUtil::clamp( modifier.getInt( resolutionYParam ), 2, 8 );
                outSet._countZ                    = 1;
                const float3           size       = outSet._box.getSize();
                const float32          middleZ    = outSet._box.getCenter()._z;
                const vector<float32>& listOffset = modifier.getNumberList( outSet._offsetsParam );
                for ( int32 indexY = 0; indexY < outSet._countY; ++indexY )
                {
                    for ( int32 indexX = 0; indexX < outSet._countX; ++indexX )
                    {
                        const size_t  controlIndex = static_cast<size_t>( indexX + outSet._countX * indexY );
                        const float32 offsetX      = controlIndex * 2 + 1 < listOffset.size() ? listOffset[controlIndex * 2] : 0.0f;
                        const float32 offsetY      = controlIndex * 2 + 1 < listOffset.size() ? listOffset[controlIndex * 2 + 1] : 0.0f;
                        const float32 coordS       = static_cast<float32>( indexX ) / static_cast<float32>( outSet._countX - 1 ) + offsetX;
                        const float32 coordT       = static_cast<float32>( indexY ) / static_cast<float32>( outSet._countY - 1 ) + offsetY;
                        const float3  local{ outSet._box._min._x + coordS * size._x, outSet._box._min._y + coordT * size._y, middleZ };
                        outSet._listWorld.push_back( toWorld( frame, local ) );
                    }
                }
                return true;
            }

            static void drawPointSet( const EditorViewportVisualizerArgs& args, const PointSet& set, int32 selectedPoint, int32 hoveredPoint )
            {
                for ( int32 indexZ = 0; indexZ < set._countZ; ++indexZ )
                {
                    for ( int32 indexY = 0; indexY < set._countY; ++indexY )
                    {
                        for ( int32 indexX = 0; indexX < set._countX; ++indexX )
                        {
                            const int32 index = indexX + set._countX * ( indexY + set._countY * indexZ );
                            if ( static_cast<size_t>( index ) >= set._listWorld.size() )
                                continue;
                            const float3& point = set._listWorld[static_cast<size_t>( index )];
                            if ( indexX + 1 < set._countX )
                                drawSegment( args, point, set._listWorld[static_cast<size_t>( index + 1 )], kLatticeLineColor, 1.0f );
                            if ( indexY + 1 < set._countY )
                                drawSegment( args, point, set._listWorld[static_cast<size_t>( index + set._countX )], kLatticeLineColor, 1.0f );
                            if ( indexZ + 1 < set._countZ )
                                drawSegment( args, point, set._listWorld[static_cast<size_t>( index + set._countX * set._countY )], kLatticeLineColor, 1.0f );
                            ImVec2 screen;
                            if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) == false )
                                continue;
                            if ( index == selectedPoint )
                            {
                                args._pDrawList->AddCircleFilled( screen, 6.0f, kSelectedPointColor );
                                args._pDrawList->AddCircle( screen, 8.0f, kHoverColor, 0, 1.5f );
                            }
                            else
                            {
                                args._pDrawList->AddCircleFilled( screen, index == hoveredPoint ? 5.0f : 3.5f, index == hoveredPoint ? kHoverColor : kLatticePointColor );
                            }
                        }
                    }
                }
            }

            /** @brief 제어점 하나를 정규 좌표 @p delta 만큼 옮깁니다. 세션의 대칭 설정을 따릅니다. */
            static void movePoint( MeshModifierSession& session, PointSet& set, int32 pointIndex, const float3& delta )
            {
                if ( set._pLattice != nullptr )
                {
                    const vector<int32> listIndex{ pointIndex };
                    set._pLattice->moveControlPoints( listIndex, delta, session.isMirrorEditing(), session.getMirrorAxis() );
                    return;
                }
                if ( set._pFFD2D == nullptr )
                    return;
                const size_t    pointCount = static_cast<size_t>( set._countX * set._countY );
                vector<float32> listOffset = set._pFFD2D->getNumberList( set._offsetsParam );
                if ( listOffset.size() < pointCount * 2 )
                    listOffset.resize( pointCount * 2, 0.0f );
                const size_t index = static_cast<size_t>( pointIndex );
                listOffset[index * 2] += delta._x;
                listOffset[index * 2 + 1] += delta._y;
                if ( session.isMirrorEditing() )
                {
                    const uint32 axis   = session.getMirrorAxis();
                    const int32  mirror = ModifierGizmoLogic::findMirrorIndex2D( pointIndex, set._countX, set._countY, axis );
                    if ( mirror >= 0 && mirror != pointIndex )
                    {
                        const size_t mirrorIndex = static_cast<size_t>( mirror );
                        listOffset[mirrorIndex * 2] += axis == 0 ? -delta._x : delta._x;
                        listOffset[mirrorIndex * 2 + 1] += axis == 1 ? -delta._y : delta._y;
                    }
                }
                set._pFFD2D->setNumberList( set._offsetsParam, listOffset );
                set._pFFD2D->onParamsChanged();
            }

            /** @brief 월드 이동량을 격자 정규 좌표 이동량으로 바꿉니다. 상자 크기가 0 인 축은 0 입니다. */
            static float3 toNormalizedDelta( const Frame& frame, const PointSet& set, const float3& worldDelta )
            {
                const float3 modifierDelta = float3::transformVector( worldDelta, frame._worldToModifier );
                const float3 size          = set._box.getSize();
                float3       normalized{ 0.0f, 0.0f, 0.0f };
                normalized._x = size._x > MathUtil::kEpsilon ? modifierDelta._x / size._x : 0.0f;
                normalized._y = size._y > MathUtil::kEpsilon ? modifierDelta._y / size._y : 0.0f;
                normalized._z = ( set._pLattice != nullptr && size._z > MathUtil::kEpsilon ) ? modifierDelta._z / size._z : 0.0f;
                return normalized;
            }

            static void collectGizmoHandles( const Frame& frame, vector<Handle>& outListHandle )
            {
                const float32 length = frame._worldSize * kGizmoSizeRatio;
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    const float3 axisWorld = computeBaseAxisWorld( frame, axis );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    const float3 unit = axisWorld.normalize();
                    outListHandle.push_back( Handle{ DragKind::GizmoMove, axis, frame._centerWorld, unit * length, 0.0f } );
                    outListHandle.push_back( Handle{ DragKind::GizmoRotate, axis, frame._centerWorld, unit, length * 0.8f } );
                }
            }

            static void collectPointHandles( const Frame& frame, const PointSet& set, int32 selectedPoint, vector<Handle>& outListHandle )
            {
                if ( selectedPoint < 0 || static_cast<size_t>( selectedPoint ) >= set._listWorld.size() )
                    return;
                const float32 length    = frame._worldSize * kPointHandleRatio;
                const uint32  axisCount = set._pLattice != nullptr ? 3u : 2u; // ffd2d 는 XY 만 옮긴다
                for ( uint32 axis = 0; axis < axisCount; ++axis )
                {
                    const float3 axisWorld = float3::transformVector( makeUnit( axis ), frame._modifierToWorld );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    outListHandle.push_back( Handle{ DragKind::PointAxis, axis, set._listWorld[static_cast<size_t>( selectedPoint )], axisWorld.normalize() * length, 0.0f } );
                }
            }

            /** @brief 손잡이와 레이의 거리입니다. 집을 수 없으면 아주 큰 값입니다. */
            static float32 computeHandleDistance( const Handle& handle, const EditorPickRay& ray )
            {
                if ( handle._kind == DragKind::GizmoRotate )
                {
                    float3 hit{ 0.0f, 0.0f, 0.0f };
                    if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, handle._origin, handle._direction, hit ) == false )
                        return 1e30f;
                    return MathUtil::abs( ( hit - handle._origin ).getLength() - handle._radius );
                }
                return ModifierGizmoLogic::computeRaySegmentDistance( ray._origin, ray._direction, handle._origin, handle._origin + handle._direction );
            }

            static void drawHandle( const EditorViewportVisualizerArgs& args, const Handle& handle, bool bHighlight )
            {
                const ImU32 color = bHighlight ? kHoverColor : kArrAxisColor[MathUtil::min( handle._axis, 2u )];
                if ( handle._kind == DragKind::GizmoRotate )
                {
                    // 고리 평면의 두 방향을 법선에서 만든다
                    const float3 helper = MathUtil::abs( handle._direction._y ) < 0.9f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                    const float3 axisU  = handle._direction.cross( helper ).normalize();
                    const float3 axisV  = handle._direction.cross( axisU ).normalize();
                    drawCircle( args, handle._origin, axisU, axisV, handle._radius, color, bHighlight ? 2.5f : 1.5f );
                    return;
                }
                drawArrow( args, handle._origin, handle._origin + handle._direction, color, bHighlight ? 3.0f : 2.0f );
            }

            /**
             * @brief 자동화가 켜졌으면 이동 화살표마다 화면 사각형 이름(`meshmodifiers.handle.move.<축>`)를 남깁니다 — 시나리오가 손잡이를 끌어 양보를 검증한다.
             * @details 화살표 끝 쪽(길이의 90 %)을 고른다. 가운데 쪽은 다른 축 회전 고리(반경 80 %)와 겹친다.
             */
            static void noteHandleMarks( const EditorViewportVisualizerArgs& args, const vector<Handle>& listHandle )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static constexpr float32 kMarkHalfSize = 4.0f;
                for ( const Handle& handle : listHandle )
                {
                    ImVec2 screen;
                    if ( handle._kind != DragKind::GizmoMove )
                        continue;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._origin + handle._direction * 0.9f, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    const string key = string( "meshmodifiers.handle.move." ) + to_string( handle._axis );
                    EditorSelfTestMarks::noteRect( key.c_str(), float2{ screen.x - kMarkHalfSize, screen.y - kMarkHalfSize }, float2{ screen.x + kMarkHalfSize, screen.y + kMarkHalfSize } );
                }
            }

            static void ensureUndo( MeshModifierSession& session, DragState& state )
            {
                if ( state._bUndoRecorded )
                    return;
                session.recordUndo();
                state._bUndoRecorded = true;
            }

            /** @brief 끄는 중인 손잡이를 지금 레이로 옮깁니다. */
            static void updateDrag( MeshModifierSession& session, DragState& state, const Frame& frame, PointSet& set, const EditorPickRay& ray )
            {
                MeshModifierGizmo& gizmo  = frame._pModifier->getGizmo();
                const Handle&      handle = state._handle;
                switch ( handle._kind )
                {
                    case DragKind::GizmoMove:
                    {
                        // 손잡이 방향은 기준 공간 길이 1 의 월드 벡터라 매개변수 차이가 곧 기준 공간 이동량이다
                        const float3  axisWorld = handle._direction / MathUtil::max( frame._worldSize * kGizmoSizeRatio, MathUtil::kEpsilon );
                        const float3  baseAxis  = computeBaseAxisWorld( frame, handle._axis );
                        const float32 baseScale = MathUtil::max( baseAxis.getLength(), MathUtil::kEpsilon );
                        const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                        float3        center    = state._startCenter;
                        MeshModifierUtil::setComponent( center, handle._axis,
                                                        MeshModifierUtil::getComponent( center, handle._axis ) + ( parameter - state._startParameter ) / baseScale );
                        if ( ( center - gizmo._center ).getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session, state );
                        gizmo._center = center;
                        break;
                    }
                    case DragKind::GizmoRotate:
                    {
                        float3 hit{ 0.0f, 0.0f, 0.0f };
                        if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, handle._origin, handle._direction, hit ) == false )
                            return;
                        const float32 angle = ModifierGizmoLogic::computeSignedAngleDegree( state._startVector, hit - handle._origin, handle._direction );
                        // 고리는 기준 공간 축을 그리므로 시작 회전에 그 축 둘레 증분 회전을 곱해 오일러로 다시 푼다(다른 성분이 0 이 아니어도 맞다)
                        const float3 rotation = ModifierGizmoLogic::applyRotationDelta( state._startRotation, handle._axis, angle );
                        if ( ( rotation - gizmo._rotationDegree ).getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session, state );
                        gizmo._rotationDegree = rotation;
                        break;
                    }
                    case DragKind::PointAxis:
                    case DragKind::PointFree:
                    {
                        if ( state._pointIndex < 0 || static_cast<size_t>( state._pointIndex ) >= set._listWorld.size() )
                            return;
                        float3 worldDelta{ 0.0f, 0.0f, 0.0f };
                        if ( handle._kind == DragKind::PointAxis )
                        {
                            const float3  unit      = handle._direction.normalize();
                            const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, unit );
                            worldDelta              = unit * ( parameter - state._startParameter );
                        }
                        else if ( ModifierGizmoLogic::tryComputePlaneDragDelta( state._startRay._origin, state._startRay._direction, ray._origin, ray._direction,
                                                                                handle._origin, state._startRay._direction, worldDelta ) == false )
                        {
                            return;
                        }
                        const float3 total     = toNormalizedDelta( frame, set, worldDelta );
                        const float3 increment = total - state._appliedDelta;
                        if ( increment.getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session, state );
                        movePoint( session, set, state._pointIndex, increment );
                        state._appliedDelta = total;
                        break;
                    }
                    case DragKind::None:
                    {
                        return;
                    }
                }
                session.markDirty();
            }

            static void beginDrag( DragState& state, const Handle& handle, const Frame& frame, int32 modifierIndex, int32 pointIndex, const EditorPickRay& ray )
            {
                endDrag( state );
                // 핸들을 집는 순간부터 놓을 때까지 뷰포트 선택 · 카메라가 마우스를 가져가지 않게 한다(호버만으로는 잡지 않는다)
                EditorViewportMouseClaim::acquire( getMouseClaimOwner() );
                state._bDragging     = true;
                state._handle        = handle;
                state._startRay      = ray;
                state._modifierIndex = modifierIndex;
                state._pointIndex    = pointIndex;
                state._startCenter   = frame._pModifier->getGizmo()._center;
                state._startRotation = frame._pModifier->getGizmo()._rotationDegree;
                if ( handle._kind == DragKind::GizmoMove )
                {
                    const float3 axisWorld = handle._direction / MathUtil::max( frame._worldSize * kGizmoSizeRatio, MathUtil::kEpsilon );
                    state._startParameter  = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                }
                else if ( handle._kind == DragKind::PointAxis )
                {
                    state._startParameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, handle._direction.normalize() );
                }
                else if ( handle._kind == DragKind::GizmoRotate )
                {
                    float3 hit{ 0.0f, 0.0f, 0.0f };
                    if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, handle._origin, handle._direction, hit ) )
                        state._startVector = hit - handle._origin;
                }
            }

            /** @brief 손잡이 · 제어점을 그리고 마우스로 고르고 끕니다. */
            static void processEditing( const EditorViewportVisualizerArgs& args, MeshModifierSession& session, const Frame& frame, PointSet& set )
            {
                DragState&  state         = getState()._drag;
                const int32 modifierIndex = session.getSelectedIndex();
                if ( state._bDragging && state._modifierIndex != modifierIndex )
                    endDrag( state ); // 끄는 중에 다른 모디파이어로 바뀌었다

                int32 selectedPoint = session.getSelectedControlPoint();
                if ( set.isValid() && selectedPoint >= static_cast<int32>( set._listWorld.size() ) )
                {
                    selectedPoint = static_cast<int32>( set._listWorld.size() ) - 1;
                    session.setSelectedControlPoint( selectedPoint );
                }

                vector<Handle>& listHandle = getState()._listHandle; // 프레임마다 다시 채우는 재사용 버퍼(UI 스레드 하나만 쓴다)
                listHandle.clear();
                if ( set.isValid() )
                    collectPointHandles( frame, set, selectedPoint, listHandle );
                collectGizmoHandles( frame, listHandle );

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                EditorPickRay ray{};
                const bool    bHasRay = EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );

                if ( state._bDragging )
                {
                    if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
                        endDrag( state ); // 놓았다. 되돌리기는 첫 변경 때 이미 남겼다
                    else
                    {
                        // 양보는 매 프레임 다시 잡아야 유지된다 — 끄는 중에 시각화 토글을 끄면 이 함수가 안 불려 다음 프레임 시작에 뷰포트가 거둔다
                        EditorViewportMouseClaim::acquire( getMouseClaimOwner() );
                        if ( bHasRay )
                            updateDrag( session, state, frame, set, ray );
                    }
                }

                int32      hoveredHandle = -1;
                int32      hoveredPoint  = -1;
                const bool bCanHover     = state._bDragging == false && bMouseInside && bHasRay && args._bGizmoDragging == false;
                if ( bCanHover )
                {
                    float32 bestDistance = 1e30f;
                    for ( size_t index = 0; index < listHandle.size(); ++index )
                    {
                        const Handle& handle   = listHandle[index];
                        const float32 length   = handle._kind == DragKind::GizmoRotate ? handle._radius : handle._direction.getLength();
                        const float32 distance = computeHandleDistance( handle, ray );
                        if ( distance <= length * kPickWidthRatio && distance < bestDistance )
                        {
                            bestDistance  = distance;
                            hoveredHandle = static_cast<int32>( index );
                        }
                    }
                    if ( hoveredHandle < 0 && set.isValid() )
                        hoveredPoint = ModifierGizmoLogic::findPointOnRay( ray._origin, ray._direction, set._listWorld, frame._worldSize * kPointPickRatio );
                    if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                    {
                        if ( hoveredHandle >= 0 )
                        {
                            beginDrag( state, listHandle[static_cast<size_t>( hoveredHandle )], frame, modifierIndex, selectedPoint, ray );
                        }
                        else if ( hoveredPoint >= 0 )
                        {
                            // 점을 누르면 고르고, 그대로 끌면 카메라를 마주 보는 평면에서 옮긴다
                            session.setSelectedControlPoint( hoveredPoint );
                            selectedPoint = hoveredPoint;
                            const Handle freeHandle{
                                DragKind::PointFree, 0, set._listWorld[static_cast<size_t>( hoveredPoint )], float3{ 0.0f, 0.0f, 0.0f },
                                0.0f
                            };
                            beginDrag( state, freeHandle, frame, modifierIndex, hoveredPoint, ray );
                        }
                    }
                }

                if ( set.isValid() )
                    drawPointSet( args, set, selectedPoint, hoveredPoint );
                for ( size_t index = 0; index < listHandle.size(); ++index )
                {
                    const Handle& handle  = listHandle[index];
                    const bool    bActive = state._bDragging && state._handle._kind == handle._kind && state._handle._axis == handle._axis;
                    drawHandle( args, handle, bActive || static_cast<int32>( index ) == hoveredHandle );
                }
                noteHandleMarks( args, listHandle );
                ImVec2 centerScreen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, frame._centerWorld, args._canvasPos, args._canvasSize, centerScreen ) )
                    args._pDrawList->AddCircle( centerScreen, 6.0f, kAxisColor, 0, 2.0f );
            }

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                MeshModifierSession* pSession      = findSubsystem<MeshModifierSession>();
                const bool           bNoDragTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoDragTarget )
                {
                    endDrag( getState()._drag );
                    return;
                }
                MeshModifierSession& session = *pSession;
                session.refreshPreview();

                vector<float3>& listSegmentPoint = getState()._listSegmentPoint; // 프레임마다 다시 채우는 재사용 버퍼
                session.makePreviewSegments( listSegmentPoint );
                drawSegments( args, listSegmentPoint, kPreviewColor );

                MeshModifier* pModifier = session.getStack().findModifier( session.getSelectedIndex() );
                Frame         frame{};
                if ( pModifier == nullptr || makeFrame( session, *pModifier, frame ) == false )
                {
                    endDrag( getState()._drag );
                    return;
                }

                // 주축 선(기존 표시)과 범위
                float3 axisDirection{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( axisDirection, frame._axisIndex, frame._worldSize * 0.6f );
                const float3 axisWorld = float3::transformVector( axisDirection, frame._modifierToWorld );
                drawSegment( args, frame._centerWorld - axisWorld, frame._centerWorld + axisWorld, kAxisColor, 1.0f );
                drawRanges( args, frame );

                PointSet& pointSet = getState()._pointSet; // 제어점 월드 목록을 프레임마다 다시 채운다
                pointSet           = PointSet{};
                (void)makePointSet( frame, pointSet ); // 격자 · ffd2d 가 아니면 빈 채로 둔다
                processEditing( args, session, frame, pointSet );
            }

            static void drawStage( const EditorViewportVisualizerArgs& args )
            {
                MeshModifierSession* pSession        = findSubsystem<MeshModifierSession>();
                const bool           bNoActiveTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoActiveTarget )
                    return;
                vector<float3>& listStagePoint = getState()._listStagePoint; // 프레임마다 다시 채우는 재사용 버퍼
                pSession->makeStagePreviewSegments( listStagePoint );
                drawSegments( args, listStagePoint, kStageColor );
            }
        };

        /**
         * @class MeshModifiersVisualizerState
         * @brief 뷰포트 끌기 상태와 프레임 재사용 버퍼의 주인입니다(확장이 기여하는 에디터 서브시스템 — `SW_EDITOR_SUBSYSTEM`). 에디터 범위마다 하나이고 모듈이 내려가면 함께 사라집니다.
         */
        class MeshModifiersVisualizerState final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "MeshModifiersVisualizerState";
            const utf8*                  getSubsystemName() const override { return kSubsystemName; }

            MeshModifiersVisualizerInternal::State _state{};
        };

        MeshModifiersVisualizerInternal::State& MeshModifiersVisualizerInternal::getState()
        {
            return getSubsystem<MeshModifiersVisualizerState>()._state;
        }
    } // namespace

    SW_EDITOR_SUBSYSTEM( MeshModifiersVisualizerState, 9500 );

    SW_EDITOR_VISUALIZER( MeshModifiers, "meshmodifiers.preview", 910, editoricon::kCube, "Modifiers",
                          "메시 모디파이어 스택의 변형 미리보기 선, 기즈모 · 범위 표시와 끌기 편집, 격자 제어점 고르기 · 끌기", true, &MeshModifiersVisualizerInternal::draw );
    SW_EDITOR_VISUALIZER( MeshModifiersStage, "meshmodifiers.stage", 911, nullptr, "Stage", "고른 모디파이어까지만 건 결과를 다른 색 선으로 겹쳐 그립니다(변형 영향 미리보기)", false,
                          &MeshModifiersVisualizerInternal::drawStage );
} // namespace sw::editor
