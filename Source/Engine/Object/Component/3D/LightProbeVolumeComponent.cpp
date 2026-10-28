#include "pch.h"

#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "LightProbeVolume" );

    namespace
    {
        struct LightProbeVolumeComponentInternal
        {
            /// @brief 기본 상자와 간격 — 방 하나 크기에 2 m 간격(유니티 APV 의 기본 최소 간격 1 m 보다 성기게, 학습용 씬의 프로브 수를 작게).
            static constexpr float3  kDefaultExtent{ 10.0f, 4.0f, 10.0f };
            static constexpr float32 kDefaultSpacing{ 2.0f };
            /// @brief 기본 베이크 — 프로브마다 레이 256 개(SH9 의 9 계수에 넉넉한 표본), 닿은 면의 반사율 0.5.
            static constexpr uint32  kDefaultBakeSampleCount{ 256 };
            static constexpr float32 kDefaultBounceAlbedo{ 0.5f };
            /// @brief 간격의 하한 — 0 이면 프로브 수가 무한이다.
            static constexpr float32 kMinSpacing{ 0.1f };
            /// @brief 읽은 격자와 지금 격자를 같다고 볼 위치 · 간격의 오차(m).
            static constexpr float32 kLayoutTolerance{ 1e-3f };
            /// @brief 실시간 갱신의 기본값 — 복셀 0.5 m, 프로브마다 광선 64 개, 8 프레임에 한 바퀴, 새 값 10 % 섞기.
            static constexpr float32 kDefaultVoxelSize{ 0.5f };
            static constexpr uint32  kDefaultRealtimeRayCount{ 64 };
            static constexpr uint32  kDefaultRealtimeUpdatePeriod{ 8 };
            static constexpr float32 kDefaultRealtimeBlend{ 0.1f };
            /// @brief 복셀 상자가 볼륨 상자 바깥으로 더 가지는 고정 여유(m) — 간격에 더한다. 상자 가장자리 프로브 곁의 벽이 빠지지 않게 한다.
            static constexpr float32 kVoxelMargin{ 2.0f };

            /** @brief 한 축의 프로브 수와 실제 간격입니다 — 상자 양 끝에 프로브가 놓이게 고르게 나눈다. */
            static void computeAxis( float32 extent, float32 spacing, uint32& outCount, float32& outSpacing )
            {
                const float32 safeSpacing = MathUtil::max( spacing, kMinSpacing );
                outCount                  = static_cast<uint32>( MathUtil::max( extent, 0.0f ) / safeSpacing ) + 1u;
                outSpacing                = ( outCount > 1u ) ? extent / static_cast<float32>( outCount - 1u ) : safeSpacing;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LightProbeVolumeComponent::LightProbeVolumeComponent()
        : _extent{ LightProbeVolumeComponentInternal::kDefaultExtent }
        , _spacing{ LightProbeVolumeComponentInternal::kDefaultSpacing }
        , _probeDataPath{}
        , _bakeSampleCount{ LightProbeVolumeComponentInternal::kDefaultBakeSampleCount }
        , _bounceAlbedo{ LightProbeVolumeComponentInternal::kDefaultBounceAlbedo }
        , _bRealtime{ SW_FALSE }
        , _voxelSize{ LightProbeVolumeComponentInternal::kDefaultVoxelSize }
        , _realtimeRayCount{ LightProbeVolumeComponentInternal::kDefaultRealtimeRayCount }
        , _realtimeUpdatePeriod{ LightProbeVolumeComponentInternal::kDefaultRealtimeUpdatePeriod }
        , _realtimeBlend{ LightProbeVolumeComponentInternal::kDefaultRealtimeBlend }
        , _probeData{}
        , _pRealtimeVoxelGrid{}
        , _realtimeVoxelSetGeneration{ 0 }
        , _realtimeVoxelBounds{}
        , _realtimeVoxelCellSize{ 0.0f }
    {
    }

    void LightProbeVolumeComponent::setRealtime( bool bRealtime )
    {
        _bRealtime = bRealtime ? SW_TRUE : SW_FALSE;
        onPropertyChanged( hashed_string( "_bRealtime" ) );
    }

    AABB LightProbeVolumeComponent::computeRealtimeVoxelBounds() const
    {
        const float32 margin = MathUtil::max( _spacing, 0.0f ) + LightProbeVolumeComponentInternal::kVoxelMargin;
        const float3  center = getWorldPosition();
        const float3  half   = _extent * 0.5f + float3{ margin, margin, margin };
        return AABB{ center - half, center + half };
    }

    shared_ptr<const SceneVoxelGrid> LightProbeVolumeComponent::findRealtimeVoxelGrid( uint64 primitiveSetGeneration ) const
    {
        const AABB bounds = computeRealtimeVoxelBounds();
        // 상자(볼륨을 옮기거나 키웠다) · 칸 크기 · 프리미티브 집합이 그대로여야 캐시가 맞다.
        const bool bSameBounds = _realtimeVoxelBounds._min == bounds._min && _realtimeVoxelBounds._max == bounds._max;
        const bool bFresh      = _pRealtimeVoxelGrid != nullptr && bSameBounds && MathUtil::nearEqual( _realtimeVoxelCellSize, _voxelSize ) && _realtimeVoxelSetGeneration == primitiveSetGeneration;
        return bFresh ? _pRealtimeVoxelGrid : nullptr;
    }

    void LightProbeVolumeComponent::setRealtimeVoxelGrid( shared_ptr<const SceneVoxelGrid> grid, uint64 primitiveSetGeneration ) const
    {
        _pRealtimeVoxelGrid         = std::move( grid );
        _realtimeVoxelSetGeneration = primitiveSetGeneration;
        _realtimeVoxelBounds        = computeRealtimeVoxelBounds();
        _realtimeVoxelCellSize      = _voxelSize;
    }

    void LightProbeVolumeComponent::setExtent( const float3& extent )
    {
        _extent = float3{ MathUtil::max( extent._x, 0.0f ), MathUtil::max( extent._y, 0.0f ), MathUtil::max( extent._z, 0.0f ) };
        onPropertyChanged( hashed_string( "_extent" ) );
    }

    void LightProbeVolumeComponent::setSpacing( float32 spacing )
    {
        _spacing = MathUtil::max( spacing, LightProbeVolumeComponentInternal::kMinSpacing );
        onPropertyChanged( hashed_string( "_spacing" ) );
    }

    LightProbeGridLayout LightProbeVolumeComponent::computeLayout() const
    {
        using Internal = LightProbeVolumeComponentInternal;
        LightProbeGridLayout layout{};
        Internal::computeAxis( _extent._x, _spacing, layout._countX, layout._spacing._x );
        Internal::computeAxis( _extent._y, _spacing, layout._countY, layout._spacing._y );
        Internal::computeAxis( _extent._z, _spacing, layout._countZ, layout._spacing._z );
        layout._origin = getWorldPosition() - _extent * 0.5f;
        return layout;
    }

    const LightProbeGrid* LightProbeVolumeComponent::findProbeData() const
    {
        if ( _probeData._listProbe.empty() )
            return nullptr;
        // 볼륨을 옮기거나 크기를 바꾸고 다시 베이크하지 않았으면 옛 격자의 빛이다 — 쓰지 않는다.
        if ( _probeData._layout.matches( computeLayout(), LightProbeVolumeComponentInternal::kLayoutTolerance ) == false )
            return nullptr;
        return &_probeData;
    }

    bool LightProbeVolumeComponent::adoptProbeData( LightProbeGrid&& grid, string_view path )
    {
        string     error;
        const bool bValid   = grid.isValid( &error );
        const bool bMatches = bValid && grid._layout.matches( computeLayout(), LightProbeVolumeComponentInternal::kLayoutTolerance );
        if ( bMatches == false )
        {
            SW_LOG_ERROR( "Light probe data for '%#' does not fit this volume (%#)", string( path ).c_str(), bValid ? "grid layout differs" : error.c_str() );
            return false;
        }
        _probeData     = std::move( grid );
        _probeDataPath = string( path );
        return true;
    }

    void LightProbeVolumeComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<LightProbeVolumeComponent>( this );
    }

    void LightProbeVolumeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<LightProbeVolumeComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void LightProbeVolumeComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        reloadProbeData();
    }

    void LightProbeVolumeComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        if ( propertyName == hashed_string( "_probeDataPath" ) )
            reloadProbeData();
    }

    void LightProbeVolumeComponent::reloadProbeData()
    {
        _probeData.clear();
        if ( _probeDataPath.empty() )
            return;
        string error;
        if ( _probeData.loadFromAsset( AssetID{ _probeDataPath }, &error ) == false )
        {
            SW_LOG_ERROR( "Light probe data '%#' could not be loaded (%#) - the volume gives no indirect light until it is baked again", _probeDataPath.c_str(), error.c_str() );
            return;
        }
        if ( _probeData._layout.matches( computeLayout(), LightProbeVolumeComponentInternal::kLayoutTolerance ) == false )
            SW_LOG_ERROR( "Light probe data '%#' was baked for a different volume layout - bake the volume again", _probeDataPath.c_str() );
    }

    void LightProbeVolumeComponent::validateProbeData( ValidationContext& context ) const
    {
        const bool bNoExtent = _extent._x <= 0.0f && _extent._y <= 0.0f && _extent._z <= 0.0f;
        if ( bNoExtent )
            context.addError( "Extent is 0 on every axis - the volume holds a single probe" );
        if ( _probeDataPath.empty() && _bRealtime == SW_FALSE )
            context.addWarning( "Light probe volume is not baked - it gives no indirect light" );
    }
} // namespace sw
