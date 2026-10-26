#include "pch.h"

#include "Engine/Config/PackEngineSettings.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "PackEngineSettings" );

    bool PackEngineSettings::loadFromPack( const DataPath& packRoot )
    {
        const string path = FileUtil::joinPath( FileUtil::trimTrailingSlashes( packRoot.get() ), kPackRelativePath );
        string       text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "%#: the pack settings file could not be read", path.c_str() );
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool PackEngineSettings::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        const string label( sourceName );
        XMLDocument  document;
        if ( document.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "%#: %#", label.c_str(), document.getLastError().c_str() );
            return false;
        }
        const XMLNode root    = document.getRoot( kRootElementName );
        const XMLNode element = root.isValid() ? root.findChild( kElementName ) : XMLNode{};
        if ( element.isValid() == false )
        {
            SW_LOG_ERROR( "%#: <%#><%#> is missing - the pack names its window title and engine settings there", label.c_str(), kRootElementName, kElementName );
            return false;
        }
        // 먼저 비운다 — 다른 팩을 읽은 값이 이 팩에 없는 칸으로 남지 않게.
        *this = PackEngineSettings{};
        if ( ConfigManager::readConfigXMLElement( this, *StaticType(), element.toString(), label.c_str() ) == false )
            return false;

        // 경로 칸이 가리키는 파일이 있어야 한다. 이 호스트가 읽지 않는 에셋 종류(전용 서버의 UI 문서)는 팩에 없으므로 보지 않는다.
        // Shipping 은 쿠킹이 팩의 짝을 지킨다(`EngineDefaultAssets::loadFromResource` 와 같다).
        bool bComplete = true;
#if !defined( SW_SHIPPING )
        const hashed_string dataPathType( "DataPath" );
        for ( const PropertyInfo& prop : StaticType()->getPropertiesWithBase() )
        {
            if ( prop._typeName != dataPathType )
                continue;
            const DataPath* pValue    = prop.getValuePtr<DataPath>( this );
            const bool      bNotSet   = pValue == nullptr || pValue->isSet() == false;
            const bool      bSkipHost = bNotSet || ResourcePaths::isExcludedForHost( pValue->get() );
            if ( bSkipHost || ResourceIO::hasResource( pValue->get() ) )
                continue;
            SW_LOG_ERROR( "%#: <%# %#> points to '%#', which does not exist", label.c_str(), kElementName, prop._name.c_str(), pValue->c_str() );
            bComplete = false;
        }
#endif
        return bComplete;
    }

    const string& PackEngineSettings::resolve( const DataPath& packValue, const DataPath& engineValue )
    {
        return packValue.isSet() ? packValue.get() : engineValue.get();
    }
} // namespace sw
