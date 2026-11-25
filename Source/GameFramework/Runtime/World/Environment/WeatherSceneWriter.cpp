#include "pch.h"

#include "GameFramework/Runtime/World/Environment/WeatherSceneWriter.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Visual/Component/LightComponent.h"

#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    namespace
    {
        struct WeatherSceneWriterInternal
        {
            static constexpr uint32 kTargetCount = static_cast<uint32>( WeatherSceneTarget::WindSpeed ) + 1u; ///< 마지막 대상 다음
            static_assert( kTargetCount <= 8, "the bound target mask is 8 bits" );

            static constexpr uint8 makeBit( uint32 target ) { return static_cast<uint8>( BitUtil::bit( target ) ); }
        };
    } // namespace

    WeatherSceneWriter::WeatherSceneWriter()
        : _pCatalog{ nullptr }
        , _listSoundState{}
        , _skyOpenness{ 1.0f }
        , _boundTargetMask{ 0 }
    {
    }

    void WeatherSceneWriter::open( const WeatherCatalog* pCatalog )
    {
        close( nullptr );
        if ( pCatalog == nullptr || ( pCatalog->getSceneBindings().empty() && pCatalog->getSoundBindings().empty() ) )
            return;
        _pCatalog = pCatalog;
        _listSoundState.assign( pCatalog->getSoundBindings().size(), SoundState{} );
        for ( const WeatherSceneBinding& binding : pCatalog->getSceneBindings() )
            _boundTargetMask = static_cast<uint8>( _boundTargetMask | WeatherSceneWriterInternal::makeBit( static_cast<uint32>( binding._target ) ) );
    }

    float32 WeatherSceneWriter::computeScale( const WeatherCatalog& catalog, const WeatherSystem& weather, WeatherSceneTarget target )
    {
        float32 scale = 1.0f;
        for ( const WeatherSceneBinding& binding : catalog.getSceneBindings() )
        {
            if ( binding._target == target )
                scale *= MathUtil::lerp( binding._atZero, binding._atOne, MathUtil::saturate( weather.computeValue( binding._value ) ) );
        }
        return scale;
    }

    void WeatherSceneWriter::writeScale( GameObjectManager& manager, WeatherSceneTarget target, float32 scale )
    {
        ComponentRegistry& registry = manager.getComponentRegistry();
        switch ( target )
        {
            case WeatherSceneTarget::SunIntensity:
            {
                for ( LightComponent* pLight : registry.getAll<LightComponent>( LightComponent::getRegistryIndex( LightKind::Directional ) ) )
                    pLight->setIntensityScale( scale );
                break;
            }
            case WeatherSceneTarget::SkyIntensity:
            {
                for ( SkyComponent* pSky : registry.getAll<SkyComponent>() )
                    pSky->setIntensityScale( scale );
                break;
            }
            case WeatherSceneTarget::FogDensity:
            {
                for ( FogComponent* pFog : registry.getAll<FogComponent>() )
                    pFog->setDensityScale( scale );
                break;
            }
            case WeatherSceneTarget::WindStrength:
            {
                for ( WindComponent* pWind : registry.getAll<WindComponent>() )
                    pWind->setStrengthScale( scale );
                break;
            }
            case WeatherSceneTarget::WindSpeed:
            {
                for ( WindComponent* pWind : registry.getAll<WindComponent>() )
                    pWind->setSpeedScale( scale );
                break;
            }
        }
    }

    void WeatherSceneWriter::apply( GameObjectManager& manager, const WeatherSystem& weather )
    {
        if ( _pCatalog == nullptr )
            return;
        for ( uint32 target = 0; target < WeatherSceneWriterInternal::kTargetCount; ++target )
        {
            if ( ( _boundTargetMask & WeatherSceneWriterInternal::makeBit( target ) ) != 0 )
                writeScale( manager, static_cast<WeatherSceneTarget>( target ), computeScale( *_pCatalog, weather, static_cast<WeatherSceneTarget>( target ) ) );
        }
        updateAudio( manager, weather );
    }

    void WeatherSceneWriter::updateAudio( GameObjectManager& manager, const WeatherSystem& weather )
    {
        const vector<WeatherSoundBinding>& listBinding = _pCatalog->getSoundBindings();
        bool                               bAnySound   = false;
        for ( size_t index = 0; index < listBinding.size() && index < _listSoundState.size(); ++index )
        {
            const WeatherSoundBinding& binding = listBinding[index];
            SoundState&                state   = _listSoundState[index];
            const float32              value   = weather.computeValue( binding._value );
            const bool                 bSound  = value > 0.0f;
            if ( value != state._written )
            {
                GameSound::setParameter( binding._parameter, value );
                state._written = value;
            }
            if ( bSound && state._bPlaying == SW_FALSE )
                state._playingID = GameSound::postEvent( binding._event );
            else if ( bSound == false && state._bPlaying == SW_TRUE )
                GameSound::stop( state._playingID, -1.0f ); // 음수 페이드는 이벤트의 값
            state._bPlaying = bSound ? SW_TRUE : SW_FALSE;
            bAnySound       = bAnySound || ( bSound && binding._opennessParameter.empty() == false );
        }
        float3 listener{};
        if ( bAnySound == false || EnvironmentUtil::findViewPosition( manager, listener ) == false )
            return;
        const float32 openness = WeatherShelter::computeSkyOpenness( manager, listener, WeatherShelter::kShelterDistance );
        _skyOpenness           = openness;
        for ( const WeatherSoundBinding& binding : listBinding )
        {
            if ( binding._opennessParameter.empty() == false )
                GameSound::setParameter( binding._opennessParameter, openness ); // 라이브러리의 `_seekSpeed` 가 부드럽게 따라간다
        }
    }

    void WeatherSceneWriter::close( GameObjectManager* pManager )
    {
        for ( const SoundState& state : _listSoundState )
        {
            if ( state._bPlaying == SW_TRUE )
                GameSound::stop( state._playingID, -1.0f );
        }
        for ( uint32 target = 0; pManager != nullptr && target < WeatherSceneWriterInternal::kTargetCount; ++target )
        {
            if ( ( _boundTargetMask & WeatherSceneWriterInternal::makeBit( target ) ) != 0 )
                writeScale( *pManager, static_cast<WeatherSceneTarget>( target ), 1.0f );
        }
        _pCatalog = nullptr;
        _listSoundState.clear();
        _skyOpenness     = 1.0f;
        _boundTargetMask = 0;
    }

    bool WeatherSceneWriter::isSoundPlaying( const hashed_string& event ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const vector<WeatherSoundBinding>& listBinding = _pCatalog->getSoundBindings();
        for ( size_t index = 0; index < listBinding.size() && index < _listSoundState.size(); ++index )
        {
            if ( listBinding[index]._event == event && _listSoundState[index]._bPlaying == SW_TRUE )
                return true;
        }
        return false;
    }

    WeatherMaterialWriter::WeatherMaterialWriter()
        : _pCatalog{ nullptr }
        , _pCollection{}
        , _listStartValue{}
        , _listWritten{}
        , _listUsable{}
        , _listHasWritten{}
    {
    }

    WeatherMaterialWriter::~WeatherMaterialWriter()
    {
        close();
    }

    float4 WeatherMaterialWriter::computeValue( const WeatherMaterialBinding& binding, const float4& fileValue, const WeatherSystem* pWeather )
    {
        const auto read = [pWeather]( const hashed_string& name, float32 fileChannel )
        {
            if ( name.empty() )
                return fileChannel; // 적지 않은 채널은 파일 값 그대로(설선 높이처럼 날씨가 정하지 않는 값)
            return pWeather != nullptr ? pWeather->computeValue( name ) : 0.0f;
        };
        return float4{ read( binding._valueX, fileValue._x ), read( binding._valueY, fileValue._y ), read( binding._valueZ, fileValue._z ), read( binding._valueW, fileValue._w ) };
    }

    void WeatherMaterialWriter::open( const WeatherCatalog* pCatalog )
    {
        close();
        if ( pCatalog == nullptr || pCatalog->getMaterialBindings().empty() )
            return; // 강수를 칸으로 적지 않은 날씨 — 머티리얼은 파일 값 그대로다
        const vector<WeatherMaterialBinding>& listBinding = pCatalog->getMaterialBindings();
        _pCollection                                      = MaterialParameterCollection::acquire( AssetID{ pCatalog->getMaterialCollection() } ); // 읽지 못하면 받기가 오류를 남긴다
        if ( _pCollection == nullptr )
            return;
        _pCatalog = pCatalog;
        _listStartValue.assign( listBinding.size(), float4{} );
        _listWritten.assign( listBinding.size(), float4{} );
        _listUsable.assign( listBinding.size(), SW_FALSE );
        _listHasWritten.assign( listBinding.size(), SW_FALSE );
        for ( size_t index = 0; index < listBinding.size(); ++index )
        {
            const float4* pValue = _pCollection->findValue( listBinding[index]._parameter );
            if ( pValue == nullptr )
            {
                SW_LOG_ERROR_ID( "WeatherMaterial.ParameterMissing", "Weather: parameter '%#' is not in '%#' - the weather does not write it", listBinding[index]._parameter.c_str(),
                                 pCatalog->getMaterialCollection().c_str() );
                continue;
            }
            _listStartValue[index] = *pValue;
            _listUsable[index]     = SW_TRUE;
        }
    }

    void WeatherMaterialWriter::apply( const WeatherSystem& weather )
    {
        if ( _pCollection == nullptr || _pCatalog == nullptr )
            return;
        const vector<WeatherMaterialBinding>& listBinding = _pCatalog->getMaterialBindings();
        for ( size_t index = 0; index < listBinding.size() && index < _listUsable.size(); ++index )
        {
            if ( _listUsable[index] == SW_FALSE )
                continue;
            const float4 value      = computeValue( listBinding[index], _listStartValue[index], &weather );
            const bool   bUnchanged = _listHasWritten[index] == SW_TRUE && value == _listWritten[index];
            if ( bUnchanged )
                continue; // 날씨가 그대로다 — 그사이 개발 명령이 바꾼 값을 덮지 않는다
            (void)_pCollection->setValue( listBinding[index]._parameter, value );
            _listWritten[index]    = value;
            _listHasWritten[index] = SW_TRUE;
        }
    }

    void WeatherMaterialWriter::close()
    {
        // 묶음은 프로세스에 한 벌이다 — 다음 씬이 이 판의 비를 물려받지 않게 연 때의 값으로 되돌린다.
        if ( _pCollection != nullptr && _pCatalog != nullptr )
        {
            const vector<WeatherMaterialBinding>& listBinding = _pCatalog->getMaterialBindings();
            for ( size_t index = 0; index < listBinding.size() && index < _listUsable.size(); ++index )
            {
                if ( _listUsable[index] == SW_TRUE && _listHasWritten[index] == SW_TRUE )
                    (void)_pCollection->setValue( listBinding[index]._parameter, _listStartValue[index] );
            }
        }
        _pCollection.reset();
        _pCatalog = nullptr;
        _listStartValue.clear();
        _listWritten.clear();
        _listUsable.clear();
        _listHasWritten.clear();
    }
} // namespace sw
