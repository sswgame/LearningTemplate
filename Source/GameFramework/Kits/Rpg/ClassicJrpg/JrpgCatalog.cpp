#include "pch.h"

#include "GameFramework/Kits/Rpg/ClassicJrpg/JrpgCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct JrpgCatalogInternal
        {
            static constexpr const utf8* kArrStatName[kJrpgStatCount]   = { "hp", "mp", "str", "agi", "vit", "intellect", "luck" };
            static constexpr const utf8* kArrGrowthName[kJrpgStatCount] = { "growHp", "growMp", "growStr", "growAgi", "growVit", "growIntellect", "growLuck" };

            static void parseNameList( string_view text, vector<hashed_string>& outListName )
            {
                GameDataXml::forEachToken( text, ",; \t", [&]( string_view token )
                { outListName.push_back( hashed_string( token ) ); } );
            }

            static void parseStats( const XmlNode& node, const utf8* const ( &arrName )[kJrpgStatCount], int32 ( &inoutArrValue )[kJrpgStatCount] )
            {
                for ( int32 index = 0; index < kJrpgStatCount; ++index )
                    inoutArrValue[index] = MathUtil::max( 0, node.getAttributeInt( arrName[index], inoutArrValue[index] ) );
            }

            static JrpgTargetKind parseTarget( string_view text )
            {
                return StringUtil::equals( text, string_view( "All" ), true ) ? JrpgTargetKind::All : JrpgTargetKind::One;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "JrpgCatalog" );

    JrpgCatalog::JrpgCatalog()
        : _classCatalog{}
        , _spellCatalog{}
        , _comboCatalog{}
        , _manualCatalog{}
        , _enemyCatalog{}
        , _areaCatalog{}
        , _curve{}
    {
    }

    bool JrpgCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "JrpgCatalog", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool JrpgCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "JrpgCatalog", root ) && loadRoot( root, sourceName ) > 0;
    }

    void JrpgCatalog::clear()
    {
        _classCatalog.clear();
        _spellCatalog.clear();
        _comboCatalog.clear();
        _manualCatalog.clear();
        _enemyCatalog.clear();
        _areaCatalog.clear();
        _curve = ExperienceCurve{};
    }

    uint32 JrpgCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const XmlNode curve = root.findChild( "ExperienceCurve" );
        if ( curve )
            _curve.loadFromNode( curve );

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName   = node.getName();
            const bool  bClass  = StringUtil::equals( pName, "Class", true );
            const bool  bSpell  = StringUtil::equals( pName, "Spell", true );
            const bool  bCombo  = StringUtil::equals( pName, "Combo", true );
            const bool  bManual = StringUtil::equals( pName, "Manual", true );
            const bool  bEnemy  = StringUtil::equals( pName, "Enemy", true );
            const bool  bArea   = StringUtil::equals( pName, "Area", true );
            if ( ( bClass || bSpell || bCombo || bManual || bEnemy || bArea ) == false )
                continue;
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;

            if ( bClass )
            {
                loadClass( node, pId );
            }
            else if ( bSpell )
            {
                loadSpell( node, pId, sourceName );
            }
            else if ( bCombo )
            {
                JrpgComboDef def;
                def._id           = hashed_string( pId );
                const utf8* pText = node.findAttribute( "name" );
                def._name         = pText != nullptr ? pText : pId;
                def._points       = MathUtil::max( 0, node.getAttributeInt( "points", def._points ) );
                def._power        = MathUtil::max( 0, node.getAttributeInt( "power", def._power ) );
                def._target       = JrpgCatalogInternal::parseTarget( node.getAttributeText( "target" ) );
                JrpgCatalogInternal::parseNameList( node.getAttributeText( "members" ), def._listMemberId );
                JrpgCatalogInternal::parseNameList( node.getAttributeText( "types" ), def._listDamageType );
                if ( def._listMemberId.size() < 2 )
                    SW_LOG_WARNING( "%#: combo '%#' names fewer than two members", sourceName, pId );
                (void)_comboCatalog.add( def );
            }
            else if ( bManual )
            {
                JrpgManualDef def;
                def._id           = hashed_string( pId );
                const utf8* pText = node.findAttribute( "name" );
                def._name         = pText != nullptr ? pText : pId;
                for ( XmlNode stage = node.findChild( "Stage" ); stage; stage = stage.findNextSibling( "Stage" ) )
                {
                    JrpgManualStage entry;
                    entry._techniqueId = hashed_string( stage.getAttributeText( "technique" ) );
                    entry._proficiency = MathUtil::max( 0, stage.getAttributeInt( "proficiency", 0 ) );
                    if ( entry._techniqueId.empty() == false )
                        def._listStage.push_back( entry );
                }
                (void)_manualCatalog.add( def );
            }
            else if ( bEnemy )
            {
                loadEnemy( node, pId );
            }
            else
            {
                JrpgAreaDef def;
                def._id         = hashed_string( pId );
                def._rate       = MathUtil::clamp( node.getAttributeFloat( "rate", def._rate ), 0.0f, 1.0f );
                def._graceSteps = MathUtil::max( 0, node.getAttributeInt( "grace", def._graceSteps ) );
                for ( XmlNode group = node.findChild( "Group" ); group; group = group.findNextSibling( "Group" ) )
                {
                    JrpgEncounterGroup entry;
                    JrpgCatalogInternal::parseNameList( group.getAttributeText( "enemies" ), entry._listEnemyId );
                    entry._weight = MathUtil::max( 0, group.getAttributeInt( "weight", 1 ) );
                    if ( entry._listEnemyId.empty() == false )
                        def._listGroup.push_back( entry );
                }
                (void)_areaCatalog.add( def );
            }
            ++loadedCount;
        }

        // 서로 가리키는 id 는 다 읽은 뒤에 확인한다.
        for ( const JrpgClassDef& def : _classCatalog.getAll() )
        {
            for ( const JrpgLearnEntry& learn : def._listLearn )
            {
                if ( _spellCatalog.find( learn._spellId ) == nullptr )
                    SW_LOG_WARNING( "%#: class '%#' learns unknown spell '%#'", sourceName, def._id.c_str(), learn._spellId.c_str() );
            }
        }
        for ( const JrpgManualDef& def : _manualCatalog.getAll() )
        {
            for ( const JrpgManualStage& stage : def._listStage )
            {
                if ( _spellCatalog.find( stage._techniqueId ) == nullptr )
                    SW_LOG_WARNING( "%#: manual '%#' unlocks unknown technique '%#'", sourceName, def._id.c_str(), stage._techniqueId.c_str() );
            }
        }
        for ( const JrpgAreaDef& def : _areaCatalog.getAll() )
        {
            for ( const JrpgEncounterGroup& group : def._listGroup )
            {
                for ( const hashed_string& enemyId : group._listEnemyId )
                {
                    if ( _enemyCatalog.find( enemyId ) == nullptr )
                        SW_LOG_WARNING( "%#: area '%#' names unknown enemy '%#'", sourceName, def._id.c_str(), enemyId.c_str() );
                }
            }
        }
        return loadedCount;
    }

    void JrpgCatalog::loadClass( const XmlNode& node, const utf8* pId )
    {
        JrpgClassDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        def._attackType   = hashed_string( node.getAttributeText( "attackType" ) );
        def._requiredItem = hashed_string( node.getAttributeText( "requires" ) );
        JrpgCatalogInternal::parseStats( node, JrpgCatalogInternal::kArrStatName, def._arrBase );
        JrpgCatalogInternal::parseStats( node, JrpgCatalogInternal::kArrGrowthName, def._arrGrowth );
        def._arrBase[static_cast<size_t>( JrpgStat::MaxHp )] = MathUtil::max( 1, def._arrBase[static_cast<size_t>( JrpgStat::MaxHp )] );
        for ( XmlNode learn = node.findChild( "Learn" ); learn; learn = learn.findNextSibling( "Learn" ) )
        {
            JrpgLearnEntry entry;
            entry._spellId = hashed_string( learn.getAttributeText( "spell" ) );
            entry._level   = MathUtil::max( 1, learn.getAttributeInt( "level", 1 ) );
            if ( entry._spellId.empty() == false )
                def._listLearn.push_back( entry );
        }
        (void)_classCatalog.add( def );
    }

    void JrpgCatalog::loadSpell( const XmlNode& node, const utf8* pId, string_view sourceName )
    {
        JrpgSpellDef def;
        def._id                = hashed_string( pId );
        const utf8* pName      = node.findAttribute( "name" );
        def._name              = pName != nullptr ? pName : pId;
        def._damageType        = hashed_string( node.getAttributeText( "type" ) );
        def._manualId          = hashed_string( node.getAttributeText( "manual" ) );
        def._power             = MathUtil::max( 0, node.getAttributeInt( "power", def._power ) );
        def._mpCost            = MathUtil::max( 0, node.getAttributeInt( "mp", def._mpCost ) );
        def._innerCost         = MathUtil::max( 0, node.getAttributeInt( "inner", def._innerCost ) );
        def._proficiencyGain   = MathUtil::max( 0, node.getAttributeInt( "proficiency", def._proficiencyGain ) );
        def._target            = JrpgCatalogInternal::parseTarget( node.getAttributeText( "target" ) );
        const string_view kind = node.getAttributeText( "kind" );
        if ( StringUtil::equals( kind, string_view( "Heal" ), true ) )
            def._kind = JrpgSpellKind::Heal;
        else if ( StringUtil::equals( kind, string_view( "Revive" ), true ) )
            def._kind = JrpgSpellKind::Revive;
        else if ( kind.empty() == false && StringUtil::equals( kind, string_view( "Damage" ), true ) == false )
            SW_LOG_WARNING( "%#: spell '%#' has an unknown kind '%#' - read as damage", sourceName, pId, kind );
        (void)_spellCatalog.add( def );
    }

    void JrpgCatalog::loadEnemy( const XmlNode& node, const utf8* pId )
    {
        JrpgEnemyDef def;
        def._id           = hashed_string( pId );
        const utf8* pName = node.findAttribute( "name" );
        def._name         = pName != nullptr ? pName : pId;
        def._attackType   = hashed_string( node.getAttributeText( "attackType" ) );
        def._castSpellId  = hashed_string( node.getAttributeText( "cast" ) );
        JrpgCatalogInternal::parseStats( node, JrpgCatalogInternal::kArrStatName, def._arrStat );
        def._arrStat[static_cast<size_t>( JrpgStat::MaxHp )] = MathUtil::max( 1, def._arrStat[static_cast<size_t>( JrpgStat::MaxHp )] );
        JrpgCatalogInternal::parseNameList( node.getAttributeText( "weak" ), def._listWeakness );
        JrpgCatalogInternal::parseNameList( node.getAttributeText( "locks" ), def._listLock );
        def._exp       = MathUtil::max( 0, node.getAttributeInt( "exp", def._exp ) );
        def._gold      = MathUtil::max( 0, node.getAttributeInt( "gold", def._gold ) );
        def._castTurns = MathUtil::max( 1, node.getAttributeInt( "castTurns", def._castTurns ) );
        def._castEvery = MathUtil::max( 1, node.getAttributeInt( "castEvery", def._castEvery ) );
        def._bBoss     = node.getAttributeBool( "boss", false );
        (void)_enemyCatalog.add( def );
    }
} // namespace sw
