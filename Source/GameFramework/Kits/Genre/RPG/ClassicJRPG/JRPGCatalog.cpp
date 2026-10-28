#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

#include <optional>

namespace sw
{
    namespace
    {
        struct JRPGCatalogInternal
        {
            static constexpr const utf8* kArrStatName[kJRPGStatCount]   = { "hp", "mp", "str", "agi", "vit", "intellect", "luck" };
            static constexpr const utf8* kArrGrowthName[kJRPGStatCount] = { "growHP", "growMP", "growStr", "growAgi", "growVit", "growIntellect", "growLuck" };

            /** @brief 능력치 일곱 칸을 이름(@p arrName)으로 읽습니다. 적지 않은 칸은 Def 의 초기값이고, 깨졌거나 0 보다 작으면 오류입니다. */
            [[nodiscard]] static bool readStats( const XMLNode& node, const utf8* const ( &arrName )[kJRPGStatCount], string_view sourceName,
                                                 int32 ( &inoutArrValue )[kJRPGStatCount] )
            {
                bool bValid = true;
                for ( int32 index = 0; index < kJRPGStatCount; ++index )
                {
                    std::optional<int32> value;
                    const bool           bRead     = node.readOptionalAttribute( arrName[index], value );
                    const bool           bNegative = value.has_value() && *value < 0;
                    if ( bRead == false || bNegative )
                    {
                        SW_LOG_ERROR( "%#: <%# %#=\"%#\"> needs an integer of 0 or more", sourceName, node.getName(), arrName[index], node.getAttributeText( arrName[index] ) );
                        bValid = false;
                    }
                    else if ( value.has_value() )
                        inoutArrValue[index] = *value;
                }
                return bValid;
            }

            /** @brief 최대 HP 는 1 이상이어야 합니다(0 이면 처음부터 쓰러진 상태다). */
            [[nodiscard]] static bool checkMaxHP( const int32 ( &arrStat )[kJRPGStatCount], const XMLNode& node, string_view sourceName )
            {
                if ( arrStat[static_cast<size_t>( JRPGStat::MaxHP )] >= 1 )
                    return true;
                SW_LOG_ERROR( "%#: <%# id=\"%#\"> needs hp of 1 or more", sourceName, node.getName(), node.getAttributeText( "id" ) );
                return false;
            }
        };
    } // namespace

    SW_LOG_CALLER( "JRPGCatalog" );

    JRPGCatalog::JRPGCatalog()
        : _classCatalog{}
        , _spellCatalog{}
        , _comboCatalog{}
        , _manualCatalog{}
        , _enemyCatalog{}
        , _areaCatalog{}
        , _curve{}
    {
    }

    void JRPGCatalog::clear()
    {
        _classCatalog.clear();
        _spellCatalog.clear();
        _comboCatalog.clear();
        _manualCatalog.clear();
        _enemyCatalog.clear();
        _areaCatalog.clear();
        _curve = ExperienceCurve{};
    }

    uint32 JRPGCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }

        // 서로 가리키는 id 는 모두 읽은 뒤에 확인한다.
        for ( const JRPGClassDef& def : _classCatalog.getAll() )
        {
            for ( const JRPGLearnEntry& learn : def._listLearn )
            {
                if ( _spellCatalog.find( learn._spellID ) == nullptr )
                    SW_LOG_ERROR( "%#: class '%#' learns unknown spell '%#'", sourceName, def._id.c_str(), learn._spellID.c_str() );
            }
        }
        for ( const JRPGManualDef& def : _manualCatalog.getAll() )
        {
            for ( const JRPGManualStage& stage : def._listStage )
            {
                if ( _spellCatalog.find( stage._techniqueID ) == nullptr )
                    SW_LOG_ERROR( "%#: manual '%#' unlocks unknown technique '%#'", sourceName, def._id.c_str(), stage._techniqueID.c_str() );
            }
        }
        for ( const JRPGAreaDef& def : _areaCatalog.getAll() )
        {
            for ( const JRPGEncounterGroup& group : def._listGroup )
            {
                for ( const hashed_string& enemyID : group._listEnemyID )
                {
                    if ( _enemyCatalog.find( enemyID ) == nullptr )
                        SW_LOG_ERROR( "%#: area '%#' names unknown enemy '%#'", sourceName, def._id.c_str(), enemyID.c_str() );
                }
            }
        }
        return loadedCount;
    }

    bool JRPGCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        if ( XMLReadUtil::isElement( name, "ExperienceCurve" ) )
            return _curve.loadFromNode( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Class" ) )
            return loadClass( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Spell" ) )
            return GameDataXML::readInto( node, sourceName, _spellCatalog );
        if ( XMLReadUtil::isElement( name, "Combo" ) )
            return loadCombo( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Manual" ) )
            return loadManual( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Enemy" ) )
            return loadEnemy( node, sourceName );
        if ( XMLReadUtil::isElement( name, "Area" ) )
            return loadArea( node, sourceName );
        SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return false;
    }

    bool JRPGCatalog::loadClass( const XMLNode& node, string_view sourceName )
    {
        using Internal = JRPGCatalogInternal;
        JRPGClassDef def;
        bool         bValid = GameDataXML::readFields( node, def, sourceName,
                                                       { "hp", "mp", "str", "agi", "vit", "intellect", "luck", "growHP", "growMP", "growStr", "growAgi", "growVit",
                                                         "growIntellect", "growLuck" } );
        bValid              = Internal::readStats( node, Internal::kArrStatName, sourceName, def._arrBase ) && bValid;
        bValid              = Internal::readStats( node, Internal::kArrGrowthName, sourceName, def._arrGrowth ) && bValid;
        bValid              = Internal::checkMaxHP( def._arrBase, node, sourceName ) && bValid;
        for ( XMLNode learn = node.findChild( "Learn" ); learn; learn = learn.findNextSibling( "Learn" ) )
        {
            bValid = GameDataXML::readInto( learn, sourceName, def._listLearn ) && bValid;
        }
        if ( bValid )
            (void)_classCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool JRPGCatalog::loadCombo( const XMLNode& node, string_view sourceName )
    {
        JRPGComboDef def;
        if ( GameDataXML::readFields( node, def, sourceName, { "members", "types" } ) == false )
            return false;
        XMLReadUtil::appendNameList( node.getAttributeText( "members" ), ",; \t", def._listMemberID );
        XMLReadUtil::appendNameList( node.getAttributeText( "types" ), ",; \t", def._listDamageType );
        if ( def._listMemberID.size() < 2 )
        {
            SW_LOG_ERROR( "%#: combo '%#' needs two or more members", sourceName, def._id.c_str() );
            return false;
        }
        (void)_comboCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool JRPGCatalog::loadManual( const XMLNode& node, string_view sourceName )
    {
        JRPGManualDef def;
        bool          bValid = GameDataXML::readFields( node, def, sourceName );
        for ( XMLNode stage = node.findChild( "Stage" ); stage; stage = stage.findNextSibling( "Stage" ) )
        {
            bValid = GameDataXML::readInto( stage, sourceName, def._listStage ) && bValid;
        }
        if ( bValid )
            (void)_manualCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool JRPGCatalog::loadEnemy( const XMLNode& node, string_view sourceName )
    {
        using Internal = JRPGCatalogInternal;
        JRPGEnemyDef def;
        bool         bValid = GameDataXML::readFields( node, def, sourceName, { "hp", "mp", "str", "agi", "vit", "intellect", "luck", "weak", "locks" } );
        bValid              = Internal::readStats( node, Internal::kArrStatName, sourceName, def._arrStat ) && bValid;
        bValid              = Internal::checkMaxHP( def._arrStat, node, sourceName ) && bValid;
        XMLReadUtil::appendNameList( node.getAttributeText( "weak" ), ",; \t", def._listWeakness );
        XMLReadUtil::appendNameList( node.getAttributeText( "locks" ), ",; \t", def._listLock );
        if ( bValid )
            (void)_enemyCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }

    bool JRPGCatalog::loadArea( const XMLNode& node, string_view sourceName )
    {
        JRPGAreaDef def;
        bool        bValid = GameDataXML::readFields( node, def, sourceName );
        for ( XMLNode groupNode = node.findChild( "Group" ); groupNode; groupNode = groupNode.findNextSibling( "Group" ) )
        {
            JRPGEncounterGroup group;
            bValid = GameDataXML::readFields( groupNode, group, sourceName, { "enemies" } ) && bValid;
            XMLReadUtil::appendNameList( groupNode.getAttributeText( "enemies" ), ",; \t", group._listEnemyID );
            if ( group._listEnemyID.empty() )
            {
                SW_LOG_ERROR( "%#: area '%#' has a <Group> without enemies", sourceName, def._id.c_str() );
                bValid = false;
                continue;
            }
            def._listGroup.push_back( group );
        }
        if ( bValid )
            (void)_areaCatalog.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return bValid;
    }
} // namespace sw
