#include "pch.h"

#include "GameFramework/Kits/ActionCombat/MonsterDataCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "MonsterDataCatalog" );

    MonsterDataCatalog::MonsterDataCatalog()
        : _mapMonster{}
    {
    }

    MonsterDataCatalog::~MonsterDataCatalog()
    {
        clear();
    }

    void MonsterDataCatalog::seedFallback()
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
        defaultMonster._speed          = 150.0f;
        defaultMonster._patrolRange    = 200.0f;
        defaultMonster._detectRange    = 400.0f;
        defaultMonster._attackRange    = 50.0f;
        defaultMonster._attackCoolTime = 1.5f;

        _mapMonster[hashed_string( defaultMonster._id.c_str() )] = defaultMonster;
    }

    bool MonsterDataCatalog::loadFromResource( string_view assetRelativePath )
    {
        clear();

        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read %# — using fallback monster definitions.", assetRelativePath );
            seedFallback();
            return false;
        }

        XmlNode root = doc.root( "MonsterCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <MonsterCatalog> root in %# — using fallback.", absPath );
            seedFallback();
            return false;
        }

        for ( XmlNode node = root.child( "Monster" ); node; node = node.next( "Monster" ) )
        {
            const utf8* pIdStr = node.attribute( "id" );
            if ( StringUtil::isNullOrEmpty( pIdStr ) )
                continue;

            MonsterDef monsterDef;
            monsterDef._id       = pIdStr;
            const utf8* pNameStr = node.attribute( "name" );
            if ( pNameStr != nullptr )
                monsterDef._name = pNameStr;
            else
                monsterDef._name = monsterDef._id;

            monsterDef._archetype = parseArchetype( node.attribute( "archetype" ) );

            XmlNode statsNode = node.child( "Stats" );
            if ( statsNode.isValid() )
            {
                monsterDef._hp            = statsNode.attributeInt( "hp", monsterDef._hp );
                monsterDef._maxHp         = statsNode.attributeInt( "maxHp", monsterDef._maxHp );
                monsterDef._atk           = statsNode.attributeInt( "atk", monsterDef._atk );
                monsterDef._def           = statsNode.attributeInt( "def", monsterDef._def );
                monsterDef._speed         = statsNode.attributeFloat( "speed", monsterDef._speed );
                monsterDef._invincibility = statsNode.attributeFloat( "invincibility", monsterDef._invincibility );
            }

            XmlNode aiNode = node.child( "AI" );
            if ( aiNode.isValid() )
            {
                monsterDef._patrolRange    = aiNode.attributeFloat( "patrolRange", monsterDef._patrolRange );
                monsterDef._detectRange    = aiNode.attributeFloat( "detectRange", monsterDef._detectRange );
                monsterDef._attackRange    = aiNode.attributeFloat( "attackRange", monsterDef._attackRange );
                monsterDef._attackCoolTime = aiNode.attributeFloat( "coolTime", monsterDef._attackCoolTime );
                const utf8* pProj          = aiNode.attribute( "projectilePrefab" );
                if ( pProj != nullptr )
                    monsterDef._projectilePrefab = pProj;
            }

            XmlNode prefabNode = node.child( "Prefab" );
            if ( prefabNode.isValid() )
            {
                const utf8* pPath = prefabNode.attribute( "path" );
                if ( pPath != nullptr )
                    monsterDef._prefabPath = pPath;
            }

            XmlNode dropNode = node.child( "Drop" );
            if ( dropNode.isValid() )
            {
                // 속성 이름이 곧 보상 이름이다 — 코드가 보상 종류를 알 필요가 없다.
                for ( XmlAttribute attr = dropNode.firstAttribute(); attr.isValid(); attr = attr.next() )
                {
                    const utf8* pRewardId = attr.name();
                    if ( StringUtil::isNullOrEmpty( pRewardId ) )
                        continue;
                    monsterDef._mapDrop.insert_or_assign( hashed_string( pRewardId ),
                                                          dropNode.attributeInt( pRewardId, 0 ) );
                }
            }

            _mapMonster[hashed_string( monsterDef._id.c_str() )] = monsterDef;
        }

        SW_LOG_INFO( "Loaded %# monster definitions from %#", static_cast<int32>( _mapMonster.size() ), absPath );
        return true;
    }

    const MonsterDef* MonsterDataCatalog::findMonster( const hashed_string& id ) const
    {
        auto mapIter = _mapMonster.find( id );
        if ( mapIter != _mapMonster.end() )
            return &mapIter->second;
        return nullptr;
    }

    const MonsterDef* MonsterDataCatalog::findMonster( const string& id ) const
    {
        return findMonster( hashed_string( id.c_str() ) );
    }

    const unordered_map<hashed_string, MonsterDef>& MonsterDataCatalog::getAllMonsters() const
    {
        return _mapMonster;
    }

    void MonsterDataCatalog::clear()
    {
        _mapMonster.clear();
    }

    MonsterArchetype MonsterDataCatalog::parseArchetype( const utf8* pStr )
    {
        if ( pStr == nullptr )
            return MonsterArchetype::MeleePatrol;

        if ( StringUtil::equals( pStr, "RangedShooter", true ) )
            return MonsterArchetype::RangedShooter;
        if ( StringUtil::equals( pStr, "FlyingPursuer", true ) )
            return MonsterArchetype::FlyingPursuer;
        if ( StringUtil::equals( pStr, "ChargerRush", true ) )
            return MonsterArchetype::ChargerRush;

        return MonsterArchetype::MeleePatrol;
    }
} // namespace sw
