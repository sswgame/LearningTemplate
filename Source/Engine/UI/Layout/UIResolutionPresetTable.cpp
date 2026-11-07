#include "pch.h"

#include "Engine/UI/Layout/UIResolutionPresetTable.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    /** @brief 해상도 견본 표의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 표를 읽어 본다). */
    SW_ASSET_TYPE( UIResolutionPresets, AssetTypeDef::makeDataFile( "uiresolutions", AssetPathRule{ "engine/", {}, "/settings/uiresolutions.xml" }, AssetTypeLoader::of<UIResolutionPresetTable>() ) );
} // namespace sw

namespace sw
{
    bool UIResolutionPresetTable::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        _listResolution.clear();
        bool bOk = ReflectedXMLFile::loadDesc( assetID, *this );
        if ( bOk && _listResolution.empty() )
        {
            SW_LOG_ERROR_ID( "UIResolutionPresetTable.NoResolutionsListResolution", "%#: no resolutions in <_listResolution>", path );
            bOk = false;
        }
        // 원소의 `Min` · `Max` 는 `loadDesc` 가 본다(`SerializerUtil::collectRangeErrors` 는 시퀀스 원소까지 본다).
        if ( bOk == false )
            _listResolution.clear();
        return bOk;
    }
} // namespace sw
