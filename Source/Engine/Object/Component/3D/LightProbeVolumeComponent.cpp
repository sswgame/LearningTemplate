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
            /// @brief 간격의 하한 — 0 이면 프로브 수가 무한이다.
            static constexpr float32 kMinSpacing{ 0.1f };
            /// @brief 읽은 격자와 지금 격자를 같다고 볼 위치 · 간격의 오차(m).
            static constexpr float32 kLayoutTolerance{ 1e-3f };

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
        , _probeData{}
    {
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
        // 볼륨을 옮기거나 크기를 바꾸고 다시 굽지 않았으면 옛 격자의 빛이다 — 쓰지 않는다.
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
        if ( _probeData.loadFromResource( _probeDataPath, &error ) == false )
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
        if ( _probeDataPath.empty() )
            context.addWarning( "Light probe volume is not baked - it gives no indirect light" );
    }
} // namespace sw
