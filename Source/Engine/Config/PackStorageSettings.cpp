#include "pch.h"

#include "Engine/Config/PackStorageSettings.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    bool PackStorageSettings::loadFromAsset( const AssetID& assetID )
    {
        bool bValid = XMLSerializer::loadFromAsset( assetID, this, *StaticType() ) && _listStoredExtension.empty() == false;
        for ( const string& extension : _listStoredExtension )
            bValid = bValid && extension.size() > 1 && extension[0] == '.';
        if ( bValid == false )
            SW_LOG_ERROR_ID( "PackStorageSettings.Invalid", "Pack storage settings '%#' could not be read, name no stored extension, or name one that does not start with '.'", assetID.view() );
        return bValid;
    }

    /** @brief 팩 저장 방식 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( PackStorage, AssetTypeDef::makeDataFile( "packstorage", AssetPathRule{ {}, {}, "packstorage.xml" }, AssetTypeLoader::of<PackStorageSettings>() ) );
} // namespace sw
