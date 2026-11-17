#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    RTSCatalog::RTSCatalog()
        : _rules{}
        , _supplyMax{ RTSCatalogRootDef{}._supplyMax }
        , _rulesCount{ 0 }
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

    uint32 RTSCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        RTSCatalogRootDef rootDef;
        if ( root.readProperties( rootDef, sourceName ) )
            _supplyMax = rootDef._supplyMax;
        using Element                          = XMLCatalogElement<RTSCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&RTSCatalog::loadRules>( "Rules" ),
            Element::of<&RTSCatalog::loadUnit>( "Unit" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        // 규칙은 데이터에만 있다 — 한 번도 `<Rules>` 를 읽지 못한 카탈로그는 로드 실패다(월드가 C++ 기본값으로 열리지 않는다, 감사 K-4).
        if ( _rulesCount == 0 )
        {
            SW_LOG_ERROR_ID( "RTSCatalog.RulesMissing", "%#: <RTSCatalog> needs one <Rules .../> line (every rule value is data)", sourceName );
            return 0;
        }
        // 생산자 · 선행 유닛은 참조 표(`engine/settings/datacatalogreferences.xml`)가 보고 없으면 로드를 멈춘다(예전에는 오류만 남기고 로드가 성공했다).
        return result._loadedCount;
    }

    bool RTSCatalog::loadRules( const XMLNode& node, string_view sourceName )
    {
        RTSSettings rules;
        if ( XMLFields::readProperties( node, rules, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "RTSCatalog.RulesInvalid", "%#: <Rules> has missing or out-of-range values", sourceName );
            return false;
        }
        _rules = rules; // 덧씌우는 파일(합치기)은 규칙 전체를 바꾼다 — 칸 일부만 두지 않는다
        ++_rulesCount;
        return true;
    }

    bool RTSCatalog::loadUnit( const XMLNode& node, string_view sourceName )
    {
        RTSUnitDef def;
        bool       bValid = XMLFields::readProperties( node, def, sourceName, { "targets" } );
        // 공격 대상은 적었으면 그것만이다(적지 않으면 지상). 모르는 이름은 오류다.
        const string_view targets = node.getAttributeText( "targets" );
        if ( targets.empty() == false )
        {
            def._bTargetsGround = false;
            StringUtil::forEachToken( targets, ",; ", [&]( string_view token )
            {
                if ( StringUtil::equals( token, string_view( "Ground" ) ) )
                    def._bTargetsGround = true;
                else if ( StringUtil::equals( token, string_view( "Air" ) ) )
                    def._bTargetsAir = true;
                else
                {
                    SW_LOG_ERROR_ID( "RTSCatalog.UnknownTargetGroundAir", "%#: '%#' has an unknown target '%#' (Ground, Air)", sourceName, def._id.c_str(), token );
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

namespace sw
{
    /** @brief `RTSCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( RTSUnits, RTSCatalog, SW_KIT_API, RTSCatalog::kAssetType.withLoader( AssetTypeLoader::of<RTSCatalog>( MemoryTag::Game ) ) );
} // namespace sw
