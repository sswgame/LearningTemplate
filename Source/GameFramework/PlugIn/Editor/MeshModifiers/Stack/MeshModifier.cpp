/**
 * @file MeshModifier.cpp
 * @brief 메시 모디파이어 공통 틀 — 공간 변환(로컬 · 월드 · 기준 오브젝트), 선택 가중치 섞기, 야코비안 법선, 매개변수 값 저장을 구현합니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

namespace sw::editor
{
    namespace
    {
        TaskManager* s_pTaskManager = nullptr;

        struct MeshModifierInternal
        {
            /** @brief [0, count) 를 작업 관리자로 나눠 돌립니다. 관리자가 없으면 현재 스레드가 한 번에 돕니다(`engine::runParallel` 과 같은 규칙). */
            static void runBlocks( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
            {
                if ( s_pTaskManager != nullptr )
                {
                    s_pTaskManager->runParallel( count, serialThreshold, body );
                    return;
                }
                if ( count > 0 && body.isBound() )
                    body( 0, count );
            }

            /** @brief 야코비안 차분의 걸음을 상자 대각선에 곱하는 비율입니다. */
            static constexpr float32 kJacobianStepRatio = 1e-3f;
            static constexpr float32 kMinJacobianStep   = 1e-5f;

            static float3 rotateX( const float3& value, float32 radian )
            {
                const float32 cosValue = MathUtil::cos( radian );
                const float32 sinValue = MathUtil::sin( radian );
                return float3{ value._x, value._y * cosValue - value._z * sinValue, value._y * sinValue + value._z * cosValue };
            }

            static float3 rotateY( const float3& value, float32 radian )
            {
                const float32 cosValue = MathUtil::cos( radian );
                const float32 sinValue = MathUtil::sin( radian );
                return float3{ value._x * cosValue + value._z * sinValue, value._y, -value._x * sinValue + value._z * cosValue };
            }

            static float3 rotateZ( const float3& value, float32 radian )
            {
                const float32 cosValue = MathUtil::cos( radian );
                const float32 sinValue = MathUtil::sin( radian );
                return float3{ value._x * cosValue - value._y * sinValue, value._x * sinValue + value._y * cosValue, value._z };
            }

            /** @brief 행 벡터 규약(v · M) 행렬을 기저 세 개와 평행 이동으로 만듭니다. 행 i 가 기저 i 의 상입니다. */
            static float4x4 makeMatrixFromBasis( const float3& axisX, const float3& axisY, const float3& axisZ, const float3& translation )
            {
                return float4x4{ axisX._x,       axisX._y,       axisX._z,       0.0f, axisY._x, axisY._y, axisY._z, 0.0f, axisZ._x, axisZ._y, axisZ._z, 0.0f,
                                 translation._x, translation._y, translation._z, 1.0f };
            }

            /** @brief 병렬 덩어리 하나의 정점 수입니다. 디스패치 비용(수십 us)보다 덩어리 일이 충분히 크게 잡는다. */
            static constexpr uint32 kChunkVertexCount = 4096;

            static uint32 computeChunkCount( size_t vertexCount )
            {
                return static_cast<uint32>( ( vertexCount + kChunkVertexCount - 1 ) / kChunkVertexCount );
            }

            /** @brief 위치(와 법선)를 행렬로 옮기는 병렬 일입니다. 덩어리 번호 구간을 받습니다. */
            struct TransformJob
            {
                float3*  _pPosition{ nullptr };
                float3*  _pNormal{ nullptr }; ///< 없으면 nullptr
                float4x4 _pointMatrix{};
                float4x4 _normalMatrix{};
                size_t   _vertexCount{ 0 };

                void run( uint32 startChunk, uint32 endChunk )
                {
                    const size_t first = static_cast<size_t>( startChunk ) * kChunkVertexCount;
                    const size_t last  = MathUtil::min( static_cast<size_t>( endChunk ) * kChunkVertexCount, _vertexCount );
                    for ( size_t index = first; index < last; ++index )
                    {
                        _pPosition[index] = float3::transform( _pPosition[index], _pointMatrix );
                        if ( _pNormal != nullptr )
                            _pNormal[index] = MeshModifierUtil::normalizeOr( float3::transformVector( _pNormal[index], _normalMatrix ), float3::UnitY );
                    }
                }
            };

            /** @brief 정점마다 `deformPoint` 를 걸고 가중치로 섞고 법선을 야코비안으로 옮기는 병렬 일입니다. 정점끼리 서로 읽지 않으므로 제자리에서 쓴다. */
            struct DeformJob
            {
                const MeshModifier*        _pModifier{ nullptr };
                const MeshModifierContext* _pContext{ nullptr };
                MeshModifierMesh*          _pMesh{ nullptr };
                float32                    _step{ 0.0f };

                void run( uint32 startChunk, uint32 endChunk )
                {
                    MeshModifierMesh& mesh       = *_pMesh;
                    const size_t      first      = static_cast<size_t>( startChunk ) * kChunkVertexCount;
                    const size_t      last       = MathUtil::min( static_cast<size_t>( endChunk ) * kChunkVertexCount, mesh.getVertexCount() );
                    const bool        bHasNormal = mesh.hasNormal();
                    const bool        bHasUV     = mesh.hasUV();
                    const bool        bHasColor  = mesh.hasColor();
                    for ( size_t index = first; index < last; ++index )
                    {
                        const float32 weight = MathUtil::saturate( mesh.getWeight( index ) );
                        if ( weight <= 0.0f )
                            continue;
                        MeshModifierPoint point{};
                        point._position    = mesh._listPosition[index];
                        point._normal      = bHasNormal ? mesh._listNormal[index] : float3::UnitY;
                        point._uv          = bHasUV ? mesh._listUV[index] : float2{ 0.0f, 0.0f };
                        point._color       = bHasColor ? mesh._listColor[index] : float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                        point._vertexIndex = static_cast<uint32>( index );
                        if ( _pContext->_pListOriginalPosition != nullptr )
                        {
                            point._originalPosition     = ( *_pContext->_pListOriginalPosition )[index];
                            point._bHasOriginalPosition = true;
                        }

                        const float3 moved = blend( point._position, _pModifier->deformPoint( point, *_pContext ), weight );
                        mesh._listPosition[index] = moved;
                        if ( bHasNormal == false )
                            continue;

                        // 법선에 수직인 접선 둘을 조금 밀어 변형된 접선을 얻고, 그 외적이 새 법선이다(t1 × t2 = n 이 되게 고른다)
                        const float3      normal   = point._normal;
                        const float3      helper   = MathUtil::abs( normal._x ) < 0.9f ? float3::UnitX : float3::UnitY;
                        const float3      tangent1 = MeshModifierUtil::normalizeOr( normal.cross( helper ), float3::UnitZ );
                        const float3      tangent2 = normal.cross( tangent1 );
                        MeshModifierPoint shifted  = point;
                        shifted._position          = point._position + tangent1 * _step;
                        const float3 edge1         = blend( shifted._position, _pModifier->deformPoint( shifted, *_pContext ), weight ) - moved;
                        shifted._position          = point._position + tangent2 * _step;
                        const float3 edge2         = blend( shifted._position, _pModifier->deformPoint( shifted, *_pContext ), weight ) - moved;
                        mesh._listNormal[index]    = MeshModifierUtil::normalizeOr( edge1.cross( edge2 ), normal );
                    }
                }

                static float3 blend( const float3& from, const float3& to, float32 weight )
                {
                    return from + ( to - from ) * weight;
                }
            };

            static void transformMesh( MeshModifierMesh& inoutMesh, const float4x4& pointMatrix, const float4x4& normalMatrix )
            {
                TransformJob job{};
                job._pPosition    = inoutMesh._listPosition.data();
                job._pNormal      = inoutMesh.hasNormal() ? inoutMesh._listNormal.data() : nullptr;
                job._pointMatrix  = pointMatrix;
                job._normalMatrix = normalMatrix;
                job._vertexCount  = inoutMesh.getVertexCount();
                runBlocks( computeChunkCount( job._vertexCount ), 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &TransformJob::run, &job ) );
            }

            static bool isScalarKind( MeshModifierParamKind kind )
            {
                return kind == MeshModifierParamKind::Float || kind == MeshModifierParamKind::Int || kind == MeshModifierParamKind::Bool ||
                       kind == MeshModifierParamKind::Axis;
            }

            static void makeDefaultValue( const MeshModifierParamDef& def, MeshModifierParamValue& outValue )
            {
                outValue._value = def._default;
                outValue._listCurveKey.clear();
                outValue._listNumber.clear();
                outValue._text.clear();
                if ( def._kind == MeshModifierParamKind::Curve )
                {
                    outValue._listCurveKey.push_back( float2{ 0.0f, def._default._x } );
                    outValue._listCurveKey.push_back( float2{ 1.0f, def._default._y } );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierBounds MeshModifierBounds::makeFromPoints( const vector<float3>& listPoint )
    {
        MeshModifierBounds bounds{};
        if ( listPoint.empty() )
            return bounds;
        bounds._min = listPoint[0];
        bounds._max = listPoint[0];
        for ( const float3& point : listPoint )
        {
            bounds._min = float3{ MathUtil::min( bounds._min._x, point._x ), MathUtil::min( bounds._min._y, point._y ), MathUtil::min( bounds._min._z, point._z ) };
            bounds._max = float3{ MathUtil::max( bounds._max._x, point._x ), MathUtil::max( bounds._max._y, point._y ), MathUtil::max( bounds._max._z, point._z ) };
        }
        return bounds;
    }
} // namespace sw::editor

namespace sw::editor
{
    float3 MeshModifierGizmo::toModifierSpace( const float3& point ) const
    {
        return toModifierDirection( point - _center );
    }

    float3 MeshModifierGizmo::toBaseSpace( const float3& modifierPoint ) const
    {
        return toBaseDirection( modifierPoint ) + _center;
    }

    float3 MeshModifierGizmo::toModifierDirection( const float3& direction ) const
    {
        // 기준 = Rz · Ry · Rx · 모디파이어 이므로 거꾸로 푼다
        float3 value = MeshModifierInternal::rotateZ( direction, -MathUtil::toRadian( _rotationDegree._z ) );
        value        = MeshModifierInternal::rotateY( value, -MathUtil::toRadian( _rotationDegree._y ) );
        return MeshModifierInternal::rotateX( value, -MathUtil::toRadian( _rotationDegree._x ) );
    }

    float3 MeshModifierGizmo::toBaseDirection( const float3& modifierDirection ) const
    {
        float3 value = MeshModifierInternal::rotateX( modifierDirection, MathUtil::toRadian( _rotationDegree._x ) );
        value        = MeshModifierInternal::rotateY( value, MathUtil::toRadian( _rotationDegree._y ) );
        return MeshModifierInternal::rotateZ( value, MathUtil::toRadian( _rotationDegree._z ) );
    }
} // namespace sw::editor

namespace sw::editor
{
    MeshModifier::MeshModifier()
        : _listParamDef{}
        , _listParamValue{}
        , _typeID{}
        , _name{}
        , _spaceReference{}
        , _gizmo{}
        , _bEnabled{ true }
        , _bWorldSpace{ false }
    {
    }

    void MeshModifier::apply( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation ) const
    {
        if ( _bEnabled == false || inoutMesh._listPosition.empty() )
            return;

        // 공간은 여기 한 곳에서만 다룬다 — 모디파이어 공간으로 옮겨 걸고 되돌린다. 파생은 공간을 모른다
        const float4x4 localToModifier       = computeLocalToModifier( evaluation );
        const float4x4 modifierToLocal       = localToModifier.invert();
        const float4x4 localToModifierNormal = modifierToLocal.transpose(); // 법선은 역전치로 옮긴다
        const float4x4 modifierToLocalNormal = localToModifier.transpose();

        MeshModifierContext context{};
        context._pEvaluation     = &evaluation;
        context._axisIndex       = MathUtil::min( _gizmo._axisIndex, 2u );
        context._modifierToWorld = modifierToLocal * evaluation._meshToWorld;
        context._worldToModifier = context._modifierToWorld.invert();

        // 변형 전 원본 위치도 같은 공간 변환으로 모디파이어 공간에 둔다(스택 입력이 없으면 이 모디파이어의 입력 위치)
        vector<float3> listOriginalPosition;
        if ( needsOriginalPosition() )
        {
            const vector<float3>* pListSource = evaluation._pListStackInputPosition;
            if ( pListSource == nullptr || pListSource->size() != inoutMesh._listPosition.size() )
                pListSource = &inoutMesh._listPosition;
            listOriginalPosition.reserve( pListSource->size() );
            for ( const float3& position : *pListSource )
                listOriginalPosition.push_back( float3::transform( position, localToModifier ) );
            context._pListOriginalPosition = &listOriginalPosition;
        }

        MeshModifierInternal::transformMesh( inoutMesh, localToModifier, localToModifierNormal );
        context._bounds = MeshModifierBounds::makeFromPoints( inoutMesh._listPosition );
        applyToMesh( inoutMesh, context );
        MeshModifierInternal::transformMesh( inoutMesh, modifierToLocal, modifierToLocalNormal );
    }

    float3 MeshModifier::deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const
    {
        (void)context;
        return point._position;
    }

    void MeshModifier::applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const
    {
        const float32 diagonal = context._bounds.getSize().getLength();
        MeshModifierInternal::DeformJob job{};
        job._pModifier = this;
        job._pContext  = &context;
        job._pMesh     = &inoutMesh;
        job._step      = MathUtil::max( diagonal * MeshModifierInternal::kJacobianStepRatio, MeshModifierInternal::kMinJacobianStep );
        MeshModifierInternal::runBlocks( MeshModifierInternal::computeChunkCount( inoutMesh.getVertexCount() ), 2,
                                         SW_DELEGATE_METHOD( ParallelBlockDelegate, &MeshModifierInternal::DeformJob::run, &job ) );
    }

    void setMeshModifierTaskManager( TaskManager* pTaskManager )
    {
        s_pTaskManager = pTaskManager;
    }

    void MeshModifier::fitToBounds( const MeshModifierBounds& localBounds )
    {
        if ( localBounds.isValid() == false )
            return;
        _gizmo._center = localBounds.getCenter();
        vector<float3> listCorner;
        listCorner.reserve( 8 );
        for ( uint32 corner = 0; corner < 8; ++corner )
        {
            const float3 local{ ( corner & 1u ) != 0 ? localBounds._max._x : localBounds._min._x, ( corner & 2u ) != 0 ? localBounds._max._y : localBounds._min._y,
                                ( corner & 4u ) != 0 ? localBounds._max._z : localBounds._min._z };
            listCorner.push_back( _gizmo.toModifierSpace( local ) );
        }
        onFitToBounds( MeshModifierBounds::makeFromPoints( listCorner ) );
    }

    void MeshModifier::resetParams()
    {
        for ( size_t index = 0; index < _listParamDef.size(); ++index )
        {
            MeshModifierInternal::makeDefaultValue( _listParamDef[index], _listParamValue[index] );
        }
        onParamsChanged();
    }

    void MeshModifier::writeExtraJSON( const JSONValue& object ) const
    {
        (void)object;
    }

    bool MeshModifier::readExtraJSON( const JSONValue& object )
    {
        (void)object;
        return true;
    }

    void MeshModifier::onParamsChanged() {}

    bool MeshModifier::packGPUParams( vector<float4>& outListValue ) const
    {
        if ( supportsGPU() == false )
            return false;
        for ( size_t index = 0; index < _listParamDef.size(); ++index )
        {
            const MeshModifierParamKind kind  = _listParamDef[index]._kind;
            const float3&               value = _listParamValue[index]._value;
            if ( MeshModifierInternal::isScalarKind( kind ) )
                outListValue.push_back( float4{ value._x, 0.0f, 0.0f, 0.0f } );
            else if ( kind == MeshModifierParamKind::Vector3 )
                outListValue.push_back( float4{ value._x, value._y, value._z, 0.0f } );
        }
        return true;
    }

    void MeshModifier::onFitToBounds( const MeshModifierBounds& modifierBounds )
    {
        (void)modifierBounds;
    }

    float4x4 MeshModifier::computeLocalToModifier( const MeshModifierEvaluation& evaluation ) const
    {
        // 기준 공간 → 모디파이어 공간은 기즈모 행렬(기준 = 모디파이어 · G)의 역이다
        const float4x4 gizmoMatrix = MeshModifierInternal::makeMatrixFromBasis( _gizmo.toBaseDirection( float3::UnitX ), _gizmo.toBaseDirection( float3::UnitY ),
                                                                                _gizmo.toBaseDirection( float3::UnitZ ), _gizmo._center );
        float4x4       result         = gizmoMatrix.invert();
        if ( _bWorldSpace == false )
            return result;

        float4x4 localToBase = evaluation._meshToWorld;
        float4x4 referenceWorld{};
        const bool bHasReference = _spaceReference.empty() == false && evaluation._pInputs != nullptr &&
                                   evaluation._pInputs->tryGetWorldMatrix( _spaceReference, referenceWorld );
        if ( bHasReference )
            localToBase = localToBase * referenceWorld.invert();
        result = localToBase * result;
        return result;
    }

    bool MeshModifier::tryGetParamIndex( string_view name, uint32& outIndex ) const
    {
        for ( size_t index = 0; index < _listParamDef.size(); ++index )
        {
            if ( name == string_view( _listParamDef[index]._pName ) )
            {
                outIndex = static_cast<uint32>( index );
                return true;
            }
        }
        return false;
    }

    int32 MeshModifier::getInt( uint32 paramIndex ) const
    {
        return static_cast<int32>( MathUtil::floor( _listParamValue[paramIndex]._value._x + 0.5f ) );
    }

    uint32 MeshModifier::getAxisIndex( uint32 paramIndex ) const
    {
        const int32 value = getInt( paramIndex );
        return static_cast<uint32>( MathUtil::clamp( value, 0, 2 ) );
    }

    float32 MeshModifier::evaluateCurve( uint32 paramIndex, float32 x ) const
    {
        const vector<float2>& listKey = _listParamValue[paramIndex]._listCurveKey;
        if ( listKey.empty() )
            return 0.0f;
        if ( x <= listKey.front()._x )
            return listKey.front()._y;
        if ( x >= listKey.back()._x )
            return listKey.back()._y;
        for ( size_t index = 1; index < listKey.size(); ++index )
        {
            const float2& right = listKey[index];
            if ( x > right._x )
                continue;
            const float2& left  = listKey[index - 1];
            const float32 width = right._x - left._x;
            if ( width <= MathUtil::kEpsilon )
                return right._y;
            const float32 ratio = ( x - left._x ) / width;
            return left._y + ( right._y - left._y ) * ratio;
        }
        return listKey.back()._y;
    }

    void MeshModifier::setScalar( uint32 paramIndex, float32 value )
    {
        const MeshModifierParamDef& def = _listParamDef[paramIndex];
        if ( MeshModifierInternal::isScalarKind( def._kind ) == false || MathUtil::isFinite( value ) == false )
            return;
        float32 clamped = value;
        if ( def._kind == MeshModifierParamKind::Bool )
            clamped = value != 0.0f ? 1.0f : 0.0f;
        else if ( def._kind == MeshModifierParamKind::Axis )
            clamped = MathUtil::clamp( MathUtil::floor( value + 0.5f ), 0.0f, 2.0f );
        else if ( def._kind == MeshModifierParamKind::Int )
            clamped = MathUtil::clamp( MathUtil::floor( value + 0.5f ), def._min, def._max );
        else
            clamped = MathUtil::clamp( value, def._min, def._max );
        _listParamValue[paramIndex]._value._x = clamped;
    }

    void MeshModifier::setVector3( uint32 paramIndex, const float3& value )
    {
        const MeshModifierParamDef& def = _listParamDef[paramIndex];
        if ( def._kind != MeshModifierParamKind::Vector3 )
            return;
        const bool bFinite = MathUtil::isFinite( value._x ) && MathUtil::isFinite( value._y ) && MathUtil::isFinite( value._z );
        if ( bFinite == false )
            return;
        _listParamValue[paramIndex]._value = float3{ MathUtil::clamp( value._x, def._min, def._max ), MathUtil::clamp( value._y, def._min, def._max ),
                                                     MathUtil::clamp( value._z, def._min, def._max ) };
    }

    void MeshModifier::setCurveKeys( uint32 paramIndex, const vector<float2>& listKey )
    {
        if ( _listParamDef[paramIndex]._kind != MeshModifierParamKind::Curve )
            return;
        vector<float2>& listTarget = _listParamValue[paramIndex]._listCurveKey;
        listTarget.clear();
        for ( const float2& key : listKey )
        {
            if ( MathUtil::isFinite( key._x ) == false || MathUtil::isFinite( key._y ) == false )
                continue;
            const float2 clampedKey{ MathUtil::saturate( key._x ), key._y };
            size_t       insertIndex = listTarget.size();
            while ( insertIndex > 0 && listTarget[insertIndex - 1]._x > clampedKey._x )
            {
                --insertIndex;
            }
            listTarget.insert( listTarget.begin() + static_cast<ptrdiff_t>( insertIndex ), clampedKey );
        }
    }

    void MeshModifier::setNumberList( uint32 paramIndex, const vector<float32>& listNumber )
    {
        const MeshModifierParamDef& def = _listParamDef[paramIndex];
        if ( def._kind != MeshModifierParamKind::NumberList )
            return;
        const size_t     stride  = MathUtil::max<size_t>( def._stride, 1 );
        const size_t     count   = ( listNumber.size() / stride ) * stride;
        vector<float32>& listOut = _listParamValue[paramIndex]._listNumber;
        listOut.clear();
        listOut.reserve( count );
        for ( size_t index = 0; index < count; ++index )
        {
            const float32 value = listNumber[index];
            listOut.push_back( MathUtil::isFinite( value ) ? MathUtil::clamp( value, def._min, def._max ) : 0.0f );
        }
    }

    void MeshModifier::setText( uint32 paramIndex, string_view text )
    {
        const MeshModifierParamKind kind = _listParamDef[paramIndex]._kind;
        if ( kind != MeshModifierParamKind::Reference && kind != MeshModifierParamKind::AssetPath )
            return;
        _listParamValue[paramIndex]._text = string( text );
    }

    uint32 MeshModifier::addParam( const MeshModifierParamDef& def )
    {
        _listParamDef.push_back( def );
        MeshModifierParamValue value{};
        MeshModifierInternal::makeDefaultValue( def, value );
        _listParamValue.push_back( std::move( value ) );
        return static_cast<uint32>( _listParamDef.size() - 1 );
    }
} // namespace sw::editor

namespace sw::editor
{
    float32 MeshModifierUtil::getComponent( const float3& value, uint32 axisIndex )
    {
        if ( axisIndex == 0 )
            return value._x;
        if ( axisIndex == 1 )
            return value._y;
        return value._z;
    }

    void MeshModifierUtil::setComponent( float3& inoutValue, uint32 axisIndex, float32 component )
    {
        if ( axisIndex == 0 )
            inoutValue._x = component;
        else if ( axisIndex == 1 )
            inoutValue._y = component;
        else
            inoutValue._z = component;
    }

    float3 MeshModifierUtil::normalizeOr( const float3& value, const float3& fallback )
    {
        const float32 length = value.getLength();
        if ( length <= MathUtil::kEpsilon || MathUtil::isFinite( length ) == false )
            return fallback;
        return value / length;
    }

    float32 MeshModifierUtil::getAxisLength( const MeshModifierContext& context )
    {
        const float32 length = getComponent( context._bounds.getSize(), context._axisIndex );
        return length > MathUtil::kEpsilon ? length : 1.0f;
    }

    float32 MeshModifierUtil::getAxisFraction( const float3& point, const MeshModifierContext& context )
    {
        const float32 lower = getComponent( context._bounds._min, context._axisIndex );
        return ( getComponent( point, context._axisIndex ) - lower ) / getAxisLength( context );
    }

    void MeshModifierUtil::recomputeFaceNormals( const vector<float3>& listPosition, vector<float3>& inoutListNormal )
    {
        if ( inoutListNormal.size() != listPosition.size() )
            inoutListNormal.assign( listPosition.size(), float3::UnitY );
        const size_t triangleCount = listPosition.size() / 3;
        for ( size_t triangle = 0; triangle < triangleCount; ++triangle )
        {
            const size_t  first  = triangle * 3;
            const float3  edge1  = listPosition[first + 1] - listPosition[first];
            const float3  edge2  = listPosition[first + 2] - listPosition[first];
            const float3  cross  = edge1.cross( edge2 );
            const float32 length = cross.getLength();
            if ( length <= MathUtil::kEpsilon )
                continue;
            const float3 normal = cross / length;
            inoutListNormal[first]     = normal;
            inoutListNormal[first + 1] = normal;
            inoutListNormal[first + 2] = normal;
        }
    }
} // namespace sw::editor
