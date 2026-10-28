#include "pch.h"

#include "Engine/Object/Component/3D/ReflectionProbeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    SW_LOG_CALLER( "ReflectionProbe" );

    namespace
    {
        struct ReflectionProbeComponentInternal
        {
            /// @brief 기본 상자 — 방 하나 크기(라이트 프로브 볼륨의 기본과 같다), 경계 안쪽 1 m 동안 섞는다.
            static constexpr float3  kDefaultExtent{ 10.0f, 4.0f, 10.0f };
            static constexpr float32 kDefaultBlendDistance{ 1.0f };
            /// @brief 기본 베이크 — 옥타헤드럴 32 × 32(흐린 반사에 넉넉하고 프로브 하나가 단계 여섯에 약 6000 원소), 면 128 픽셀, 텍셀마다 표본 128.
            static constexpr uint32 kDefaultResolution{ 32 };
            static constexpr uint32 kDefaultCaptureResolution{ 128 };
            static constexpr uint32 kDefaultBakeSampleCount{ 128 };
            /// @brief 읽은 데이터의 찍은 위치와 지금 위치를 같다고 볼 오차(m).
            static constexpr float32 kPositionTolerance{ 1e-3f };
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReflectionProbeComponent::ReflectionProbeComponent()
        : _extent{ ReflectionProbeComponentInternal::kDefaultExtent }
        , _blendDistance{ ReflectionProbeComponentInternal::kDefaultBlendDistance }
        , _probeDataPath{}
        , _resolution{ ReflectionProbeComponentInternal::kDefaultResolution }
        , _captureResolution{ ReflectionProbeComponentInternal::kDefaultCaptureResolution }
        , _bakeSampleCount{ ReflectionProbeComponentInternal::kDefaultBakeSampleCount }
        , _probeData{}
    {
    }

    void ReflectionProbeComponent::setExtent( const float3& extent )
    {
        _extent = float3{ MathUtil::max( extent._x, 0.0f ), MathUtil::max( extent._y, 0.0f ), MathUtil::max( extent._z, 0.0f ) };
        onPropertyChanged( hashed_string( "_extent" ) );
    }

    bool ReflectionProbeComponent::matchesCapturePosition( const ReflectionProbeData& data ) const
    {
        return ( data._capturePosition - getWorldPosition() ).getLength() <= ReflectionProbeComponentInternal::kPositionTolerance;
    }

    const ReflectionProbeData* ReflectionProbeComponent::findProbeData() const
    {
        if ( _probeData._listTexel.empty() )
            return nullptr;
        // 프로브를 옮기고 다시 베이크하지 않았으면 옛 자리의 반사다 — 쓰지 않는다.
        if ( matchesCapturePosition( _probeData ) == false )
            return nullptr;
        return &_probeData;
    }

    bool ReflectionProbeComponent::adoptProbeData( ReflectionProbeData&& data, string_view path )
    {
        string     error;
        const bool bValid   = data.isValid( &error );
        const bool bMatches = bValid && matchesCapturePosition( data );
        if ( bMatches == false )
        {
            SW_LOG_ERROR( "Reflection probe data for '%#' does not fit this probe (%#)", string( path ).c_str(), bValid ? "captured at another position" : error.c_str() );
            return false;
        }
        _probeData     = std::move( data );
        _probeDataPath = string( path );
        return true;
    }

    void ReflectionProbeComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<ReflectionProbeComponent>( this );
    }

    void ReflectionProbeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ReflectionProbeComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void ReflectionProbeComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        reloadProbeData();
    }

    void ReflectionProbeComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        if ( propertyName == hashed_string( "_probeDataPath" ) )
            reloadProbeData();
    }

    void ReflectionProbeComponent::reloadProbeData()
    {
        _probeData.clear();
        if ( _probeDataPath.empty() )
            return;
        string error;
        if ( _probeData.loadFromAsset( AssetID{ _probeDataPath }, &error ) == false )
        {
            SW_LOG_ERROR( "Reflection probe data '%#' could not be loaded (%#) - the probe gives no reflection until it is baked again", _probeDataPath.c_str(), error.c_str() );
            return;
        }
        if ( _probeData.isValid( &error ) == false )
        {
            SW_LOG_ERROR( "Reflection probe data '%#' is broken (%#) - bake the probe again", _probeDataPath.c_str(), error.c_str() );
            _probeData.clear();
            return;
        }
        if ( matchesCapturePosition( _probeData ) == false )
            SW_LOG_ERROR( "Reflection probe data '%#' was captured at another position - bake the probe again", _probeDataPath.c_str() );
    }

    void ReflectionProbeComponent::validateProbeData( ValidationContext& context ) const
    {
        const bool bNoExtent = _extent._x <= 0.0f || _extent._y <= 0.0f || _extent._z <= 0.0f;
        if ( bNoExtent )
            context.addError( "Extent is 0 on some axis - no surface is inside the probe box" );
        if ( _probeDataPath.empty() )
            context.addWarning( "Reflection probe is not baked - it gives no reflection" );
    }
} // namespace sw
