#include "pch.h"

#include "GameFramework/Runtime/World/Environment/WeatherMaterialWriter.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"

#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
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
