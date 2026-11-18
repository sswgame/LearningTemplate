#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherMaterialComponent.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    WeatherMaterialComponent::WeatherMaterialComponent()
        : _collectionPath{}
        , _listBinding{}
        , _pCollection{}
        , _listStartValue{}
        , _listWritten{}
        , _listUsable{}
        , _listHasWritten{}
    {
        setCanEverTick( true );
    }

    WeatherMaterialComponent::~WeatherMaterialComponent() = default;

    float4 WeatherMaterialComponent::computeValue( const WeatherMaterialBinding& binding, const float4& fileValue, const WeatherSystem* pWeather )
    {
        const auto read = [pWeather]( const hashed_string& name, float32 fallbackChannel )
        {
            if ( name.empty() )
                return fallbackChannel; // 적지 않은 채널은 파일 값 그대로(설선 높이처럼 날씨가 정하지 않는 값)
            return pWeather != nullptr ? pWeather->computeValue( name ) : 0.0f;
        };
        return float4{ read( binding._valueX, fileValue._x ), read( binding._valueY, fileValue._y ), read( binding._valueZ, fileValue._z ), read( binding._valueW, fileValue._w ) };
    }

    void WeatherMaterialComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject* pOwner     = getOwner();
        const utf8*       pOwnerName = pOwner != nullptr ? pOwner->getName().c_str() : "?";
        _pCollection                 = MaterialParameterCollection::acquire( AssetID{ _collectionPath } ); // 읽지 못하면 받기가 오류를 남긴다
        _listStartValue.assign( _listBinding.size(), float4{} );
        _listWritten.assign( _listBinding.size(), float4{} );
        _listUsable.assign( _listBinding.size(), SW_FALSE );
        _listHasWritten.assign( _listBinding.size(), SW_FALSE );
        if ( _pCollection == nullptr )
            return;
        for ( size_t index = 0; index < _listBinding.size(); ++index )
        {
            const float4* pValue = _pCollection->findValue( _listBinding[index]._parameter );
            if ( pValue == nullptr )
            {
                SW_LOG_ERROR_ID( "WeatherMaterial.ParameterMissing", "'%#': parameter '%#' is not in '%#' - the weather does not write it", pOwnerName, _listBinding[index]._parameter.c_str(),
                                 _collectionPath.c_str() );
                continue;
            }
            _listStartValue[index] = *pValue;
            _listUsable[index]     = SW_TRUE;
        }
    }

    void WeatherMaterialComponent::onEndPlay()
    {
        // 묶음은 프로세스에 한 벌이다 — 다음 씬이 이 씬의 비를 물려받지 않게 시작 값으로 되돌린다.
        for ( size_t index = 0; _pCollection != nullptr && index < _listBinding.size(); ++index )
        {
            if ( _listUsable[index] == SW_TRUE )
                (void)_pCollection->setValue( _listBinding[index]._parameter, _listStartValue[index] );
        }
        _pCollection.reset();
        Component::onEndPlay();
    }

    void WeatherMaterialComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _pCollection == nullptr )
            return;
        // 묶음은 게임 스레드만 고친다 — 쓰기는 틱 뒤에 한다.
        context.deferPostTickOnComponent( *this, []( WeatherMaterialComponent& component, GameObjectManager& manager )
        { component.applyWeather( manager ); } );
    }

    void WeatherMaterialComponent::applyWeather( GameObjectManager& manager )
    {
        if ( _pCollection == nullptr )
            return;
        // 날씨가 없는 씬은 값 0 이다 — 판 상태가 날씨를 열지 않았다(WeatherWaveComponent 와 같은 규칙).
        const SessionStateComponent* pSession = SessionStateComponent::findFirstOpen( &manager );
        const WeatherSystem*         pWeather = pSession != nullptr ? &pSession->getWeather() : nullptr;
        for ( size_t index = 0; index < _listBinding.size(); ++index )
        {
            if ( _listUsable[index] == SW_FALSE )
                continue;
            const float4 value      = computeValue( _listBinding[index], _listStartValue[index], pWeather );
            const bool   bUnchanged = _listHasWritten[index] == SW_TRUE && value == _listWritten[index];
            if ( bUnchanged )
                continue; // 날씨가 그대로다 — 그사이 개발 명령이 바꾼 값을 덮지 않는다
            (void)_pCollection->setValue( _listBinding[index]._parameter, value );
            _listWritten[index]    = value;
            _listHasWritten[index] = SW_TRUE;
        }
    }
} // namespace sw
