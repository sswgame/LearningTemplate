#include "pch.h"

#include "Engine/Config/EngineDefaultAssets.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "EngineDefaultAssets" );

    unique_ptr<EngineDefaultAssets> EngineDefaultAssets::load( string_view assetRelativePath )
    {
        unique_ptr<EngineDefaultAssets> assets = make_unique<EngineDefaultAssets>( ConstructKey{} );
        if ( assets->loadFromResource( ResourceID{ assetRelativePath } ) == false )
            return nullptr;
        return assets;
    }

    bool EngineDefaultAssets::loadFromResource( const ResourceID& resourceID )
    {
        const string_view assetRelativePath = resourceID.view();
        const string      path              = assetRelativePath.empty() ? string( path::kEngineDefaultAssets ) : string( assetRelativePath );

        // REFLECT_BODY() 가 헤더에 StaticType() 을 선언해 둔다. 그래서 레지스트리를 이름으로 뒤질
        // 필요가 없고, Engine 내부 서비스에 접근할 수 없는 모듈에서도 그대로 쓸 수 있다.
        const TypeInfo* pTypeInfo = EngineDefaultAssets::StaticType();
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR( "EngineDefaultAssets TypeInfo is missing - cannot read %#", path );
            return false;
        }

        // 파일에 없는 선택 칸은 멤버 초기값(스키마 기본값)이 그대로 남는다. 필수 칸은 초기값이 없으므로 아래에서 막는다.
        if ( XMLSerializer::loadFile( ResourceID{ path }, this, *pTypeInfo ) == false )
        {
            SW_LOG_ERROR( "EngineDefaultAssets could not be read or has mismatched fields: %#", path );
            return false;
        }

        // 필수 칸(셰이더 · 파이프라인 · 렌더 패스 경로)은 헤더에 값이 없다 — 데이터가 기준이다. 빠졌거나 가리키는 파일이 없으면
        // 다른 경로로 대신하지 않고 읽기를 실패로 끝낸다(기동 · 쿠킹이 멈춘다). Shipping 팩에는 셰이더 소스(.hlsl)가 없으므로
        // 파일 확인은 개발 빌드만 한다 — Shipping 은 쿠킹 스탬프(`cook.stamp`)가 소스와 바이너리의 짝을 지킨다.
        bool bComplete = true;
        for ( const PropertyInfo& prop : pTypeInfo->getPropertiesWithBase() )
        {
            if ( prop._metadata->_bRequired == SW_FALSE )
                continue;
            const DataPath* pValue = prop.getValuePtr<DataPath>( this );
            if ( pValue == nullptr || pValue->isSet() == false )
            {
                SW_LOG_ERROR( "%#: required field <%#> is missing", path, prop._name.c_str() );
                bComplete = false;
                continue;
            }
#if !defined( SW_SHIPPING )
            // 이 호스트가 읽지 않는 에셋 종류(전용 서버의 텍스처 · 셰이더 · 글꼴)는 팩에 없다 — 그 칸은 쓰는 쪽도 없으므로 보지 않는다.
            if ( ResourcePaths::isExcludedForHost( pValue->get() ) == false && ResourceIO::hasResource( ResourceID{ pValue->get() } ) == false )
            {
                SW_LOG_ERROR( "%#: <%#> points to '%#', which does not exist", path, prop._name.c_str(), pValue->c_str() );
                bComplete = false;
            }
#endif
        }
        if ( bComplete == false )
            return false;

        SW_LOG_INFO( "Loaded from %#", path );
        return true;
    }
} // namespace sw
