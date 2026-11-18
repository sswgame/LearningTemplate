// 뷰포트 Modifiers 화면 도구의 Model 쪽(모으기 · 판정 · 명령 순서 · 적용, ImGui 없음)입니다. 그리기와 마우스는 `MeshModifiersVisualizer.cpp`(MVVM View)다.
#include "pch.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Model/MeshModifierHandleEdit.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/Model/EditorViewportMouseCapture.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/ModifierGizmoLogic.h"

#include <cmath>

namespace sw::editor
{
    namespace
    {
        struct MeshModifierHandleEditInternal
        {
            static constexpr uint32      kRingSegmentCount = 48;
            static constexpr float32     kPickWidthRatio   = 0.06f; ///< 손잡이 길이 대비 집는 두께
            static constexpr float32     kPointPickRatio   = 0.04f; ///< 원본 상자 대각선(월드) 대비 제어점 구 반경
            static constexpr float32     kGizmoSizeRatio   = 0.35f; ///< 원본 상자 대각선(월드) 대비 기즈모 손잡이 길이
            static constexpr float32     kPointHandleRatio = 0.15f; ///< 원본 상자 대각선(월드) 대비 제어점 축 손잡이 길이
            static constexpr float32     kAxisLineRatio    = 0.6f;  ///< 원본 상자 대각선(월드) 대비 주축 선 반 길이
            static constexpr float32     kFarDistance      = 1e30f; ///< 집을 수 없는 손잡이의 거리
            static constexpr float32     kRingRadiusRatio  = 0.8f;  ///< 이동 화살표 길이 대비 회전 고리 반경
            static constexpr float32     kMinWorldSize     = 0.1f;  ///< 원본 상자 대각선(월드)의 하한 — 손잡이가 사라지지 않게
            static constexpr float32     kLimitThickness   = 1.5f;  ///< 하한 · 상한 사각형 선 두께(픽셀)
            static constexpr const utf8* kFFD2DTypeID      = "ffd2d";

            static float3 makeUnit( uint32 axisIndex )
            {
                float3 unit{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( unit, axisIndex, 1.0f );
                return unit;
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
                const float3 modifierDirection = _pModifier->getGizmo().toModifierDirection( MeshModifierHandleEditInternal::makeUnit( axisIndex ) );
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
                _worldSize            = MathUtil::max( ( worldMax - worldMin ).getLength(), MeshModifierHandleEditInternal::kMinWorldSize );
                return true;
            }

            float3 toWorld( const float3& modifierPoint ) const
            {
                return float3::transform( modifierPoint, _modifierToWorld );
            }

            /** @brief 모디파이어 범위 매개변수(반경 · 한계 · 중심 · 방향)를 월드 도형 값으로 냅니다. */
            void collectRanges( vector<MeshModifierRangeShape>& outListRange ) const
            {
                using Internal               = MeshModifierHandleEditInternal;
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
                const float32  arrowLength = _worldSize * Internal::kGizmoSizeRatio;
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
                                const float3 axisU    = Internal::makeUnit( ( plane + 1 ) % 3 );
                                const float3 axisV    = Internal::makeUnit( ( plane + 2 ) % 3 );
                                float3       previous = toWorld( center + axisU * radius );
                                for ( uint32 segment = 1; segment <= Internal::kRingSegmentCount; ++segment )
                                {
                                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( Internal::kRingSegmentCount );
                                    const float3  next  = toWorld( center + axisU * ( ::cosf( angle ) * radius ) + axisV * ( ::sinf( angle ) * radius ) );
                                    outListRange.push_back( MeshModifierRangeShape{ MeshModifierRangeShapeKind::Segment, previous, next, 1.0f } );
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
                                outListRange.push_back( MeshModifierRangeShape{ MeshModifierRangeShapeKind::Segment, toWorld( listCorner[corner] ),
                                                                                toWorld( listCorner[( corner + 1 ) % listCorner.size()] ), Internal::kLimitThickness } );
                            }
                            break;
                        }
                        case ModifierRangeKind::Center:
                        {
                            const float3 point = toWorld( modifier.getVector3( index ) );
                            outListRange.push_back( MeshModifierRangeShape{ MeshModifierRangeShapeKind::Marker, point, point, 2.0f } );
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
                            outListRange.push_back( MeshModifierRangeShape{ MeshModifierRangeShapeKind::Arrow, worldFrom, worldFrom + worldStep.normalize() * arrowLength, 2.0f } );
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
            void collectGizmoHandles( vector<MeshModifierHandle>& outListHandle ) const
            {
                const float32 length = _worldSize * MeshModifierHandleEditInternal::kGizmoSizeRatio;
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    const float3 axisWorld = computeBaseAxisWorld( axis );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    const float3 unit = axisWorld.normalize();
                    outListHandle.push_back( MeshModifierHandle{ MeshModifierHandleKind::GizmoMove, axis, _centerWorld, unit * length, 0.0f } );
                    outListHandle.push_back( MeshModifierHandle{ MeshModifierHandleKind::GizmoRotate, axis, _centerWorld, unit, length * MeshModifierHandleEditInternal::kRingRadiusRatio } );
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
            int32                 getCountX() const { return _countX; }
            int32                 getCountY() const { return _countY; }
            int32                 getCountZ() const { return _countZ; }

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
                if ( string_view( modifier.getTypeID() ) != string_view( MeshModifierHandleEditInternal::kFFD2DTypeID ) )
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
            void collectHandles( const ModifierViewFrame& frame, int32 selectedPoint, vector<MeshModifierHandle>& outListHandle ) const
            {
                if ( selectedPoint < 0 || static_cast<size_t>( selectedPoint ) >= _listWorld.size() )
                    return;
                const float32 length    = frame._worldSize * MeshModifierHandleEditInternal::kPointHandleRatio;
                const uint32  axisCount = _pLattice != nullptr ? 3u : 2u; // ffd2d 는 XY 만 옮긴다
                for ( uint32 axis = 0; axis < axisCount; ++axis )
                {
                    const float3 axisWorld = float3::transformVector( MeshModifierHandleEditInternal::makeUnit( axis ), frame._modifierToWorld );
                    if ( axisWorld.getLengthSquared() <= MathUtil::kEpsilonSquared )
                        continue;
                    outListHandle.push_back(
                        MeshModifierHandle{ MeshModifierHandleKind::PointAxis, axis, _listWorld[static_cast<size_t>( selectedPoint )], axisWorld.normalize() * length, 0.0f } );
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

        /** @brief 끌기 하나의 상태입니다. 뷰포트 마우스 양보의 주인 식별자는 이 객체의 주소다(에디터 범위마다 다르다). */
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
                , _modifierIndex{ MeshModifierHandleViewState::kNoIndex }
                , _pointIndex{ MeshModifierHandleViewState::kNoIndex }
                , _bDragging{ SW_FALSE }
                , _bUndoRecorded{ SW_FALSE }
            {
            }

            bool                      isDragging() const { return _bDragging == SW_TRUE; }
            int32                     getModifierIndex() const { return _modifierIndex; }
            const MeshModifierHandle& getHandle() const { return _handle; }

            /** @brief 끌기를 끝냅니다. 핸들을 집고 있었으면 뷰포트 마우스 양보도 돌려준다(놓기 · 취소 모두). */
            void end()
            {
                if ( _bDragging )
                    EditorViewportMouseCapture::release( this );
                *this = ModifierHandleDrag{};
            }

            /** @brief 양보는 매 프레임 다시 잡아야 유지된다 — 끄는 중에 시각화 토글을 끄면 도구가 안 불려 다음 프레임 시작에 뷰포트가 거둔다. */
            void acquireClaim() const { EditorViewportMouseCapture::acquire( this ); }

            /** @brief 끄는 중인 손잡이를 지금 레이로 옮깁니다. */
            void update( MeshModifierSession& session, const ModifierViewFrame& frame, ModifierPointSet& set, const Ray& ray )
            {
                using Internal                   = MeshModifierHandleEditInternal;
                MeshModifierGizmo&        gizmo  = frame._pModifier->getGizmo();
                const MeshModifierHandle& handle = _handle;
                switch ( handle._kind )
                {
                    case MeshModifierHandleKind::GizmoMove:
                    {
                        // 손잡이 방향은 기준 공간 길이 1 의 월드 벡터라 매개변수 차이가 곧 기준 공간 이동량이다
                        const float3  axisWorld = handle._direction / MathUtil::max( frame._worldSize * Internal::kGizmoSizeRatio, MathUtil::kEpsilon );
                        const float3  baseAxis  = frame.computeBaseAxisWorld( handle._axis );
                        const float32 baseScale = MathUtil::max( baseAxis.getLength(), MathUtil::kEpsilon );
                        const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                        float3        center    = _startCenter;
                        MeshModifierUtil::setComponent( center, handle._axis, MeshModifierUtil::getComponent( center, handle._axis ) + ( parameter - _startParameter ) / baseScale );
                        if ( ( center - gizmo._center ).getLengthSquared() <= MathUtil::kEpsilonSquared )
                            return;
                        ensureUndo( session );
                        gizmo._center = center;
                        break;
                    }
                    case MeshModifierHandleKind::GizmoRotate:
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
                    case MeshModifierHandleKind::PointAxis:
                    case MeshModifierHandleKind::PointFree:
                    {
                        if ( _pointIndex < 0 || static_cast<size_t>( _pointIndex ) >= set.getWorldPoints().size() )
                            return;
                        float3 worldDelta{ 0.0f, 0.0f, 0.0f };
                        if ( handle._kind == MeshModifierHandleKind::PointAxis )
                        {
                            const float3  unit      = handle._direction.normalize();
                            const float32 parameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, unit );
                            worldDelta              = unit * ( parameter - _startParameter );
                        }
                        else if ( ModifierGizmoLogic::tryComputePlaneDragDelta( _startRay._origin, _startRay._direction, ray._origin, ray._direction, handle._origin,
                                                                                _startRay._direction, worldDelta ) == false )
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
                    case MeshModifierHandleKind::None:
                    {
                        return;
                    }
                }
                session.markDirty();
            }

            /** @brief 손잡이 @p handle 을 집습니다. 놓을 때까지 뷰포트 마우스를 가져간다. */
            void begin( const MeshModifierHandle& handle, const ModifierViewFrame& frame, int32 modifierIndex, int32 pointIndex, const Ray& ray )
            {
                end();
                // 핸들을 집는 순간부터 놓을 때까지 뷰포트 선택 · 카메라가 마우스를 가져가지 않게 한다(호버만으로는 잡지 않는다)
                EditorViewportMouseCapture::acquire( this );
                _bDragging     = SW_TRUE;
                _handle        = handle;
                _startRay      = ray;
                _modifierIndex = modifierIndex;
                _pointIndex    = pointIndex;
                _startCenter   = frame._pModifier->getGizmo()._center;
                _startRotation = frame._pModifier->getGizmo()._rotationDegree;
                if ( handle._kind == MeshModifierHandleKind::GizmoMove )
                {
                    const float3 axisWorld = handle._direction / MathUtil::max( frame._worldSize * MeshModifierHandleEditInternal::kGizmoSizeRatio, MathUtil::kEpsilon );
                    _startParameter        = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, axisWorld );
                }
                else if ( handle._kind == MeshModifierHandleKind::PointAxis )
                {
                    _startParameter = ModifierGizmoLogic::computeClosestLineParameter( ray._origin, ray._direction, handle._origin, handle._direction.normalize() );
                }
                else if ( handle._kind == MeshModifierHandleKind::GizmoRotate )
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
            MeshModifierHandle _handle;
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
         * @brief 화면 도구의 끌기 상태 · 지난 모으기의 공간 정보 · 프레임 재사용 버퍼입니다(확장이 기여하는 에디터 서브시스템 — `SW_EDITOR_SUBSYSTEM`). 에디터 범위마다 하나이고 모듈이 내려가면 함께 사라집니다.
         * @details 모으기와 적용은 같은 프레임의 화면 도구 안에서 잇따라 돈다 — 적용은 모으기가 남긴 공간 정보(`_frame` · `_pointSet`)를 쓴다(그 사이 씬이 바뀌지 않는다).
         */
        class MeshModifiersVisualizerState final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName  = "MeshModifiersVisualizerState";
            static constexpr int32       kSubsystemOrder = 9500; ///< 에디터 서브시스템 등록 순서

            MeshModifiersVisualizerState()
                : _drag{}
                , _frame{}
                , _pointSet{}
                , _state{}
                , _listSegmentPoint{}
                , _listStagePoint{}
                , _bFrameValid{ SW_FALSE }
            {
            }

            const utf8* getSubsystemName() const override { return kSubsystemName; }

            /**
             * @brief 대상의 결과 선분을 도형 값으로 모읍니다 — @p bStage 면 고른 모디파이어까지(`meshmodifiers.stage`), 아니면 전체 변형 미리보기(`meshmodifiers.preview`, CPU 미리보기를 먼저 갱신한다).
             * @details 그리기는 `EditorViewportVisualizer::drawAll` 이 한다. 손잡이 · 제어점 끌기는 화면 도구다.
             */
            void gatherSegments( bool bStage, EditorVisualizerDrawList& outDrawList )
            {
                MeshModifierSession* pSession        = findSubsystem<MeshModifierSession>();
                const bool           bNoActiveTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoActiveTarget )
                    return;
                vector<float3>& listPoint = bStage ? _listStagePoint : _listSegmentPoint; // 프레임마다 다시 채우는 재사용 버퍼
                if ( bStage )
                    pSession->makeStagePreviewSegments( listPoint );
                else
                {
                    pSession->refreshPreview();
                    pSession->makePreviewSegments( listPoint );
                }
                EditorVisualizerLine prototype{};
                prototype._thickness = 1.0f;
                prototype._colorRole = EditorVisualizerColorRole::ThemeToken;
                prototype._pThemeKey = bStage ? "Viewport.ModifierStage" : "Viewport.ModifierPreview";
                for ( size_t index = 0; index + 1 < listPoint.size(); index += 2 )
                {
                    EditorVisualizerLine line = prototype;
                    line._from                = listPoint[index];
                    line._to                  = listPoint[index + 1];
                    outDrawList.addLine( line );
                }
            }

            /** @brief 고른 모디파이어의 범위 · 손잡이 · 제어점을 값으로 모읍니다. 공간 정보는 이어지는 적용이 쓰도록 남긴다. */
            const MeshModifierHandleViewState& gather()
            {
                using Internal                        = MeshModifierHandleEditInternal;
                MeshModifierHandleViewState& outState = _state;
                outState._listRange.clear();
                outState._listHandle.clear();
                outState._listPoint.clear();
                outState._bAvailable        = false;
                outState._bPointSet         = false;
                outState._bDragging         = _drag.isDragging();
                outState._active            = _drag.getHandle();
                outState._dragModifierIndex = _drag.getModifierIndex();
                _bFrameValid                = SW_FALSE;

                MeshModifierSession* pSession  = findSubsystem<MeshModifierSession>();
                const bool           bNoTarget = pSession == nullptr || pSession->hasTarget() == false || pSession->findTarget() == nullptr;
                if ( bNoTarget )
                    return outState;
                MeshModifierSession& session   = *pSession;
                MeshModifier*        pModifier = session.getStack().findModifier( session.getSelectedIndex() );
                _frame                         = ModifierViewFrame{};
                if ( pModifier == nullptr || _frame.build( session, *pModifier ) == false )
                    return outState;
                _bFrameValid = SW_TRUE;

                // 주축 선(기존 표시)과 범위
                float3 axisDirection{ 0.0f, 0.0f, 0.0f };
                MeshModifierUtil::setComponent( axisDirection, _frame._axisIndex, _frame._worldSize * Internal::kAxisLineRatio );
                const float3 axisWorld = float3::transformVector( axisDirection, _frame._modifierToWorld );
                outState._axisFrom     = _frame._centerWorld - axisWorld;
                outState._axisTo       = _frame._centerWorld + axisWorld;
                outState._centerWorld  = _frame._centerWorld;
                _frame.collectRanges( outState._listRange );

                (void)_pointSet.build( _frame ); // 제어점 월드 목록을 프레임마다 다시 채운다 — 격자 · ffd2d 가 아니면 빈 채로 둔다
                int32 selectedPoint = session.getSelectedControlPoint();
                if ( _pointSet.isValid() && selectedPoint >= static_cast<int32>( _pointSet.getWorldPoints().size() ) )
                {
                    selectedPoint = static_cast<int32>( _pointSet.getWorldPoints().size() ) - 1;
                    session.setSelectedControlPoint( selectedPoint );
                }
                if ( _pointSet.isValid() )
                {
                    _pointSet.collectHandles( _frame, selectedPoint, outState._listHandle );
                    outState._listPoint = _pointSet.getWorldPoints();
                    outState._countX    = _pointSet.getCountX();
                    outState._countY    = _pointSet.getCountY();
                    outState._countZ    = _pointSet.getCountZ();
                }
                _frame.collectGizmoHandles( outState._listHandle );
                outState._pointPickRadius = _frame._worldSize * Internal::kPointPickRatio;
                outState._modifierIndex   = session.getSelectedIndex();
                outState._selectedPoint   = selectedPoint;
                outState._bPointSet       = _pointSet.isValid();
                outState._bAvailable      = true;
                return outState;
            }

            /** @brief 명령들을 차례로 적용합니다. 공간 정보가 없으면(대상이 없다) 놓기만 한다. */
            void apply( const vector<MeshModifierHandleCommand>& listCommand )
            {
                MeshModifierSession* pSession = findSubsystem<MeshModifierSession>();
                for ( const MeshModifierHandleCommand& command : listCommand )
                {
                    if ( command._kind == MeshModifierHandleCommandKind::End || _bFrameValid == SW_FALSE || pSession == nullptr )
                    {
                        _drag.end(); // 놓았다. 되돌리기는 첫 변경 때 이미 남겼다
                        continue;
                    }
                    if ( command._kind == MeshModifierHandleCommandKind::Drag )
                    {
                        _drag.acquireClaim();
                        if ( command._bHasRay )
                            _drag.update( *pSession, _frame, _pointSet, command._ray );
                        continue;
                    }
                    if ( command._bSelectPoint )
                        pSession->setSelectedControlPoint( command._pointIndex );
                    _drag.begin( command._handle, _frame, command._modifierIndex, command._pointIndex, command._ray );
                }
            }

        private:
            ModifierHandleDrag          _drag;
            ModifierViewFrame           _frame;            ///< 지난 모으기의 공간 정보(`_bFrameValid` 일 때만)
            ModifierPointSet            _pointSet;         ///< 지난 모으기의 제어점 월드 목록
            MeshModifierHandleViewState _state;            ///< 지난 모으기 값(프레임마다 다시 채우는 버퍼 — 화면 도구가 읽는다)
            vector<float3>              _listSegmentPoint; ///< 변형 결과 미리보기 선분 끝점
            vector<float3>              _listStagePoint;   ///< 고른 모디파이어까지의 결과 선분 끝점
            uint8                       _bFrameValid : 1;  ///< 지난 모으기가 공간 정보를 풀었다
        };
    } // namespace

    SW_EDITOR_SUBSYSTEM( MeshModifiersVisualizerState, MeshModifiersVisualizerState::kSubsystemOrder );

    float32 MeshModifierHandle::computeDistance( const Ray& ray ) const
    {
        if ( _kind == MeshModifierHandleKind::GizmoRotate )
        {
            float3 hit{ 0.0f, 0.0f, 0.0f };
            if ( ModifierGizmoLogic::tryIntersectRayPlane( ray._origin, ray._direction, _origin, _direction, hit ) == false )
                return MeshModifierHandleEditInternal::kFarDistance;
            return MathUtil::abs( ( hit - _origin ).getLength() - _radius );
        }
        return ModifierGizmoLogic::computeRaySegmentDistance( ray._origin, ray._direction, _origin, _origin + _direction );
    }

    void MeshModifierHandleViewState::pick( const Ray& ray, optional<int32>& outHandle, optional<int32>& outPoint ) const
    {
        outHandle            = nullopt;
        outPoint             = nullopt;
        float32 bestDistance = MeshModifierHandleEditInternal::kFarDistance;
        for ( size_t index = 0; index < _listHandle.size(); ++index )
        {
            const MeshModifierHandle& handle   = _listHandle[index];
            const float32             length   = handle._kind == MeshModifierHandleKind::GizmoRotate ? handle._radius : handle._direction.getLength();
            const float32             distance = handle.computeDistance( ray );
            if ( distance <= length * MeshModifierHandleEditInternal::kPickWidthRatio && distance < bestDistance )
            {
                bestDistance = distance;
                outHandle    = static_cast<int32>( index );
            }
        }
        if ( outHandle.has_value() == false && _bPointSet )
            outPoint = ModifierGizmoLogic::findPointOnRay( ray._origin, ray._direction, _listPoint, _pointPickRadius );
    }

    const MeshModifierHandleViewState& MeshModifierHandleViewState::gather()
    {
        return getSubsystem<MeshModifiersVisualizerState>().gather();
    }

    bool MeshModifierHandleCommand::step( const MeshModifierHandleViewState& state, bool bMouseDown, const Ray* pRay, vector<MeshModifierHandleCommand>& outListCommand )
    {
        if ( state._bDragging == false )
            return false;
        // 대상이 없어졌거나 끄는 중에 다른 모디파이어로 바뀌었거나 단추를 놓았다
        const bool                bEnd = state._bAvailable == false || state._dragModifierIndex != state._modifierIndex || bMouseDown == false;
        MeshModifierHandleCommand command{};
        command._kind = bEnd ? MeshModifierHandleCommandKind::End : MeshModifierHandleCommandKind::Drag;
        if ( bEnd == false && pRay != nullptr )
        {
            command._ray     = *pRay;
            command._bHasRay = true;
        }
        outListCommand.push_back( command );
        return bEnd == false;
    }

    void MeshModifierHandleCommand::press( const MeshModifierHandleViewState& state, optional<int32> hoveredHandle, optional<int32> hoveredPoint, const Ray& ray,
                                           vector<MeshModifierHandleCommand>& outListCommand )
    {
        MeshModifierHandleCommand command{};
        command._kind          = MeshModifierHandleCommandKind::Begin;
        command._ray           = ray;
        command._bHasRay       = true;
        command._modifierIndex = state._modifierIndex;
        if ( hoveredHandle.has_value() && static_cast<size_t>( *hoveredHandle ) < state._listHandle.size() )
        {
            command._handle     = state._listHandle[static_cast<size_t>( *hoveredHandle )];
            command._pointIndex = state._selectedPoint;
            outListCommand.push_back( command );
            return;
        }
        if ( hoveredPoint.has_value() == false || static_cast<size_t>( *hoveredPoint ) >= state._listPoint.size() )
            return;
        // 점을 누르면 고르고, 그대로 끌면 카메라를 마주 보는 평면에서 옮긴다
        command._handle = MeshModifierHandle{
            MeshModifierHandleKind::PointFree, 0, state._listPoint[static_cast<size_t>( *hoveredPoint )], float3{ 0.0f, 0.0f, 0.0f },
            0.0f
        };
        command._pointIndex   = *hoveredPoint;
        command._bSelectPoint = true;
        outListCommand.push_back( command );
    }

    void MeshModifierHandleCommand::apply( const vector<MeshModifierHandleCommand>& listCommand )
    {
        getSubsystem<MeshModifiersVisualizerState>().apply( listCommand );
    }

    void MeshModifierPreviewGatherer::gatherPreview( const EditorViewportVisualizerArgs& /*args*/, EditorVisualizerDrawList& outDrawList )
    {
        getSubsystem<MeshModifiersVisualizerState>().gatherSegments( false, outDrawList );
    }

    void MeshModifierPreviewGatherer::gatherStage( const EditorViewportVisualizerArgs& /*args*/, EditorVisualizerDrawList& outDrawList )
    {
        getSubsystem<MeshModifiersVisualizerState>().gatherSegments( true, outDrawList );
    }
} // namespace sw::editor
