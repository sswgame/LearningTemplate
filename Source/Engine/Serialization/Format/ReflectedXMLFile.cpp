#include "pch.h"

#include "Engine/Serialization/Format/ReflectedXMLFile.h"

#include "Core/File/FileUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw
{
    bool ReflectedXMLFile::loadDescInternal( const AssetID& assetID, void* pDesc, const TypeInfo* pTypeInfo )
    {
        // 낱개 파일 허용 정책은 `AssetIO` 한 곳이 본다 — 실패 뒤 디스크를 다시 읽으면 팩 전용 구성의 거절을 지나간다.
        string text;
        if ( pTypeInfo != nullptr && AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "Xml.FileNotFound", "XML load failed: %# (%#) - file not found", assetID.view(), pTypeInfo->_name.c_str() );
            return false;
        }
        return parseDesc( assetID.view(), text, pDesc, pTypeInfo );
    }

    bool ReflectedXMLFile::loadDescFromDiskFileInternal( string_view diskPath, void* pDesc, const TypeInfo* pTypeInfo )
    {
        // 디스크 파일은 부르는 쪽이 이름(`loadDescFromDiskFile`)으로 고른 것이다 — 리소스 id 읽기가 실패한 뒤 넘어오는 길이 아니다.
        string text;
        if ( pTypeInfo != nullptr && AssetIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_ERROR_ID( "Xml.FileNotFound", "XML load failed: %# (%#) - file not found", diskPath, pTypeInfo->_name.c_str() );
            return false;
        }
        return parseDesc( diskPath, text, pDesc, pTypeInfo );
    }

    bool ReflectedXMLFile::parseDesc( string_view sourceName, const string& text, void* pDesc, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            // 리플렉션 생성물이 빠졌다는 뜻이다. 파일 탓이 아니므로 문장을 따로 둔다.
            SW_LOG_ERROR_ID( "Reflection.TypeInfoMissing", "TypeInfo not found (%#) - reflection generation is missing", sourceName );
            return false;
        }

        // PROPERTY 그래프를 그대로 읽는다 — 필드를 손으로 읽으면 필드를 더할 때마다 파서와 라이터를 같이 고쳐야 하고,
        // 하나만 빠뜨리면 조용히 빈 값이 된다. 모르는 이름 · 읽지 못한 값 · `Min`/`Max` 밖 값은 이름과 함께 오류다(설정 JSON 과 같은 계약).
        // 부모를 가리키면 한 벌로 펼친 뒤 읽는다 — 읽기 규칙(모르는 이름 · 범위)은 펼친 결과에 그대로 걸린다.
        string flatText;
        if ( DataInheritance::findBasePath( text ).empty() == false && DataInheritance::flattenReflected( text, sourceName, *pTypeInfo, flatText ) == false )
            return false;
        vector<SchemaOrphanValue> listOrphan;
        const SoftReadResult      result = XMLSerializer::deserializeSoft( pDesc, *pTypeInfo, flatText.empty() ? string_view( text ) : string_view( flatText ), &listOrphan );
        if ( result == SoftReadResult::NotParsed )
        {
            // 왜 못 읽었는지(`Xml.ParseError` · `Xml.WrongRoot`)는 안쪽이 이미 알렸다 — 어느 파일인지를 더한다. 빈 글은 이 줄만 남는다.
            SW_LOG_ERROR_ID( "Xml.NotRead", "XML load failed: %# is not a readable <%#> document", sourceName, pTypeInfo->_name.c_str() );
            return false;
        }
        // 범위 밖 값(`Rejected`)은 읽기 공통 끝(`SerializerUtil::isWithinDeclaredRanges`)이 칸마다 이미 알렸다.
        // 그래도 모은 이름은 알린다 — 범위 밖 값 하나가 모르는 이름 · 읽지 못한 값의 오류를 삼키지 않게(모두 알린다는 계약).
        for ( const SchemaOrphanValue& orphan : listOrphan )
        {
            const utf8* const pName = orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str();
            SW_LOG_ERROR_ID( "Serialization.OrphanValue", "%#: '%#' is unknown to %# or its value '%#' cannot be read", sourceName, pName, pTypeInfo->_name.c_str(), orphan._text.c_str() );
        }
        return result == SoftReadResult::Read && listOrphan.empty();
    }

    bool ReflectedXMLFile::saveDescInternal( const AbsolutePath& absPath, const void* pDesc, const void* pParent, const string_view baseID, const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR_ID( "Reflection.TypeInfoMissing", "TypeInfo not found (%#) - reflection generation is missing", absPath.str() );
            return false;
        }

        string deltaText;
        if ( DataInheritance::makeDelta( XMLSerializer::serialize( pDesc, *pTypeInfo ), XMLSerializer::serialize( pParent, *pTypeInfo ), DataInheritanceSchema::makeFromType( *pTypeInfo ), baseID, deltaText ) == false ||
             FileUtil::ensureParentDirectoryExists( absPath.str() ) == false || FileUtil::writeTextFile( absPath.str(), deltaText ) == false )
        {
            SW_LOG_ERROR_ID( "Xml.WriteFailed", "XML write failed: %# (%#)", absPath.str(), pTypeInfo->_name.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
