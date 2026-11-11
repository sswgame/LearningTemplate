#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Catalog/HousingPart.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct HousingPartInternal
        {
            /** @brief 소켓이 점유 상자 경계에 놓일 때 받는 여유(m)입니다. 경계 위의 소켓(벽 끝)은 안이다. */
            static constexpr float32 kBoundsEpsilon = 1.0e-3f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    HousingPartCatalog::HousingPartCatalog()
        : _catalog{}
        , _root{}
    {
    }

    bool HousingPartCatalog::addPart( const HousingPartDef& part )
    {
        return _catalog.add( part ) >= 0; // 겹친 id 는 GameCatalog 가 알리고 거절한다(앞 부품을 지킨다)
    }

    const HousingPartDef* HousingPartCatalog::findReplacement( const hashed_string& removedID ) const
    {
        if ( removedID.empty() )
            return nullptr;
        return _catalog.findIf( [&removedID]( const HousingPartDef& part )
        { return part._replacedID == removedID; } );
    }

    bool HousingPartCatalog::checkPart( const HousingPartDef& part, vector<string>& outListProblem ) const
    {
        using Internal          = HousingPartInternal;
        const size_t firstCount = outListProblem.size();
        if ( part._minLoadVersion > part._version )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder3;
            textBuilder3.appendFormat( "part '%#' has minLoadVersion %# above its version %#", part._id.c_str(), part._minLoadVersion, part._version );
            outListProblem.push_back( string( textBuilder3.view() ) );
        }
        if ( part._replacedID.empty() == false && findPart( part._replacedID ) != nullptr )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder2;
            textBuilder2.appendFormat( "part '%#' replaces '%#', which is still a part of this catalog", part._id.c_str(), part._replacedID.c_str() );
            outListProblem.push_back( string( textBuilder2.view() ) );
        }
        // 소켓은 점유 상자(회전 0) 안이다 — 밖의 소켓에 붙은 부품은 자기 칸 밖에 놓여 겹침 검사와 어긋난다.
        const float32 halfX = static_cast<float32>( part._sizeX ) * _root._cellSize * 0.5f + Internal::kBoundsEpsilon;
        const float32 halfZ = static_cast<float32>( part._sizeZ ) * _root._cellSize * 0.5f + Internal::kBoundsEpsilon;
        for ( const HousingSocketDef& socket : part._listSocket )
        {
            const float3& position = socket._position;
            const bool    bInside  = MathUtil::abs( position._x ) <= halfX && MathUtil::abs( position._z ) <= halfZ && position._y >= -Internal::kBoundsEpsilon &&
                                 position._y <= part._height + Internal::kBoundsEpsilon;
            if ( bInside == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "part '%#' has socket '%#' at %#,%#,%# outside its %#x%# footprint and %# m height", part._id.c_str(), socket._kind.c_str(),
                                          position._x, position._y, position._z, part._sizeX, part._sizeZ, part._height );
                outListProblem.push_back( string( textBuilder.view() ) );
            }
        }
        if ( part._snapKind.empty() == false )
        {
            bool bKnownKind = false;
            for ( const HousingPartDef& other : getParts() )
            {
                for ( const HousingSocketDef& socket : other._listSocket )
                    bKnownKind = bKnownKind || socket._kind == part._snapKind;
            }
            if ( bKnownKind == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "part '%#' snaps to socket kind '%#', which no part of this catalog has", part._id.c_str(), part._snapKind.c_str() );
                outListProblem.push_back( string( textBuilder.view() ) );
            }
        }
        return outListProblem.size() == firstCount;
    }

    uint32 HousingPartCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        if ( XMLFields::readProperties( root, _root, sourceName ) == false )
            return 0;
        using Element                          = XMLCatalogElement<HousingPartCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&HousingPartCatalog::loadPart>( "Part" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        // 스냅 지점 · 판 · 이어받기는 모든 부품을 읽은 뒤에 본다(앞 부품이 뒤 부품의 소켓 종류를 가리킬 수 있다). 하나라도 틀리면 파일 전체를 거절한다 — 틀린 부품을 빼고 쓰면 그 부품의 세이브가 조용히 사라진다.
        vector<string> listProblem;
        for ( const HousingPartDef& part : getParts() )
            (void)checkPart( part, listProblem );
        for ( const string& problem : listProblem )
            SW_LOG_ERROR_ID( "HousingPartCatalog.PartInvalid", "%#: %#", sourceName, problem );
        return listProblem.empty() ? result._loadedCount : 0;
    }

    bool HousingPartCatalog::loadPart( const XMLNode& node, string_view sourceName )
    {
        HousingPartDef part;
        bool           bValid = XMLFields::readProperties( node, part, sourceName );
        for ( XMLNode socketNode = node.findChild( "Socket" ); socketNode; socketNode = socketNode.findNextSibling( "Socket" ) )
        {
            HousingSocketDef socket;
            if ( XMLFields::readProperties( socketNode, socket, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            part._listSocket.push_back( socket );
        }
        return bValid && addPart( part );
    }
} // namespace sw

namespace sw
{
    /** @brief `HousingPartCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( HousingParts, HousingPartCatalog, SW_KIT_API, HousingPartCatalog::kAssetType.withLoader( AssetTypeLoader::of<HousingPartCatalog>( MemoryTag::Game ) ) );
} // namespace sw
