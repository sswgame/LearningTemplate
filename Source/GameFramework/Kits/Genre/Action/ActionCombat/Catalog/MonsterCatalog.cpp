#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "MonsterCatalog" );

    MonsterCatalog::MonsterCatalog()
        : _mapMonster{}
    {
        // 읽기 전에도 비어 있지 않다 — 데이터가 늦게 오거나 못 와도 `findMonster` 가 널만 돌려주지 않는다.
        seedFallback();
    }

    MonsterCatalog::~MonsterCatalog()
    {
        clear();
    }

    void MonsterCatalog::seedFallback()
    {
        _mapMonster.clear();

        MonsterDef defaultMonster;
        defaultMonster._id             = "default_monster";
        defaultMonster._name           = "Default Monster";
        defaultMonster._archetype      = MonsterArchetype::MeleePatrol;
        defaultMonster._hp             = 100;
        defaultMonster._maxHp          = 100;
        defaultMonster._atk            = 10;
        defaultMonster._def            = 0;
        defaultMonster._speed          = 3.0f;
        defaultMonster._patrolRange    = 4.0f;
        defaultMonster._detectRange    = 8.0f;
        defaultMonster._attackRange    = 1.0f;
        defaultMonster._attackCoolTime = 1.5f;

        _mapMonster[hashed_string( defaultMonster._id.c_str() )] = defaultMonster;
    }

    bool MonsterCatalog::loadFromResource( string_view assetRelativePath )
    {
        clear();
        if ( XMLCatalog<MonsterCatalog>::loadFromResource( assetRelativePath ) )
            return true;
        // 파일 · 루트가 없거나 `<Monster>` 가 하나도 없다(읽기 쪽이 이미 까닭을 알렸다) — 폴백을 심는다.
        SW_LOG_WARNING( "%# gave no monster definitions — using fallback monster definitions.", assetRelativePath );
        seedFallback();
        return false;
    }

    uint32 MonsterCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        for ( XMLNode node = root.findChild( "Monster" ); node; node = node.findNextSibling( "Monster" ) )
        {
            const utf8* pIdStr = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pIdStr ) )
                continue;

            MonsterDef monsterDef;
            monsterDef._id       = pIdStr;
            const utf8* pNameStr = node.findAttribute( "name" );
            if ( pNameStr != nullptr )
                monsterDef._name = pNameStr;
            else
                monsterDef._name = monsterDef._id;

            monsterDef._archetype = parseArchetype( node.findAttribute( "archetype" ), pIdStr );

            XMLNode statsNode = node.findChild( "Stats" );
            if ( statsNode.isValid() )
            {
                monsterDef._hp            = statsNode.getAttributeInt( "hp", monsterDef._hp );
                monsterDef._maxHp         = statsNode.getAttributeInt( "maxHp", monsterDef._maxHp );
                monsterDef._atk           = statsNode.getAttributeInt( "atk", monsterDef._atk );
                monsterDef._def           = statsNode.getAttributeInt( "def", monsterDef._def );
                monsterDef._speed         = statsNode.getAttributeFloat( "speed", monsterDef._speed );
                monsterDef._invincibility = statsNode.getAttributeFloat( "invincibility", monsterDef._invincibility );
                monsterDef._radius        = statsNode.getAttributeFloat( "radius", monsterDef._radius );
                if ( monsterDef._speed > MonsterDef::kMaxSpeed )
                    SW_LOG_WARNING( "Monster '%#': speed %# is above %# m/s - the catalog is in meters, was it written in pixels?", pIdStr, monsterDef._speed,
                                    MonsterDef::kMaxSpeed );
            }

            XMLNode aiNode = node.findChild( "AI" );
            if ( aiNode.isValid() )
            {
                monsterDef._patrolRange      = aiNode.getAttributeFloat( "patrolRange", monsterDef._patrolRange );
                monsterDef._detectRange      = aiNode.getAttributeFloat( "detectRange", monsterDef._detectRange );
                monsterDef._attackRange      = aiNode.getAttributeFloat( "attackRange", monsterDef._attackRange );
                monsterDef._attackCoolTime   = aiNode.getAttributeFloat( "coolTime", monsterDef._attackCoolTime );
                monsterDef._firstAttackDelay = aiNode.getAttributeFloat( "firstDelay", monsterDef._firstAttackDelay );
                const utf8* pProj            = aiNode.findAttribute( "projectilePrefab" );
                if ( pProj != nullptr )
                    monsterDef._projectilePrefab = pProj;
            }

            // 사격은 줄마다 한 발이다 — 한 번에 쏘는 패턴(겨냥 · 옆 · 부채)을 코드 없이 적는다.
            for ( XMLNode shotNode = node.findChild( "Shot" ); shotNode; shotNode = shotNode.findNextSibling( "Shot" ) )
            {
                MonsterShotDef shot;
                shot._angleDegrees = shotNode.getAttributeFloat( "angle", shot._angleDegrees );
                shot._speed        = shotNode.getAttributeFloat( "speed", shot._speed );
                shot._lifeTime     = shotNode.getAttributeFloat( "life", shot._lifeTime );
                shot._radius       = shotNode.getAttributeFloat( "radius", shot._radius );
                shot._damage       = shotNode.getAttributeInt( "damage", shot._damage );
                monsterDef._listShot.push_back( shot );
            }

            XMLNode prefabNode = node.findChild( "Prefab" );
            if ( prefabNode.isValid() )
            {
                const utf8* pPath = prefabNode.findAttribute( "path" );
                if ( pPath != nullptr )
                    monsterDef._prefabPath = pPath;
            }

            XMLNode dropNode = node.findChild( "Drop" );
            if ( dropNode.isValid() )
            {
                // 속성 이름이 곧 보상 이름이다. 코드가 보상 종류를 알 필요가 없다.
                for ( XMLAttribute attr = dropNode.getFirstAttribute(); attr.isValid(); attr = attr.getNext() )
                {
                    const utf8* pRewardId = attr.getName();
                    if ( StringUtil::isNullOrEmpty( pRewardId ) )
                        continue;
                    monsterDef._mapDrop.insert_or_assign( hashed_string( pRewardId ),
                                                          dropNode.getAttributeInt( pRewardId, 0 ) );
                }
            }

            _mapMonster[hashed_string( monsterDef._id.c_str() )] = monsterDef;
        }

        // **읽었는데 하나도 없으면 실패다**(0 — 부르는 쪽이 폴백을 심는다). 주의: 성공으로 넘기면 `<Monster>` 를 `<monster>` 로 적은 오타 하나가
        // **텅 빈 카탈로그**가 되고, 모든 `findMonster` 가 nullptr 이라 그 널을 다루는 쪽이 조용히 아무것도 안 한다.
        if ( _mapMonster.empty() )
        {
            SW_LOG_WARNING( "No <Monster> entries in %#", sourceName );
            return 0;
        }

        SW_LOG_INFO( "Loaded %# monster definitions from %#", static_cast<int32>( _mapMonster.size() ), sourceName );
        return static_cast<uint32>( _mapMonster.size() );
    }

    const MonsterDef* MonsterCatalog::findMonster( const hashed_string& id ) const
    {
        auto mapIter = _mapMonster.find( id );
        if ( mapIter != _mapMonster.end() )
            return &mapIter->second;
        return nullptr;
    }

    const unordered_map<hashed_string, MonsterDef>& MonsterCatalog::getAllMonsters() const
    {
        return _mapMonster;
    }

    void MonsterCatalog::clear()
    {
        _mapMonster.clear();
    }

    MonsterArchetype MonsterCatalog::parseArchetype( const utf8* pStr, const utf8* pMonsterId )
    {
        if ( pStr == nullptr )
            return MonsterArchetype::MeleePatrol;

        // 이름표는 리플렉션된 열거자 하나다(대소문자 무시). 열거자를 늘리면 여기는 고치지 않는다.
        MonsterArchetype archetype{ MonsterArchetype::MeleePatrol };
        if ( engine::getTypeRegistry().enumFromString( pStr, archetype ) )
            return archetype;

        SW_LOG_WARNING( "Monster '%#': unknown archetype '%#' — using MeleePatrol", pMonsterId, pStr );
        return MonsterArchetype::MeleePatrol;
    }
} // namespace sw
