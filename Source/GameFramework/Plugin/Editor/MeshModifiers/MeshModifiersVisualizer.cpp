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
#include "Core/Math/Ray.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Localization/LocText.h"

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
            /** @brief 테마 색 토큰을 그리기 목록 색으로 바꿉니다. */
            static ImU32 themeColor( const Color& color ) { return EditorColorUtil::toImU32( color ); }
            /** @brief 손잡이 축(0 X · 1 Y · 2 Z)의 색입니다. */
            static ImU32 axisColor( uint32 axis )
            {
                switch ( axis )
                {
                    case 0:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisX" ) );
                    case 1:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisY" ) );
                    default:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisZ" ) );
                }
            }
            static constexpr uint32      kRingSegmentCount = 48;
            static constexpr float32     kPickWidthRatio   = 0.06f; ///< 손잡이 길이 대비 집는 두께
            static constexpr float32     kPointPickRatio   = 0.04f; ///< 원본 상자 대각선(월드) 대비 제어점 구 반경
            static constexpr float32     kGizmoSizeRatio   = 0.35f; ///< 원본 상자 대각선(월드) 대비 기즈모 손잡이 길이
            static constexpr float32     kPointHandleRatio = 0.15f; ///< 원본 상자 대각선(월드) 대비 제어점 축 손잡이 길이
            static constexpr const utf8* kFFD2DTypeID      = "ffd2d";

            enum class DragKind : uint8
            {
                None,
                GizmoMove,   ///< 기즈모 중심을 기준 축 하나로
                GizmoRotate, ///< 기즈모 오일러 성분 하나를 고리로
                PointAxis,   ///< 고른 제어점을 모디파이어 축 하나로
                PointFree,   ///< 고른 제어점을 카메라를 마주 보는 평면에서
            };

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
        };

        /** @brief 이번 프레임에 집을 수 있는 손잡이 하나입니다(월드). */
        struct ModifierViewHandle
        {
            MeshModifiersVisualizerInternal::DragKind _kind{ MeshModifiersVisualizerInternal::DragKind::None };
            uint32                                    _axis{ 0 };
            float3                                    _origin{ 0.0f, 0.0f, 0.0f };
            float3                                    _direction{ 0.0f, 0.0f, 0.0f }; ///< 이동은 끝점까지의 벡터, 회전은 고리 법선(단위)
            float32                                   _radius{ 0.0f };                ///< 회전 고리 반경

            /** @brief 손잡이와 레이의 거리입니다. 집을 수 없으면 아주 큰 값입니다. */
            float32 computeDistance( const Ray& ray ) const
            {
                if ( _kind == MeshModifiersVisualizerInternal::DragKind::GizmoRotate )
                {
                    float3 hit{ 0.0f, 0.0f, 0.0f };
                    if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, _origin, _direction, hit ) == false )
                        return 1e30f;
                    return MathUtil::abs( ( hit - _origin ).getLength() - _radius );
                }
                return ModifierGizmoLogic::computeRaySegmentDistance( ray._origin, ray._direction, _origin, _origin + _direction );
            }

            /** @brief 손잡이를 그립니다(이동은 화살표, 회전은 고리). */
            void draw( const EditorViewportVisualizerArgs& args, bool bHighlight ) const
            {
                const ImU32 color = bHighlight ? MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ) : MeshModifiersVisualizerInternal::axisColor( _axis );
                if ( _kind == MeshModifiersVisualizerInternal::DragKind::GizmoRotate )
                {
                    // 고리 평면의 두 방향을 법선에서 만든다
                    const float3 helper = MathUtil::abs( _direction._y ) < 0.9f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                    const float3 axisU  = _direction.cross( helper ).normalize();
                    const float3 axisV  = _direction.cross( axisU ).normalize();
                    MeshModifiersVisualizerInternal::drawCircle( args, _origin, axisU, axisV, _radius, color, bHighlight ? 2.5f : 1.5f );
                    return;
                }
                MeshModifiersVisualizerInternal::drawArrow( args, _origin, _origin + _direction, color, bHighlight ? 3.0f : 2.0f );
            }

            /**
             * @brief 자동화가 켜졌으면 이동 화살표마다 화면 사각형 이름(`meshmodifiers.handle.move.<축>`)를 남깁니다 — 시나리오가 손잡이를 끌어 양보를 검증한다.
             * @details 화살표 끝 쪽(길이의 90 %)을 고른다. 가운데 쪽은 다른 축 회전 고리(반경 80 %)와 겹친다.
             */
            static void recordMarks( const EditorViewportVisualizerArgs& args, const vector<ModifierViewHandle>& listHandle )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static constexpr float32 kMarkHalfSize = 4.0f;
                for ( const ModifierViewHandle& handle : listHandle )
                {
                    ImVec2 screen;
                    if ( handle._kind != MeshModifiersVisualizerInternal::DragKind::GizmoMove )
                        continue;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._origin + handle._direction * 0.9f, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    const string key = string( "meshmodifiers.handle.move." ) + to_string( handle._axis );
                    EditorSelfTestMarks::recordRect( key.c_str(), float2{ screen.x - kMarkHalfSize, screen.y - kMarkHalfSize }, float2{ screen.x + kMarkHalfSize, screen.y + kMarkHalfSize } );
                }
            }
        };

        /** @brief 고른 모디파이어 한 프레임의 공간 정보입니다(`build` 가 한 번 채우고 그 뒤로는 읽기만 한다). */
        struct ModifierViewFrame
        {
            MeshModifier*      _pModifier{ nullptr };
            float4x4           _modifierToWorld{};
            float4x4           _worldToModifier{};
            MeshModifierBounds _modifierBox{}; ///< 입력 메시 상자 모서리를 모디파이어 공간으로 옮긴 근사 상자
            float3             _centerWorld{ 0.0f, 0.0f, 0.0f };
            float32            _worldSize{ 1.0f }; ///< 원본 상자 대각선의 월드 길이
            uint32             _axisIndex{ 1 };

            /** @brief 기즈모 기준 공간의 축 @p axisIndex 를 월드 방향(기준 공간 길이 1 = 반환 길이)으로 옮깁니다. */
            float3 computeBaseAxisWorld( uint32 axisIndex ) const
            {
                const float3 modifierDirection = _pModifier->getGizmo().toModifierDirection( MeshModifiersVisualizerInternal::makeUnit( axisIndex ) );
                return float3::transformVector( modifierDirection, _modifierToWorld );
            }

            /** @brief 세션의 고른 모디파이어 @p modifier 로 공간 정보를 채웁니다. 원본 상자가 없으면 false 입니다. */
            bool build( MeshModifierSession& session, MeshModifier& modifier )
            {
                MeshModifierEvaluation evaluation{};
                evaluation._meshToWorld          = session.computeMeshToWorld();
                evaluation._pInputs              = &session.getModifierInputs();
                const float4x4 localToModifier   = modifier.computeLocalToModifier( evaluation );
                _pModifier                       = &modifier;
                _modifierToWorld                 = localToModifier.invert() * evaluation._meshToWorld;
                _worldToModifier                 = _modifierToWorld.invert();
                _centerWorld                     = float3::transform( float3{ 0.0f, 0.0f, 0.0f }, _modifierToWorld );
                _axisIndex                       = MathUtil::min( modifier.getGizmo()._axisIndex, 2u );
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
                _modifierBox          = MeshModifierBounds::makeFromPoints( listCorner );
                const float3 worldMin = float3::transform( source._min, evaluation._meshToWorld );
                const float3 worldMax = float3::transform( source._max, evaluation._meshToWorld );
                _worldSize            = MathUtil::max( ( worldMax - worldMin ).getLength(), 0.1f );
                return true;
            }

            float3 toWorld( const float3& modifierPoint ) const
            {
                return float3::transform( modifierPoint, _modifierToWorld );
            }

            /** @brief 모디파이어 범위 매개변수(반경 · 한계 · 중심 · 방향)를 그립니다. */
            void drawRanges( const EditorViewportVisualizerArgs& args ) const
            {
                const MeshModifier& modifier = *_pModifier;
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
                const float32  arrowLength = _worldSize * MeshModifiersVisualizerInternal::kGizmoSizeRatio;
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
                                const float3 axisU    = MeshModifiersVisualizerInternal::makeUnit( ( plane + 1 ) % 3 );
                                const float3 axisV    = MeshModifiersVisualizerInternal::makeUnit( ( plane + 2 ) % 3 );
                                float3       previous = toWorld( center + axisU * radius );
                                for ( uint32 segment = 1; segment <= MeshModifiersVisualizerInternal::kRingSegmentCount; ++segment )
                                {
                                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( MeshModifiersVisualizerInternal::kRingSegmentCount );
                                    const float3  next  = toWorld( center + axisU * ( ::cosf( angle ) * radius ) + axisV * ( ::sinf( angle ) * radius ) );
                                    MeshModifiersVisualizerInternal::drawSegment( args, previous, next, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierRange" ) ), 1.0f );
                                    previous = next;
                                }
                            }
                            break;
                        }
                        case ModifierRangeKind::LowerLimit:
                        case ModifierRangeKind::UpperLimit:
                        {
                            if ( ModifierGizmoLogic::tryMakeLimitQuad( _modifierBox, _axisIndex, modifier.getFloat( index ), listCorner ) == false )
                                break;
                            for ( size_t corner = 0; corner < listCorner.size(); ++corner )
                            {
                                MeshModifiersVisualizerInternal::drawSegment( args, toWorld( listCorner[corner] ), toWorld( listCorner[( corner + 1 ) % listCorner.size()] ), MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierRange" ) ), 1.5f );
                            }
                            break;
                        }
                        case ModifierRangeKind::Center:
                        {
                            MeshModifiersVisualizerInternal::drawMarker( args, toWorld( modifier.getVector3( index ) ), MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierRange" ) ) );
                            break;
                        }
                        case ModifierRangeKind::Direction:
                        {
                            const float3 modifierDirection = def._kind == MeshModifierParamKind::Vector3
                                                               ? ( modifier.getVector3( index ) ).normalizeOr( float3{ 0.0f, 0.0f, 0.0f }, MathUtil::kEpsilon )
                                                               : ModifierGizmoLogic::computeDirectionVector( _axisIndex, modifier.getFloat( index ) );
                            const float3 worldFrom         = toWorld( center );
                            const float3 worldStep         = float3::transformVector( modifierDirection, _modifierToWorld );
                            if ( worldStep.getLengthSquared() <= MathUtil::kEpsilonSquared )
                                break;
                            MeshModifiersVisualizerInternal::drawArrow( args, worldFrom, worldFrom + worldStep.normalize() * arrowLength, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierRange" ) ), 2.0f );
                            break;
                        }
                        case ModifierRangeKind::None:
                        {
                            break;
                        }
                    }
                }
            }

            /** @brief 기즈모 이동 화살표 · 회전 고리 손잡이를 더합니다. */
            void collectGizmoHandles( vector<ModifierViewHandle>& outListHandle ) const
            {
                const float32 length = _worldSize * MeshModifiersVisualizerInternal::kGizmoSizeRatio;
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    const float3 axisWorld = computeBaseAxisWorld( axis );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    const float3 unit = axisWorld.normalize();
                    outListHandle.push_back( ModifierViewHandle{ MeshModifiersVisualizerInternal::DragKind::GizmoMove, axis, _centerWorld, unit * length, 0.0f } );
                    outListHandle.push_back( ModifierViewHandle{ MeshModifiersVisualizerInternal::DragKind::GizmoRotate, axis, _centerWorld, unit, length * 0.8f } );
                }
            }
        };

        /** @brief 제어점 편집 대상입니다(격자 또는 ffd2d). `build` 가 프레임마다 다시 채운다. */
        class ModifierPointSet
        {
        public:
            ModifierPointSet()
                : _pLattice{ nullptr }
                , _pFFD2D{ nullptr }
                , _box{}
                , _listWorld{}
                , _countX{ 0 }
                , _countY{ 0 }
                , _countZ{ 1 }
                , _offsetsParam{ 0 }
            {
            }

            bool                  isValid() const { return _pLattice != nullptr || _pFFD2D != nullptr; }
            const vector<float3>& getWorldPoints() const { return _listWorld; }

            /** @brief 고른 모디파이어가 격자 · ffd2d 면 제어점을 월드로 채웁니다. */
            bool build( const ModifierViewFrame& frame )
            {
                *this                  = ModifierPointSet{};
                MeshModifier& modifier = *frame._pModifier;
                _listWorld.clear();
                _pLattice = LatticeModifier::findFrom( modifier );
                if ( _pLattice != nullptr )
                {
                    const LatticeModifier& lattice = *_pLattice;
                    _box                           = lattice.getEffectiveBox( frame._modifierBox );
                    _countX                        = lattice.getResolutionX();
                    _countY                        = lattice.getResolutionY();
                    _countZ                        = lattice.getResolutionZ();
                    const float3 size              = _box.getSize();
                    _listWorld.reserve( static_cast<size_t>( lattice.getControlPointCount() ) );
                    for ( int32 index = 0; index < lattice.getControlPointCount(); ++index )
                    {
                        const float3& normalized = lattice.getControlPoint( index );
                        const float3  local{ _box._min._x + normalized._x * size._x, _box._min._y + normalized._y * size._y,
                                            _box._min._z + normalized._z * size._z };
                        _listWorld.push_back( frame.toWorld( local ) );
                    }
                    return true;
                }
                if ( string_view( modifier.getTypeID() ) != string_view( MeshModifiersVisualizerInternal::kFFD2DTypeID ) )
                    return false;
                uint32     resolutionXParam  = 0;
                uint32     resolutionYParam  = 0;
                const bool bInvalidFFDParams = modifier.tryGetParamIndex( "Resolution X", resolutionXParam ) == false || modifier.tryGetParamIndex( "Resolution Y", resolutionYParam ) == false ||
                                               modifier.tryGetParamIndex( "Offsets", _offsetsParam ) == false || frame._modifierBox.isValid() == false;
                if ( bInvalidFFDParams )
                    return false;
                _pFFD2D                           = &modifier;
                _box                              = frame._modifierBox; // ffd2d 는 입력 정점 상자의 XY 를 격자로 쓴다
                _countX                           = MathUtil::clamp( modifier.getInt( resolutionXParam ), 2, 8 );
                _countY                           = MathUtil::clamp( modifier.getInt( resolutionYParam ), 2, 8 );
                _countZ                           = 1;
                const float3           size       = _box.getSize();
                const float32          middleZ    = _box.getCenter()._z;
                const vector<float32>& listOffset = modifier.getNumberList( _offsetsParam );
                for ( int32 indexY = 0; indexY < _countY; ++indexY )
                {
                    for ( int32 indexX = 0; indexX < _countX; ++indexX )
                    {
                        const size_t  controlIndex = static_cast<size_t>( indexX + _countX * indexY );
                        const float32 offsetX      = controlIndex * 2 + 1 < listOffset.size() ? listOffset[controlIndex * 2] : 0.0f;
                        const float32 offsetY      = controlIndex * 2 + 1 < listOffset.size() ? listOffset[controlIndex * 2 + 1] : 0.0f;
                        const float32 coordS       = static_cast<float32>( indexX ) / static_cast<float32>( _countX - 1 ) + offsetX;
                        const float32 coordT       = static_cast<float32>( indexY ) / static_cast<float32>( _countY - 1 ) + offsetY;
                        const float3  local{ _box._min._x + coordS * size._x, _box._min._y + coordT * size._y, middleZ };
                        _listWorld.push_back( frame.toWorld( local ) );
                    }
                }
                return true;
            }

            /** @brief 격자 선과 제어점을 그립니다(고른 점 · 호버 점은 강조). */
            void draw( const EditorViewportVisualizerArgs& args, int32 selectedPoint, int32 hoveredPoint ) const
            {
                for ( int32 indexZ = 0; indexZ < _countZ; ++indexZ )
                {
                    for ( int32 indexY = 0; indexY < _countY; ++indexY )
                    {
                        for ( int32 indexX = 0; indexX < _countX; ++indexX )
                        {
                            const int32 index = indexX + _countX * ( indexY + _countY * indexZ );
                            if ( static_cast<size_t>( index ) >= _listWorld.size() )
                                continue;
                            const float3& point = _listWorld[static_cast<size_t>( index )];
                            if ( indexX + 1 < _countX )
                                MeshModifiersVisualizerInternal::drawSegment( args, point, _listWorld[static_cast<size_t>( index + 1 )], MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "LatticeLine" ) ), 1.0f );
                            if ( indexY + 1 < _countY )
                                MeshModifiersVisualizerInternal::drawSegment( args, point, _listWorld[static_cast<size_t>( index + _countX )], MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "LatticeLine" ) ), 1.0f );
                            if ( indexZ + 1 < _countZ )
                                MeshModifiersVisualizerInternal::drawSegment( args, point, _listWorld[static_cast<size_t>( index + _countX * _countY )], MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "LatticeLine" ) ), 1.0f );
                            ImVec2 screen;
                            if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) == false )
                                continue;
                            if ( index == selectedPoint )
                            {
                                args._pDrawList->AddCircleFilled( screen, 6.0f, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "LatticeSelected" ) ) );
                                args._pDrawList->AddCircle( screen, 8.0f, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ), 0, 1.5f );
                            }
                            else
                            {
                                args._pDrawList->AddCircleFilled( screen, index == hoveredPoint ? 5.0f : 3.5f, index == hoveredPoint ? MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ) : MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "LatticePoint" ) ) );
                            }
                        }
                    }
                }
            }

            /** @brief 제어점 하나를 정규 좌표 @p delta 만큼 옮깁니다. 세션의 대칭 설정을 따릅니다. */
            void movePoint( MeshModifierSession& session, int32 pointIndex, const float3& delta )
            {
                if ( _pLattice != nullptr )
                {
                    const vector<int32> listIndex{ pointIndex };
                    _pLattice->moveControlPoints( listIndex, delta, session.isMirrorEditing(), session.getMirrorAxis() );
                    return;
                }
                if ( _pFFD2D == nullptr )
                    return;
                const size_t    pointCount = static_cast<size_t>( _countX * _countY );
                vector<float32> listOffset = _pFFD2D->getNumberList( _offsetsParam );
                if ( listOffset.size() < pointCount * 2 )
                    listOffset.resize( pointCount * 2, 0.0f );
                const size_t index = static_cast<size_t>( pointIndex );
                listOffset[index * 2] += delta._x;
                listOffset[index * 2 + 1] += delta._y;
                if ( session.isMirrorEditing() )
                {
                    const uint32          axis   = session.getMirrorAxis();
                    const optional<int32> mirror = ModifierGizmoLogic::findMirrorIndex2D( pointIndex, _countX, _countY, axis );
                    if ( mirror.has_value() && *mirror != pointIndex )
                    {
                        const size_t mirrorIndex = static_cast<size_t>( *mirror );
                        listOffset[mirrorIndex * 2] += axis == 0 ? -delta._x : delta._x;
                        listOffset[mirrorIndex * 2 + 1] += axis == 1 ? -delta._y : delta._y;
                    }
                }
                _pFFD2D->setNumberList( _offsetsParam, listOffset );
                _pFFD2D->onParamsChanged();
            }

            /** @brief 월드 이동량을 격자 정규 좌표 이동량으로 바꿉니다. 상자 크기가 0 인 축은 0 입니다. */
            float3 toNormalizedDelta( const ModifierViewFrame& frame, const float3& worldDelta ) const
            {
                const float3 modifierDelta = float3::transformVector( worldDelta, frame._worldToModifier );
                const float3 size          = _box.getSize();
                float3       normalized{ 0.0f, 0.0f, 0.0f };
                normalized._x = size._x > MathUtil::kEpsilon ? modifierDelta._x / size._x : 0.0f;
                normalized._y = size._y > MathUtil::kEpsilon ? modifierDelta._y / size._y : 0.0f;
                normalized._z = ( _pLattice != nullptr && size._z > MathUtil::kEpsilon ) ? modifierDelta._z / size._z : 0.0f;
                return normalized;
            }

            /** @brief 고른 제어점의 축 손잡이를 더합니다. 고른 점이 없으면 아무것도 하지 않는다. */
            void collectHandles( const ModifierViewFrame& frame, int32 selectedPoint, vector<ModifierViewHandle>& outListHandle ) const
            {
                if ( selectedPoint < 0 || static_cast<size_t>( selectedPoint ) >= _listWorld.size() )
                    return;
                const float32 length    = frame._worldSize * MeshModifiersVisualizerInternal::kPointHandleRatio;
                const uint32  axisCount = _pLattice != nullptr ? 3u : 2u; // ffd2d 는 XY 만 옮긴다
                for ( uint32 axis = 0; axis < axisCount; ++axis )
                {
                    const float3 axisWorld = float3::transformVector( MeshModifiersVisualizerInternal::makeUnit( axis ), frame._modifierToWorld );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    outListHandle.push_back( ModifierViewHandle{ MeshModifiersVisualizerInternal::DragKind::PointAxis, axis, _listWorld[static_cast<size_t>( selectedPoint )], axisWorld.normalize() * length, 0.0f } );
                }
            }

        private:
            LatticeModifier*   _pLattice;
            MeshModifier*      _pFFD2D;
            MeshModifierBounds _box; ///< 모디파이어 공간 격자 상자
            vector<float3>     _listWorld;
            int32              _countX;
            int32              _countY;
            int32              _countZ;
            uint32             _offsetsParam;
        };

        /** @brief 끌기 하나의 상태입니다. UI 스레드 하나만 씁니다. 뷰포트 마우스 양보의 주인 식별자는 이 객체의 주소다(에디터 범위마다 다르다). */
        class ModifierHandleDrag
        {
        public:
            ModifierHandleDrag()
                : _handle{}
                , _startRay{}
                , _startCenter{ 0.0f, 0.0f, 0.0f }
                , _startRotation{ 0.0f, 0.0f, 0.0f }
                , _startVector{ 0.0f, 0.0f, 0.0f }
                , _appliedDelta{ 0.0f, 0.0f, 0.0f }
                , _startParameter{ 0.0f }
                , _modifierIndex{ -1 }
                , _pointIndex{ -1 }
                , _bDragging{ SW_FALSE }
                , _bUndoRecorded{ SW_FALSE }
            {
            }

            bool  isDragging() const { return _bDragging == SW_TRUE; }
            int32 getModifierIndex() const { return _modifierIndex; }
            /** @brief 끄는 손잡이가 @p handle 과 같은 종류 · 축인지입니다(강조 표시). */
            bool isActive( const ModifierViewHandle& handle ) const { return isDragging() && _handle._kind == handle._kind && _handle._axis == handle._axis; }

            /** @brief 끌기를 끝냅니다. 핸들을 집고 있었으면 뷰포트 마우스 양보도 돌려준다(놓기 · 취소 모두). */
            void end()
            {
                if ( _bDragging )
                    EditorViewportMouseClaim::release( this );
                *this = ModifierHandleDrag{};
            }

            /** @brief 양보는 매 프레임 다시 잡아야 유지된다 — 끄는 중에 시각화 토글을 끄면 그리기가 안 불려 다음 프레임 시작에 뷰포트가 거둔다. */
            void acquireClaim() const { EditorViewportMouseClaim::acquire( this ); }

            /** @brief 끄는 중인 손잡이를 지금 레이로 옮깁니다. */
            void update( MeshModifierSession& session, const ModifierViewFrame& frame, ModifierPointSet& set, const Ray& ray )
            {
                MeshModifierGizmo&        gizmo  = frame._pModifier->getGizmo();
                const ModifierViewHandle& handle = _handle;
                switch ( handle._kind )
                {
                    case MeshModifiersVisualizerInternal::DragKind::GizmoMove:
                    {
                        // 손잡이 방향은 기준 공간 길이 1 의 월드 벡터라 매개변수 차이가 곧 기준 공간 이동량이다
                        const float3  axisWorld = handle._direction / MathUtil::max( frame._worldSize * MeshModifiersVisualizerInternal::kGizmoSizeRatio, MathUtil::kEpsilon );
                        const float3  baseAxis  = frame.computeBaseAxisWorld( handle._axis );
                        const float32 baseScale = MathUtil::max( baseAxis.getLength(), MathUtil::kEpsilon );
                        const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                        float3        center    = _startCenter;
                        MeshModifierUtil::setComponent( center, handle._axis,
                                                        MeshModifierUtil::getComponent( center, handle._axis ) + ( parameter - _startParameter ) / baseScale );
                        if ( ( center - gizmo._center ).getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session );
                        gizmo._center = center;
                        break;
                    }
                    case MeshModifiersVisualizerInternal::DragKind::GizmoRotate:
                    {
                        float3 hit{ 0.0f, 0.0f, 0.0f };
                        if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, handle._origin, handle._direction, hit ) == false )
                            return;
                        const float32 angle = ModifierGizmoLogic::computeSignedAngleDegree( _startVector, hit - handle._origin, handle._direction );
                        // 고리는 기준 공간 축을 그리므로 시작 회전에 그 축 둘레 증분 회전을 곱해 오일러로 다시 푼다(다른 성분이 0 이 아니어도 맞다)
                        const float3 rotation = ModifierGizmoLogic::applyRotationDelta( _startRotation, handle._axis, angle );
                        if ( ( rotation - gizmo._rotationDegree ).getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session );
                        gizmo._rotationDegree = rotation;
                        break;
                    }
                    case MeshModifiersVisualizerInternal::DragKind::PointAxis:
                    case MeshModifiersVisualizerInternal::DragKind::PointFree:
                    {
                        if ( _pointIndex < 0 || static_cast<size_t>( _pointIndex ) >= set.getWorldPoints().size() )
                            return;
                        float3 worldDelta{ 0.0f, 0.0f, 0.0f };
                        if ( handle._kind == MeshModifiersVisualizerInternal::DragKind::PointAxis )
                        {
                            const float3  unit      = handle._direction.normalize();
                            const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, unit );
                            worldDelta              = unit * ( parameter - _startParameter );
                        }
                        else if ( ModifierGizmoLogic::tryComputePlaneDragDelta( _startRay._origin, _startRay._direction, ray._origin, ray._direction,
                                                                                handle._origin, _startRay._direction, worldDelta ) == false )
                        {
                            return;
                        }
                        const float3 total     = set.toNormalizedDelta( frame, worldDelta );
                        const float3 increment = total - _appliedDelta;
                        if ( increment.getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session );
                        set.movePoint( session, _pointIndex, increment );
                        _appliedDelta = total;
                        break;
                    }
                    case MeshModifiersVisualizerInternal::DragKind::None:
                    {
                        return;
                    }
                }
                session.markDirty();
            }

            /** @brief 손잡이 @p handle 을 집습니다. 놓을 때까지 뷰포트 마우스를 가져간다. */
            void begin( const ModifierViewHandle& handle, const ModifierViewFrame& frame, int32 modifierIndex, int32 pointIndex, const Ray& ray )
            {
                end();
                // 핸들을 집는 순간부터 놓을 때까지 뷰포트 선택 · 카메라가 마우스를 가져가지 않게 한다(호버만으로는 잡지 않는다)
                EditorViewportMouseClaim::acquire( this );
                _bDragging     = SW_TRUE;
                _handle        = handle;
                _startRay      = ray;
                _modifierIndex = modifierIndex;
                _pointIndex    = pointIndex;
                _startCenter   = frame._pModifier->getGizmo()._center;
                _startRotation = frame._pModifier->getGizmo()._rotationDegree;
                if ( handle._kind == MeshModifiersVisualizerInternal::DragKind::GizmoMove )
                {
                    const float3 axisWorld = handle._direction / MathUtil::max( frame._worldSize * MeshModifiersVisualizerInternal::kGizmoSizeRatio, MathUtil::kEpsilon );
                    _startParameter        = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                }
                else if ( handle._kind == MeshModifiersVisualizerInternal::DragKind::PointAxis )
                {
                    _startParameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, handle._direction.normalize() );
                }
                else if ( handle._kind == MeshModifiersVisualizerInternal::DragKind::GizmoRotate )
                {
                    float3 hit{ 0.0f, 0.0f, 0.0f };
                    if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, handle._origin, handle._direction, hit ) )
                        _startVector = hit - handle._origin;
                }
            }

        private:
            void ensureUndo( MeshModifierSession& session )
            {
                if ( _bUndoRecorded )
                    return;
                session.recordUndo();
                _bUndoRecorded = SW_TRUE;
            }

        private:
            ModifierViewHandle _handle;
            Ray                _startRay;
            float3             _startCenter;   ///< 기즈모 중심(기준 공간)
            float3             _startRotation; ///< 기즈모 오일러 각(도)
            float3             _startVector;   ///< 회전 고리 평면 위 시작 방향
            float3             _appliedDelta;  ///< 제어점에 이미 건 정규 좌표 이동량
            float32            _startParameter;
            int32              _modifierIndex;
            int32              _pointIndex;
            uint8              _bDragging     : 1;
            uint8              _bUndoRecorded : 1;
        };

        /**
         * @class MeshModifiersVisualizerState
         * @brief 뷰포트 끌기 상태 · 프레임 재사용 버퍼와 그것을 쓰는 그리기입니다(확장이 기여하는 에디터 서브시스템 — `SW_EDITOR_SUBSYSTEM`). 에디터 범위마다 하나이고 모듈이 내려가면 함께 사라집니다.
         */
        class MeshModifiersVisualizerState final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "MeshModifiersVisualizerState";

            MeshModifiersVisualizerState()
                : _drag{}
                , _listHandle{}
                , _listSegmentPoint{}
                , _listStagePoint{}
                , _pointSet{}
            {
            }

            const utf8* getSubsystemName() const override { return kSubsystemName; }

            /** @brief 시각화 표의 `meshmodifiers.preview` 진입점입니다. */
            static void drawPreviewOfScope( const EditorViewportVisualizerArgs& args ) { getSubsystem<MeshModifiersVisualizerState>().drawPreview( args ); }
            /** @brief 시각화 표의 `meshmodifiers.stage` 진입점입니다. */
            static void drawStageOfScope( const EditorViewportVisualizerArgs& args ) { getSubsystem<MeshModifiersVisualizerState>().drawStage( args ); }

            /** @brief 변형 미리보기 · 범위 · 손잡이 · 제어점을 그리고 끌기를 처리합니다(`meshmodifiers.preview`). */
            void drawPreview( const EditorViewportVisualizerArgs& args )
            {
                MeshModifierSession* pSession      = findSubsystem<MeshModifierSession>();
                const bool           bNoDragTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoDragTarget )
                {
                    _drag.end();
                    return;
                }
                MeshModifierSession& session = *pSession;
                session.refreshPreview();

                session.makePreviewSegments( _listSegmentPoint ); // 프레임마다 다시 채우는 재사용 버퍼
                MeshModifiersVisualizerInternal::drawSegments( args, _listSegmentPoint, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierPreview" ) ) );

                MeshModifier*     pModifier = session.getStack().findModifier( session.getSelectedIndex() );
                ModifierViewFrame frame{};
                if ( pModifier == nullptr || frame.build( session, *pModifier ) == false )
                {
                    _drag.end();
                    return;
                }

                // 주축 선(기존 표시)과 범위
                float3 axisDirection{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( axisDirection, frame._axisIndex, frame._worldSize * 0.6f );
                const float3 axisWorld = float3::transformVector( axisDirection, frame._modifierToWorld );
                MeshModifiersVisualizerInternal::drawSegment( args, frame._centerWorld - axisWorld, frame._centerWorld + axisWorld, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierAxis" ) ), 1.0f );
                frame.drawRanges( args );

                (void)_pointSet.build( frame ); // 제어점 월드 목록을 프레임마다 다시 채운다 — 격자 · ffd2d 가 아니면 빈 채로 둔다
                processEditing( args, session, frame );
            }

            /** @brief 고른 모디파이어까지의 결과 선분을 그립니다(`meshmodifiers.stage`). */
            void drawStage( const EditorViewportVisualizerArgs& args )
            {
                MeshModifierSession* pSession        = findSubsystem<MeshModifierSession>();
                const bool           bNoActiveTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoActiveTarget )
                    return;
                pSession->makeStagePreviewSegments( _listStagePoint ); // 프레임마다 다시 채우는 재사용 버퍼
                MeshModifiersVisualizerInternal::drawSegments( args, _listStagePoint, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierStage" ) ) );
            }

        private:
            /** @brief 손잡이 · 제어점을 그리고 마우스로 고르고 끕니다. */
            void processEditing( const EditorViewportVisualizerArgs& args, MeshModifierSession& session, const ModifierViewFrame& frame )
            {
                ModifierPointSet& set           = _pointSet;
                const int32       modifierIndex = session.getSelectedIndex();
                if ( _drag.isDragging() && _drag.getModifierIndex() != modifierIndex )
                    _drag.end(); // 끄는 중에 다른 모디파이어로 바뀌었다

                int32 selectedPoint = session.getSelectedControlPoint();
                if ( set.isValid() && selectedPoint >= static_cast<int32>( set.getWorldPoints().size() ) )
                {
                    selectedPoint = static_cast<int32>( set.getWorldPoints().size() ) - 1;
                    session.setSelectedControlPoint( selectedPoint );
                }

                vector<ModifierViewHandle>& listHandle = _listHandle; // 프레임마다 다시 채우는 재사용 버퍼(UI 스레드 하나만 쓴다)
                listHandle.clear();
                if ( set.isValid() )
                    set.collectHandles( frame, selectedPoint, listHandle );
                frame.collectGizmoHandles( listHandle );

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                Ray        ray{};
                const bool bHasRay = EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );

                if ( _drag.isDragging() )
                {
                    if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) == false )
                        _drag.end(); // 놓았다. 되돌리기는 첫 변경 때 이미 남겼다
                    else
                    {
                        _drag.acquireClaim();
                        if ( bHasRay )
                            _drag.update( session, frame, set, ray );
                    }
                }

                int32      hoveredHandle = -1;
                int32      hoveredPoint  = -1;
                const bool bCanHover     = _drag.isDragging() == false && bMouseInside && bHasRay && args._bGizmoDragging == false;
                if ( bCanHover )
                {
                    float32 bestDistance = 1e30f;
                    for ( size_t index = 0; index < listHandle.size(); ++index )
                    {
                        const ModifierViewHandle& handle   = listHandle[index];
                        const float32             length   = handle._kind == MeshModifiersVisualizerInternal::DragKind::GizmoRotate ? handle._radius : handle._direction.getLength();
                        const float32             distance = handle.computeDistance( ray );
                        if ( distance <= length * MeshModifiersVisualizerInternal::kPickWidthRatio && distance < bestDistance )
                        {
                            bestDistance  = distance;
                            hoveredHandle = static_cast<int32>( index );
                        }
                    }
                    if ( hoveredHandle < 0 && set.isValid() )
                        hoveredPoint = ModifierGizmoLogic::findPointOnRay( ray._origin, ray._direction, set.getWorldPoints(), frame._worldSize * MeshModifiersVisualizerInternal::kPointPickRatio );
                    if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                    {
                        if ( hoveredHandle >= 0 )
                        {
                            _drag.begin( listHandle[static_cast<size_t>( hoveredHandle )], frame, modifierIndex, selectedPoint, ray );
                        }
                        else if ( hoveredPoint >= 0 )
                        {
                            // 점을 누르면 고르고, 그대로 끌면 카메라를 마주 보는 평면에서 옮긴다
                            session.setSelectedControlPoint( hoveredPoint );
                            selectedPoint = hoveredPoint;
                            const ModifierViewHandle freeHandle{
                                MeshModifiersVisualizerInternal::DragKind::PointFree, 0, set.getWorldPoints()[static_cast<size_t>( hoveredPoint )], float3{ 0.0f, 0.0f, 0.0f },
                                0.0f
                            };
                            _drag.begin( freeHandle, frame, modifierIndex, hoveredPoint, ray );
                        }
                    }
                }

                if ( set.isValid() )
                    set.draw( args, selectedPoint, hoveredPoint );
                for ( size_t index = 0; index < listHandle.size(); ++index )
                {
                    const ModifierViewHandle& handle = listHandle[index];
                    handle.draw( args, _drag.isActive( handle ) || static_cast<int32>( index ) == hoveredHandle );
                }
                ModifierViewHandle::recordMarks( args, listHandle );
                ImVec2 centerScreen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, frame._centerWorld, args._canvasPos, args._canvasSize, centerScreen ) )
                    args._pDrawList->AddCircle( centerScreen, 6.0f, MeshModifiersVisualizerInternal::themeColor( SW_THEME_COLOR( "Viewport", "ModifierAxis" ) ), 0, 2.0f );
            }

        private:
            ModifierHandleDrag         _drag;
            vector<ModifierViewHandle> _listHandle;       ///< 이번 프레임에 집을 수 있는 손잡이
            vector<float3>             _listSegmentPoint; ///< 변형 결과 미리보기 선분 끝점
            vector<float3>             _listStagePoint;   ///< 고른 모디파이어까지의 결과 선분 끝점
            ModifierPointSet           _pointSet;         ///< 제어점 월드 목록
        };
    } // namespace

    SW_EDITOR_SUBSYSTEM( MeshModifiersVisualizerState, 9500 );

    SW_EDITOR_VISUALIZER( MeshModifiers, "meshmodifiers.preview", 910, SW_EDITOR_ICON_NAME( "Cube" ), SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersLabel" ),
                          SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersTip" ), true, &MeshModifiersVisualizerState::drawPreviewOfScope );
    SW_EDITOR_VISUALIZER( MeshModifiersStage, "meshmodifiers.stage", 911, ::sw::editor::EditorIconName{}, SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersStageLabel" ), SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersStageTip" ), false,
                          &MeshModifiersVisualizerState::drawStageOfScope );
} // namespace sw::editor
