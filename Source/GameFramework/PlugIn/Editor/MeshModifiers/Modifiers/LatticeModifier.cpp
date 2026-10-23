/**
 * @file LatticeModifier.cpp
 * @brief Sederberg-Parry 격자 FFD 를 구현하고, 사용자 해상도 · 2x2x2 · 3x3x3 · 4x4x4 넷을 등록합니다.
 * @details 변형 = Σ_i Σ_j Σ_k B(i; s) · B(j; t) · B(k; u) · P_ijk, B 는 베른슈타인 기저(차수 = 해상도 - 1). 해상도를 올릴 때는 축마다
 *          차수 올리기(Q_i = i/(m+1) · P_(i-1) + (1 - i/(m+1)) · P_i)를 반복해 변형을 정확히 보존한다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct LatticeModifierInternal
        {
            static constexpr uint32 kMaxCount = static_cast<uint32>( LatticeModifier::kMaxResolution );

            /** @brief 숫자 셋을 @p offset 부터 읽습니다. 숫자가 아닌 칸이 있으면 false 입니다(0 으로 두지 않는다). */
            [[nodiscard]] static bool readPoint( const JSONValue& array, size_t offset, float3& outPoint )
            {
                const std::optional<float32> x        = array.at( offset ).read<float32>();
                const std::optional<float32> y        = array.at( offset + 1 ).read<float32>();
                const std::optional<float32> z        = array.at( offset + 2 ).read<float32>();
                const bool                   bAllRead = x.has_value() && y.has_value() && z.has_value();
                if ( bAllRead == false )
                    return false;
                outPoint = float3{ *x, *y, *z };
                return true;
            }

            /** @brief 차수 @p degree 의 베른슈타인 기저 값 @p degree + 1 개를 채웁니다. */
            static void computeBernstein( int32 degree, float32 t, float32* pOutWeight )
            {
                // 드 카스텔조 방식으로 쌓는다(이항 계수를 따로 구하지 않는다)
                pOutWeight[0]           = 1.0f;
                const float32 oneMinusT = 1.0f - t;
                for ( int32 order = 1; order <= degree; ++order )
                {
                    float32 carry = 0.0f;
                    for ( int32 index = 0; index < order; ++index )
                    {
                        const float32 value = pOutWeight[index];
                        pOutWeight[index]   = carry + oneMinusT * value;
                        carry               = t * value;
                    }
                    pOutWeight[order] = carry;
                }
            }

            static float32 multiplyOrKeep( float32 normalized, float32 lower, float32 size, float32 original )
            {
                return size > MathUtil::kEpsilon ? lower + normalized * size : original;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    LatticeModifier::LatticeModifier( int32 resolution )
        : MeshModifier()
        , _box{}
        , _listControlPoint{}
        , _resolutionXParam{ 0 }
        , _resolutionYParam{ 0 }
        , _resolutionZParam{ 0 }
        , _countX{ 0 }
        , _countY{ 0 }
        , _countZ{ 0 }
    {
        const int32   clamped = MathUtil::clamp( resolution, kMinResolution, kMaxResolution );
        const float32 value   = static_cast<float32>( clamped );
        _resolutionXParam     = addParam( MeshModifierParamDef{
            "Resolution X", MeshModifierParamKind::Int, static_cast<float32>( kMinResolution ),
            static_cast<float32>( kMaxResolution ), float3{ value, 0.0f, 0.0f }
        }
                                              .withLayout( "Lattice", "Control points along X" ) );
        _resolutionYParam     = addParam( MeshModifierParamDef{
            "Resolution Y", MeshModifierParamKind::Int, static_cast<float32>( kMinResolution ),
            static_cast<float32>( kMaxResolution ), float3{ value, 0.0f, 0.0f }
        }
                                              .withLayout( "Lattice", "Control points along Y" ) );
        _resolutionZParam     = addParam( MeshModifierParamDef{
            "Resolution Z", MeshModifierParamKind::Int, static_cast<float32>( kMinResolution ),
            static_cast<float32>( kMaxResolution ), float3{ value, 0.0f, 0.0f }
        }
                                              .withLayout( "Lattice", "Control points along Z" ) );
        _countX               = clamped;
        _countY               = clamped;
        _countZ               = clamped;
        resetToIdentity();
    }

    LatticeModifier::LatticeModifier()
        : LatticeModifier( 3 )
    {
    }

    LatticeModifier* LatticeModifier::findFrom( MeshModifier& modifier )
    {
        const string_view typeID     = modifier.getTypeID();
        const bool        bIsLattice = typeID == "lattice" || typeID == "ffd2" || typeID == "ffd3" || typeID == "ffd4";
        return bIsLattice ? static_cast<LatticeModifier*>( &modifier ) : nullptr;
    }

    float3 LatticeModifier::deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const
    {
        const MeshModifierBounds box  = getEffectiveBox( context._bounds );
        const float3             size = box.getSize();
        const float3             normalized{ size._x > MathUtil::kEpsilon ? ( point._position._x - box._min._x ) / size._x : 0.0f,
                                 size._y > MathUtil::kEpsilon ? ( point._position._y - box._min._y ) / size._y : 0.0f,
                                 size._z > MathUtil::kEpsilon ? ( point._position._z - box._min._z ) / size._z : 0.0f };
        const float3 moved = deformNormalized( normalized );
        return float3{ LatticeModifierInternal::multiplyOrKeep( moved._x, box._min._x, size._x, point._position._x ),
                       LatticeModifierInternal::multiplyOrKeep( moved._y, box._min._y, size._y, point._position._y ),
                       LatticeModifierInternal::multiplyOrKeep( moved._z, box._min._z, size._z, point._position._z ) };
    }

    float3 LatticeModifier::deformNormalized( const float3& normalizedPoint ) const
    {
        float32 arrWeightX[LatticeModifierInternal::kMaxCount];
        float32 arrWeightY[LatticeModifierInternal::kMaxCount];
        float32 arrWeightZ[LatticeModifierInternal::kMaxCount];
        LatticeModifierInternal::computeBernstein( _countX - 1, normalizedPoint._x, arrWeightX );
        LatticeModifierInternal::computeBernstein( _countY - 1, normalizedPoint._y, arrWeightY );
        LatticeModifierInternal::computeBernstein( _countZ - 1, normalizedPoint._z, arrWeightZ );

        float3 result{ 0.0f, 0.0f, 0.0f };
        for ( int32 indexZ = 0; indexZ < _countZ; ++indexZ )
        {
            for ( int32 indexY = 0; indexY < _countY; ++indexY )
            {
                const float32 weightYZ = arrWeightY[indexY] * arrWeightZ[indexZ];
                for ( int32 indexX = 0; indexX < _countX; ++indexX )
                {
                    result += getControlPoint( getControlPointIndex( indexX, indexY, indexZ ) ) * ( arrWeightX[indexX] * weightYZ );
                }
            }
        }
        return result;
    }

    void LatticeModifier::writeExtraJSON( const JSONValue& object ) const
    {
        const JSONValue box = object.set( "box" );
        box.setObject();
        box.set( "valid" ).setBool( _box.isValid() );
        const JSONValue boxMin = box.set( "min" );
        boxMin.setArray();
        boxMin.pushBack().setFloat( static_cast<float64>( _box._min._x ) );
        boxMin.pushBack().setFloat( static_cast<float64>( _box._min._y ) );
        boxMin.pushBack().setFloat( static_cast<float64>( _box._min._z ) );
        const JSONValue boxMax = box.set( "max" );
        boxMax.setArray();
        boxMax.pushBack().setFloat( static_cast<float64>( _box._max._x ) );
        boxMax.pushBack().setFloat( static_cast<float64>( _box._max._y ) );
        boxMax.pushBack().setFloat( static_cast<float64>( _box._max._z ) );

        const JSONValue resolution = object.set( "resolution" );
        resolution.setArray();
        resolution.pushBack().setInt( _countX );
        resolution.pushBack().setInt( _countY );
        resolution.pushBack().setInt( _countZ );
        const JSONValue points = object.set( "controlPoints" );
        points.setArray();
        for ( const float3& point : _listControlPoint )
        {
            points.pushBack().setFloat( static_cast<float64>( point._x ) );
            points.pushBack().setFloat( static_cast<float64>( point._y ) );
            points.pushBack().setFloat( static_cast<float64>( point._z ) );
        }
    }

    bool LatticeModifier::readExtraJSON( const JSONValue& object )
    {
        const JSONValue box = object.get( "box" );
        _box                = MeshModifierBounds{};
        bool bBoxValid      = false;
        if ( box.isObject() && box.readOptionalInto( "valid", bBoxValid ) == false )
            return false;
        if ( bBoxValid )
        {
            const JSONValue boxMin    = box.get( "min" );
            const JSONValue boxMax    = box.get( "max" );
            const bool      bValidBox = boxMin.isArray() && boxMin.size() == 3 && boxMax.isArray() && boxMax.size() == 3 && LatticeModifierInternal::readPoint( boxMin, 0, _box._min ) &&
                                   LatticeModifierInternal::readPoint( boxMax, 0, _box._max );
            if ( bValidBox == false )
            {
                _box = MeshModifierBounds{};
                return false;
            }
        }

        const JSONValue resolution          = object.get( "resolution" );
        const JSONValue points              = object.get( "controlPoints" );
        const bool      bInvalidLatticeData = resolution.isArray() == false || resolution.size() != 3 || points.isArray() == false;
        if ( bInvalidLatticeData )
            return false;
        const std::optional<int32> countX     = resolution.at( 0 ).read<int32>();
        const std::optional<int32> countY     = resolution.at( 1 ).read<int32>();
        const std::optional<int32> countZ     = resolution.at( 2 ).read<int32>();
        const bool                 bCountRead = countX.has_value() && countY.has_value() && countZ.has_value();
        const bool                 bValidSize = bCountRead && kMinResolution <= *countX && *countX <= kMaxResolution && kMinResolution <= *countY && *countY <= kMaxResolution &&
                                kMinResolution <= *countZ && *countZ <= kMaxResolution;
        if ( bValidSize == false || points.size() != static_cast<size_t>( *countX * *countY * *countZ * 3 ) )
        {
            resetToIdentity();
            return false;
        }
        _countX = *countX;
        _countY = *countY;
        _countZ = *countZ;
        _listControlPoint.assign( static_cast<size_t>( _countX * _countY * _countZ ), float3{ 0.0f, 0.0f, 0.0f } );
        for ( size_t index = 0; index < _listControlPoint.size(); ++index )
        {
            if ( LatticeModifierInternal::readPoint( points, index * 3, _listControlPoint[index] ) == false )
            {
                resetToIdentity();
                return false;
            }
        }
        syncResolutionParams();
        return true;
    }

    void LatticeModifier::onParamsChanged()
    {
        setResolution( getInt( _resolutionXParam ), getInt( _resolutionYParam ), getInt( _resolutionZParam ) );
    }

    void LatticeModifier::setResolution( int32 countX, int32 countY, int32 countZ )
    {
        const int32 newX                 = MathUtil::clamp( countX, kMinResolution, kMaxResolution );
        const int32 newY                 = MathUtil::clamp( countY, kMinResolution, kMaxResolution );
        const int32 newZ                 = MathUtil::clamp( countZ, kMinResolution, kMaxResolution );
        const bool  bResolutionUnchanged = newX == _countX && newY == _countY && newZ == _countZ;
        if ( bResolutionUnchanged )
        {
            syncResolutionParams();
            return;
        }

        const bool bOnlyGrows = newX >= _countX && newY >= _countY && newZ >= _countZ;
        if ( bOnlyGrows )
        {
            while ( _countX < newX )
            {
                elevateAxis( 0 );
            }
            while ( _countY < newY )
            {
                elevateAxis( 1 );
            }
            while ( _countZ < newZ )
            {
                elevateAxis( 2 );
            }
        }
        else
        {
            // 차수를 내리면 같은 다항식을 정확히 담을 수 없다 — 옛 변형을 새 항등 격자점에서 표본한다
            vector<float3> listResampled;
            listResampled.reserve( static_cast<size_t>( newX * newY * newZ ) );
            for ( int32 indexZ = 0; indexZ < newZ; ++indexZ )
            {
                for ( int32 indexY = 0; indexY < newY; ++indexY )
                {
                    for ( int32 indexX = 0; indexX < newX; ++indexX )
                    {
                        const float3 identity{ static_cast<float32>( indexX ) / static_cast<float32>( newX - 1 ),
                                               static_cast<float32>( indexY ) / static_cast<float32>( newY - 1 ),
                                               static_cast<float32>( indexZ ) / static_cast<float32>( newZ - 1 ) };
                        listResampled.push_back( deformNormalized( identity ) );
                    }
                }
            }
            _listControlPoint = std::move( listResampled );
            _countX           = newX;
            _countY           = newY;
            _countZ           = newZ;
        }
        syncResolutionParams();
    }

    void LatticeModifier::resetToIdentity()
    {
        _listControlPoint.assign( static_cast<size_t>( _countX * _countY * _countZ ), float3{ 0.0f, 0.0f, 0.0f } );
        for ( int32 index = 0; index < getControlPointCount(); ++index )
        {
            _listControlPoint[static_cast<size_t>( index )] = getIdentityControlPoint( index );
        }
    }

    void LatticeModifier::moveControlPoint( int32 index, const float3& delta )
    {
        if ( index < 0 || getControlPointCount() <= index )
            return;
        _listControlPoint[static_cast<size_t>( index )] += delta;
    }

    void LatticeModifier::moveControlPoints( const vector<int32>& listIndex, const float3& delta, bool bMirror, uint32 mirrorAxisIndex )
    {
        for ( const int32 index : listIndex )
        {
            moveControlPoint( index, delta );
        }
        if ( bMirror == false || mirrorAxisIndex > 2 )
            return;
        float3 mirroredDelta = delta;
        MeshModifierUtil::setComponent( mirroredDelta, mirrorAxisIndex, -MeshModifierUtil::getComponent( delta, mirrorAxisIndex ) );
        for ( const int32 index : listIndex )
        {
            if ( index < 0 || getControlPointCount() <= index )
                continue;
            int32 indexX = index % _countX;
            int32 indexY = ( index / _countX ) % _countY;
            int32 indexZ = index / ( _countX * _countY );
            if ( mirrorAxisIndex == 0 )
                indexX = _countX - 1 - indexX;
            else if ( mirrorAxisIndex == 1 )
                indexY = _countY - 1 - indexY;
            else
                indexZ = _countZ - 1 - indexZ;
            const int32 mirrorIndex = getControlPointIndex( indexX, indexY, indexZ );
            bool        bSelected   = mirrorIndex == index;
            for ( const int32 other : listIndex )
            {
                if ( other == mirrorIndex )
                    bSelected = true;
            }
            if ( bSelected == false )
                moveControlPoint( mirrorIndex, mirroredDelta );
        }
    }

    int32 LatticeModifier::getControlPointIndex( int32 indexX, int32 indexY, int32 indexZ ) const
    {
        return indexX + _countX * ( indexY + _countY * indexZ );
    }

    void LatticeModifier::setControlPoint( int32 index, const float3& normalizedPosition )
    {
        if ( index < 0 || getControlPointCount() <= index )
            return;
        _listControlPoint[static_cast<size_t>( index )] = normalizedPosition;
    }

    float3 LatticeModifier::getIdentityControlPoint( int32 index ) const
    {
        const int32 indexX = index % _countX;
        const int32 indexY = ( index / _countX ) % _countY;
        const int32 indexZ = index / ( _countX * _countY );
        return float3{ static_cast<float32>( indexX ) / static_cast<float32>( _countX - 1 ), static_cast<float32>( indexY ) / static_cast<float32>( _countY - 1 ),
                       static_cast<float32>( indexZ ) / static_cast<float32>( _countZ - 1 ) };
    }

    MeshModifierBounds LatticeModifier::getEffectiveBox( const MeshModifierBounds& fallback ) const
    {
        return _box.isValid() ? _box : fallback;
    }

    void LatticeModifier::onFitToBounds( const MeshModifierBounds& modifierBounds )
    {
        _box = modifierBounds;
    }

    void LatticeModifier::elevateAxis( uint32 axisIndex )
    {
        const int32    oldX    = _countX;
        const int32    oldY    = _countY;
        const int32    oldZ    = _countZ;
        const int32    newX    = axisIndex == 0 ? oldX + 1 : oldX;
        const int32    newY    = axisIndex == 1 ? oldY + 1 : oldY;
        const int32    newZ    = axisIndex == 2 ? oldZ + 1 : oldZ;
        const int32    degree  = ( axisIndex == 0 ? oldX : ( axisIndex == 1 ? oldY : oldZ ) ) - 1; // 옛 차수 m
        const float32  divisor = static_cast<float32>( degree + 1 );
        vector<float3> listElevated( static_cast<size_t>( newX * newY * newZ ), float3{ 0.0f, 0.0f, 0.0f } );
        for ( int32 indexZ = 0; indexZ < newZ; ++indexZ )
        {
            for ( int32 indexY = 0; indexY < newY; ++indexY )
            {
                for ( int32 indexX = 0; indexX < newX; ++indexX )
                {
                    const int32   along = axisIndex == 0 ? indexX : ( axisIndex == 1 ? indexY : indexZ );
                    const float32 ratio = static_cast<float32>( along ) / divisor;
                    float3        value{ 0.0f, 0.0f, 0.0f };
                    if ( along > 0 )
                    {
                        const int32 prevX = axisIndex == 0 ? indexX - 1 : indexX;
                        const int32 prevY = axisIndex == 1 ? indexY - 1 : indexY;
                        const int32 prevZ = axisIndex == 2 ? indexZ - 1 : indexZ;
                        value += _listControlPoint[static_cast<size_t>( prevX + oldX * ( prevY + oldY * prevZ ) )] * ratio;
                    }
                    if ( along <= degree )
                        value += _listControlPoint[static_cast<size_t>( indexX + oldX * ( indexY + oldY * indexZ ) )] * ( 1.0f - ratio );
                    listElevated[static_cast<size_t>( indexX + newX * ( indexY + newY * indexZ ) )] = value;
                }
            }
        }
        _listControlPoint = std::move( listElevated );
        _countX           = newX;
        _countY           = newY;
        _countZ           = newZ;
    }

    void LatticeModifier::syncResolutionParams()
    {
        setScalar( _resolutionXParam, static_cast<float32>( _countX ) );
        setScalar( _resolutionYParam, static_cast<float32>( _countY ) );
        setScalar( _resolutionZParam, static_cast<float32>( _countZ ) );
    }
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 2x2x2 격자(삼선형)입니다. */
    class LatticeFFD2Modifier final : public LatticeModifier
    {
    public:
        LatticeFFD2Modifier()
            : LatticeModifier( 2 )
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 3x3x3 격자(2차)입니다. */
    class LatticeFFD3Modifier final : public LatticeModifier
    {
    public:
        LatticeFFD3Modifier()
            : LatticeModifier( 3 )
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 4x4x4 격자(3차)입니다. */
    class LatticeFFD4Modifier final : public LatticeModifier
    {
    public:
        LatticeFFD4Modifier()
            : LatticeModifier( 4 )
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    SW_MESH_MODIFIER( LatticeModifier, "lattice", "Lattice (Custom)", "Lattice" );
    SW_MESH_MODIFIER( LatticeFFD2Modifier, "ffd2", "FFD 2x2x2", "Lattice" );
    SW_MESH_MODIFIER( LatticeFFD3Modifier, "ffd3", "FFD 3x3x3", "Lattice" );
    SW_MESH_MODIFIER( LatticeFFD4Modifier, "ffd4", "FFD 4x4x4", "Lattice" );
} // namespace sw::editor
