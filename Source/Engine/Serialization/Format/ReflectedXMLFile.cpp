#include "pch.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "ReflectedXMLFile" );

    bool ReflectedXMLFile::loadDescInternal( string_view assetRelativePath, void* pDesc, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            // 리플렉션 생성물이 빠졌다는 뜻이다. 파일 탓이 아니므로 문장을 따로 둔다.
            SW_LOG_ERROR( "TypeInfo 를 찾을 수 없습니다 (%#) — 리플렉션 생성이 빠졌습니다", assetRelativePath );
            return false;
        }

        // PROPERTY 그래프를 그대로 읽는다 — 필드를 손으로 읽으면 필드를 더할 때마다 파서와 라이터를 같이 고쳐야 하고,
        // 하나만 빠뜨리면 조용히 빈 값이 된다. 모르는 이름 · 읽지 못한 값 · `Min`/`Max` 밖 값은 이름과 함께 오류다(설정 JSON 과 같은 계약).
        string text;
        if ( ResourceIO::readTextResource( assetRelativePath, text ) == false && FileUtil::readTextFile( assetRelativePath, text ) == false )
        {
            SW_LOG_ERROR( "XML 로드 실패: %# (%#) — 파일을 찾지 못했습니다", assetRelativePath, pTypeInfo->_name.c_str() );
            return false;
        }
        vector<SchemaOrphanValue> listOrphan;
        if ( XMLSerializer::deserializeSoft( pDesc, *pTypeInfo, text, &listOrphan ) == false )
        {
            SW_LOG_ERROR( "XML 로드 실패: %# (%#) — <%#> 문서가 아닙니다", assetRelativePath, pTypeInfo->_name.c_str(), pTypeInfo->_name.c_str() );
            return false;
        }
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR( "%#: '%#' is unknown to %# or its value '%#' cannot be read", assetRelativePath, pName, pTypeInfo->_name.c_str(), orphan._text.c_str() );
        }
        vector<string> listRangeError;
        SerializerUtil::collectRangeErrors( *pTypeInfo, pDesc, string(), listRangeError );
        for ( const string& rangeError : listRangeError )
        {
            SW_LOG_ERROR( "%#: %#", assetRelativePath, rangeError.c_str() );
        }
        return listOrphan.empty() && listRangeError.empty();
    }

    bool ReflectedXMLFile::saveDescInternal( string_view assetRelativePath, const void* pDesc, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR( "TypeInfo 를 찾을 수 없습니다 (%#) — 리플렉션 생성이 빠졌습니다", assetRelativePath );
            return false;
        }

        // 아직 없는 파일이면 `getResourcePath` 가 빈 문자열을 준다(존재할 때만 해석한다). 그때는
        // 받은 경로를 그대로 쓴다. 새로 저장하는 길이 막히면 안 되기 때문이다.
        string absPath = ResourcePaths::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        if ( XMLSerializer::saveFile( absPath, pDesc, *pTypeInfo ) == false )
        {
            SW_LOG_ERROR( "XML 쓰기 실패: %# (%#)", absPath, pTypeInfo->_name.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
