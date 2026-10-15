#include "pch.h"

#include "Engine/UI/Layout/UIResolutionPresetTable.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    SW_LOG_CALLER( "UIResolutionPresetTable" );

    namespace
    {
        struct UIResolutionPresetTableInternal
        {
            [[nodiscard]] static bool loadTable( const string& resourceID )
            {
                UIResolutionPresetTable table;
                return table.loadFromResource( resourceID );
            }
        };
    } // namespace

    /** @brief 해상도 견본 표의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 표를 읽어 본다). */
    SW_RESOURCE_DATA_KIND( UIResolutionPresets, "uiresolutions", "engine/", nullptr, "/settings/uiresolutions.xml", &UIResolutionPresetTableInternal::loadTable );
} // namespace sw

namespace sw
{
    bool UIResolutionPresetTable::loadFromResource( string_view path )
    {
        _listResolution.clear();
        bool bOk = ReflectedXMLFile::loadDesc( path, *this );
        if ( bOk && _listResolution.empty() )
        {
            SW_LOG_ERROR( "%#: no resolutions in <_listResolution>", path );
            bOk = false;
        }
        for ( const UIResolutionPreset& resolution : _listResolution )
        {
            vector<string> listRangeError;
            SerializerUtil::collectRangeErrors( *UIResolutionPreset::StaticType(), &resolution, resolution._name, listRangeError );
            for ( const string& rangeError : listRangeError )
            {
                SW_LOG_ERROR( "%#: %#", path, rangeError.c_str() );
                bOk = false;
            }
        }
        if ( bOk == false )
            _listResolution.clear();
        return bOk;
    }
} // namespace sw
