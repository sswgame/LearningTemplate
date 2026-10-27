#include "pch.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "ReflectedXMLFile" );

    bool ReflectedXMLFile::loadDescInternal( string_view assetRelativePath, ReadSource source, void* pDesc, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            // 리플렉션 생성물이 빠졌다는 뜻이다. 파일 탓이 아니므로 문장을 따로 둔다.
            SW_LOG_ERROR( "TypeInfo not found (%#) - reflection generation is missing", assetRelativePath );
            return false;
        }

        // PROPERTY 그래프를 그대로 읽는다 — 필드를 손으로 읽으면 필드를 더할 때마다 파서와 라이터를 같이 고쳐야 하고,
        // 하나만 빠뜨리면 조용히 빈 값이 된다. 모르는 이름 · 읽지 못한 값 · `Min`/`Max` 밖 값은 이름과 함께 오류다(설정 JSON 과 같은 계약).
        string text;
        // 낱개 파일 허용 정책은 `ResourceIO` 한 곳이 본다 — 실패 뒤 디스크를 다시 읽으면 팩 전용 구성의 거절을 지나간다.
        // 디스크 파일은 부르는 쪽이 이름(`loadDescFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        const bool bRead = source == ReadSource::DiskFile ? ResourceIO::readTextFile( assetRelativePath, text )
                                                          : ResourceIO::readTextResource( ResourceID{ assetRelativePath }, text );
        if ( bRead == false )
        {
            SW_LOG_ERROR( "XML load failed: %# (%#) - file not found", assetRelativePath, pTypeInfo->_name.c_str() );
            return false;
        }
        vector<SchemaOrphanValue> listOrphan;
        const SoftReadResult      result = XMLSerializer::deserializeSoft( pDesc, *pTypeInfo, text, &listOrphan );
        if ( result == SoftReadResult::NotParsed )
        {
            SW_LOG_ERROR( "XML load failed: %# (%#) - not a <%#> document", assetRelativePath, pTypeInfo->_name.c_str(), pTypeInfo->_name.c_str() );
            return false;
        }
        // 범위 밖 값(`Rejected`)은 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 칸마다 이미 알렸다.
        // 그래도 모은 이름은 알린다 — 범위 밖 값 하나가 모르는 이름 · 읽지 못한 값의 오류를 삼키지 않게(모두 알린다는 계약).
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR_ID( "Serialization.OrphanValue", "%#: '%#' is unknown to %# or its value '%#' cannot be read", assetRelativePath, pName, pTypeInfo->_name.c_str(), orphan._text.c_str() );
        }
        return result == SoftReadResult::Read && listOrphan.empty();
    }

    bool ReflectedXMLFile::saveDescInternal( string_view assetRelativePath, const void* pDesc, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR( "TypeInfo not found (%#) - reflection generation is missing", assetRelativePath );
            return false;
        }

        // 쓰기 경로는 `getWritePath` 하나가 정한다(아직 없는 파일도 도메인 루트 아래로 — 작업 디렉터리에 떨어지지 않게).
        const string absPath = ResourcePaths::getWritePath( assetRelativePath );

        if ( XMLSerializer::saveFile( absPath, pDesc, *pTypeInfo ) == false )
        {
            SW_LOG_ERROR( "XML write failed: %# (%#)", absPath, pTypeInfo->_name.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
