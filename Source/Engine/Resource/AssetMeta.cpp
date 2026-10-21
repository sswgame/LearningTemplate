#include "pch.h"

#include "Engine/Resource/AssetMeta.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    string AssetMeta::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool AssetMeta::parseXML( string_view text, const utf8* pSourceLabel )
    {
        // 설정 XML 요소와 같은 엄격한 읽기다 — 모르는 속성, 빠진 필수 칸, 읽지 못한 값이 모두 오류다.
        return ConfigManager::readConfigXMLElement( this, *StaticType(), text, pSourceLabel );
    }
} // namespace sw
