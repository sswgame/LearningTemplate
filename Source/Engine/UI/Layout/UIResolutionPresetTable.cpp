#include "pch.h"

#include "Engine/UI/Layout/UIResolutionPresetTable.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    SW_LOG_CALLER( "UIResolutionPresetTable" );

    namespace
    {
        [[nodiscard]] bool loadTable( const ResourceID& id )
        {
            UIResolutionPresetTable table;
            return table.loadFromResource( id );
        }
    } // namespace

    /** @brief 해상도 견본 표의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 표를 읽어 본다). */
    SW_RESOURCE_DATA_KIND( UIResolutionPresets, "uiresolutions", "engine/", nullptr, "/settings/uiresolutions.xml", &loadTable );
} // namespace sw

namespace sw
{
    bool UIResolutionPresetTable::loadFromResource( const ResourceID& resourceID )
    {
        const string_view path = resourceID.view();
        _listResolution.clear();
        bool bOk = ReflectedXMLFile::loadDesc( resourceID, *this );
        if ( bOk && _listResolution.empty() )
        {
            SW_LOG_ERROR( "%#: no resolutions in <_listResolution>", path );
            bOk = false;
        }
        // 원소의 `Min` · `Max` 는 `loadDesc` 가 본다(`SerializerUtil::collectRangeErrors` 는 시퀀스 원소까지 본다).
        if ( bOk == false )
            _listResolution.clear();
        return bOk;
    }
} // namespace sw
