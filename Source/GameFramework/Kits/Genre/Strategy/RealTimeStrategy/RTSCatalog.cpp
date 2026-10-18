#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "RTSCatalog" );

    RTSCatalog::RTSCatalog()
        : _supplyMax{ RTSCatalogRootDef{}._supplyMax }
    {
    }

    void RTSCatalog::findProducts( const hashed_string& producerID, vector<const RTSUnitDef*>& outListDef ) const
    {
        outListDef.clear();
        for ( const RTSUnitDef& def : getAll() )
        {
            if ( def._producedBy == producerID )
                outListDef.push_back( &def );
        }
    }

    uint32 RTSCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        RTSCatalogRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _supplyMax = rootDef._supplyMax;
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Unit", true ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadUnit( node, sourceName ) )
                ++loadedCount;
        }
        for ( const RTSUnitDef& def : getAll() )
        {
            if ( def._producedBy.empty() == false && find( def._producedBy ) == nullptr )
                SW_LOG_ERROR( "%#: '%#' is produced by unknown '%#'", sourceName, def._id.c_str(), def._producedBy.c_str() );
            if ( def._requires.empty() == false && find( def._requires ) == nullptr )
                SW_LOG_ERROR( "%#: '%#' requires unknown '%#'", sourceName, def._id.c_str(), def._requires.c_str() );
        }
        return loadedCount;
    }

    bool RTSCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        RTSUnitDef def;
        bool       bValid = GameDataXML::readFields( node, def, sourceName, { "targets" } );
        // 공격 대상은 적었으면 그것만이다(적지 않으면 지상). 모르는 이름은 오류다.
        const string_view targets = node.getAttributeText( "targets" );
        if ( targets.empty() == false )
        {
            def._bTargetsGround = false;
            StringUtil::forEachToken( targets, ",; ", [&]( string_view token )
            {
                if ( StringUtil::equals( token, string_view( "Ground" ), true ) )
                    def._bTargetsGround = true;
                else if ( StringUtil::equals( token, string_view( "Air" ), true ) )
                    def._bTargetsAir = true;
                else
                {
                    SW_LOG_ERROR( "%#: '%#' has an unknown target '%#' (Ground, Air)", sourceName, def._id.c_str(), token );
                    bValid = false;
                }
            } );
        }
        // 건물과 자원의 몸 반지름은 막는 셀(`footprint`)이 정한다 — 두 값이 어긋나지 않게 데이터에 따로 적지 않는다.
        if ( def._kind != RTSUnitKind::Unit )
            def._radius = static_cast<float32>( def._footprint ) * 0.5f;
        if ( bValid )
            (void)add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
