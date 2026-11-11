#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        /** @brief 연금술 물건 하나를 읽습니다. 오일은 칼에 바른 뒤 적중 횟수(`hits`)가 1 이상이어야 합니다. */
        [[nodiscard]] bool readAlchemyItem( const XMLNode& node, string_view sourceName, MonsterHuntAlchemyDef& outItem )
        {
            if ( XMLFields::readProperties( node, outItem, sourceName ) == false )
                return false;
            if ( outItem._kind == MonsterHuntAlchemyKind::Oil && outItem._hits < 1 )
            {
                SW_LOG_ERROR_ID( "MonsterHuntCatalog.OilNeedsHitsMore", "%#: oil '%#' needs hits=\"...\" of 1 or more", sourceName, outItem._id.c_str() );
                return false;
            }
            return true;
        }
    } // namespace

    MonsterHuntCatalog::MonsterHuntCatalog()
        : _monsterCatalog{}
        , _alchemyCatalog{}
        , _signCatalog{}
        , _mutagenCatalog{}
        , _contractCatalog{}
        , _skillColorCatalog{}
        , _listSlotGroup{}
        , _combat{}
        , _alchemy{}
    {
    }

    hashed_string MonsterHuntCatalog::getSkillColor( const hashed_string& skillID ) const
    {
        const MonsterHuntSkillColorDef* pColor = _skillColorCatalog.find( skillID );
        return pColor != nullptr ? pColor->_color : hashed_string{};
    }

    uint32 MonsterHuntCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<MonsterHuntCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&MonsterHuntCatalog::_alchemy>( "Alchemy" ),
            Element::of<&MonsterHuntCatalog::_combat>( "Combat" ),
            Element::of<&MonsterHuntCatalog::_listSlotGroup>( "SlotGroup" ),
            Element::of<&MonsterHuntCatalog::_skillColorCatalog>( "SkillColor" ),
            Element::of<&MonsterHuntCatalog::loadMonster>( "Monster" ),
            Element::of<&MonsterHuntCatalog::loadAlchemyItem>( "Item" ),
            Element::of<&MonsterHuntCatalog::_signCatalog>( "Sign" ),
            Element::of<&MonsterHuntCatalog::_mutagenCatalog>( "Mutagen" ),
            Element::of<&MonsterHuntCatalog::loadContract>( "Contract" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        // 약점은 종류(`kind`)에 따라 표식 · 연금술 물건을 가리킨다 — 갈래가 값에 달려 참조 표 한 줄로 적을 수 없어 여기서 본다. 정책은 표와 같다: 깨진 필수 참조는 로드 실패다
        // (예전에는 오류만 남기고 항목을 둔 채 로드가 성공했다).
        bool bReferenced = true;
        for ( const MonsterHuntMonsterDef& monster : _monsterCatalog.getAll() )
        {
            for ( const MonsterHuntWeakness& weakness : monster._listWeakness )
            {
                const bool bSign = weakness._kind == MonsterHuntWeaknessKind::Sign;
                if ( bSign ? _signCatalog.find( weakness._id ) != nullptr : _alchemyCatalog.find( weakness._id ) != nullptr )
                    continue;
                SW_LOG_ERROR_ID( "MonsterHuntCatalog.MonsterNamesUnknownWeakness", "%#: monster '%#' names an unknown weakness '%#' - the catalog does not load", sourceName, monster._id.c_str(),
                                 weakness._id.c_str() );
                bReferenced = false;
            }
        }
        return bReferenced ? result._loadedCount : 0;
    }

    bool MonsterHuntCatalog::loadAlchemyItem( const XMLNode& node, string_view sourceName )
    {
        MonsterHuntAlchemyDef item;
        if ( readAlchemyItem( node, sourceName, item ) == false )
            return false;
        (void)_alchemyCatalog.add( item );
        return true;
    }

    bool MonsterHuntCatalog::loadMonster( const XMLNode& node, string_view sourceName )
    {
        MonsterHuntMonsterDef monster;
        bool                  bValid = XMLFields::readProperties( node, monster, sourceName );
        if ( monster._listElement.empty() && monster._category.empty() == false )
            monster._listElement.push_back( monster._category ); // 방어 속성을 적지 않으면 분류 하나다(`_listElement` 의 정의)
        for ( XMLNode child = node.findChild( "Weakness" ); child; child = child.findNextSibling( "Weakness" ) )
        {
            MonsterHuntWeakness weakness;
            if ( XMLFields::readProperties( child, weakness, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            if ( weakness._knowledge > monster._maxKnowledge )
            {
                SW_LOG_ERROR_ID( "MonsterHuntCatalog.MonsterWeaknessNeedsKnowledge", "%#: monster '%#' weakness '%#' needs knowledge %# above maxKnowledge %#", sourceName, monster._id.c_str(),
                                 weakness._id.c_str(), weakness._knowledge, monster._maxKnowledge );
                bValid = false;
                continue;
            }
            monster._listWeakness.push_back( weakness );
        }
        if ( bValid )
            (void)_monsterCatalog.add( monster );
        return bValid;
    }

    bool MonsterHuntCatalog::loadContract( const XMLNode& node, string_view sourceName )
    {
        MonsterHuntContractDef contract;
        bool                   bValid = XMLFields::readProperties( node, contract, sourceName );
        for ( XMLNode stepNode = node.findChild( "Step" ); stepNode; stepNode = stepNode.findNextSibling( "Step" ) )
        {
            MonsterHuntContractStepDef step;
            bValid = XMLFields::readProperties( stepNode, step, sourceName ) && bValid;
            for ( XMLNode clueNode = stepNode.findChild( "Clue" ); clueNode; clueNode = clueNode.findNextSibling( "Clue" ) )
            {
                MonsterHuntClueDef clue;
                if ( XMLFields::readProperties( clueNode, clue, sourceName ) )
                    step._listClue.push_back( clue );
                else
                    bValid = false;
            }
            contract._listStep.push_back( step );
        }
        if ( bValid )
            (void)_contractCatalog.add( contract );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MonsterHuntCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( MonsterHuntCatalog, MonsterHuntCatalog, SW_KIT_API, MonsterHuntCatalog::kAssetType.withLoader( AssetTypeLoader::of<MonsterHuntCatalog>( MemoryTag::Game ) ) );
} // namespace sw
