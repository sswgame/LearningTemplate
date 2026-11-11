#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    uint32 MatchModeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<MatchModeCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&MatchModeCatalog::loadMode>( "Mode" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool MatchModeCatalog::loadMode( const XMLNode& node, string_view sourceName )
    {
        // 정의의 id 필드(`_modeID`)은 와이어 · 저장 이름이라 데이터의 `id` 속성은 손으로 읽는다.
        MatchModeDef mode;
        bool         bValid = XMLFields::readProperties( node, mode, sourceName, { "id" } );
        const utf8*  pID    = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_ERROR_ID( "MatchModeCatalog.ModeNeedsId", "%#: <Mode> needs id=\"...\"", sourceName );
            return false;
        }
        mode._modeID = pID;
        if ( bValid )
            _listMode.push_back( std::move( mode ) );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MatchModeCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( MatchModeCatalog, MatchModeCatalog, SW_KIT_SERVER_API, MatchModeCatalog::kAssetType.withLoader( AssetTypeLoader::of<MatchModeCatalog>( MemoryTag::Game ) ) );
} // namespace sw
