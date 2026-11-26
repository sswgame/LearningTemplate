#include "pch.h"

#include "Engine/Config/DefaultAssetTable.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    bool DefaultAssetTable::load( const AssetID& assetID, void* pTable, const TypeInfo& typeInfo )
    {
        const string_view path  = assetID.view();
        const utf8*       pType = typeInfo._name.c_str();
        // 파일에 없는 선택 칸은 멤버 초기값(스키마 기본값)이 그대로 남는다. 필수 칸은 초기값이 없으므로 아래에서 막는다.
        if ( XMLSerializer::loadFromAsset( assetID, pTable, typeInfo ) == false )
        {
            SW_LOG_ERROR_ID( "DefaultAssetTable.CouldNotRead", "%# could not be read or has mismatched fields: %#", pType, path );
            return false;
        }
        bool bComplete = true;
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            if ( prop._metadata->_bRequired == SW_FALSE )
                continue;
            const DataPath* pValue = prop.getValuePtr<DataPath>( pTable );
            if ( pValue == nullptr || pValue->isSet() == false )
            {
                SW_LOG_ERROR_ID( "DefaultAssetTable.RequiredFieldMissing", "%#: required field <%#> is missing", path, prop._name.c_str() );
                bComplete = false;
                continue;
            }
            const bool bNotInThisPackage = engine::getSubsystem<AssetPaths>().isExcludedForHost( pValue->get() ) || ( AssetIO::getContentSource() == ContentSource::Cooked && PackConfig::isRawShaderSource( pValue->get() ) );
            if ( bNotInThisPackage == false && AssetIO::hasAsset( AssetID{ pValue->get() } ) == false )
            {
                SW_LOG_ERROR_ID( "DefaultAssetTable.PointsWhichDoesNot", "%#: <%#> points to '%#', which does not exist", path, prop._name.c_str(), pValue->c_str() );
                bComplete = false;
            }
        }
        if ( bComplete )
            SW_LOG_INFO_ID( "DefaultAssetTable.Loaded", "%#: loaded from %#", pType, path );
        return bComplete;
    }
} // namespace sw
