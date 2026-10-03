#include "pch.h"

#include "GameFramework/Kits/AsymmetricHorror/HorrorRules.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    namespace
    {
        struct HorrorRulesInternal
        {
            static constexpr uint32 kMaxScaleCount = 8;

            /** @brief "1,1.7,2.1" 을 읽습니다. 비었으면 @p outListScale 을 그대로 둡니다. */
            static void readScales( const XmlNode& node, vector<float32>& outListScale )
            {
                const string_view text = node.getAttributeText( "scales" );
                if ( text.empty() )
                    return;
                float32      arrScale[kMaxScaleCount] = {};
                const uint32 count                    = GameDataXml::parseFloats( text, arrScale, kMaxScaleCount );
                outListScale.clear();
                for ( uint32 index = 0; index < count; ++index )
                    outListScale.push_back( MathUtil::max( 0.0f, arrScale[index] ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "HorrorRulesCatalog" );

    HorrorRulesCatalog::HorrorRulesCatalog()
        : _rules{}
        , _judge{}
        , _catalogKiller{}
        , _listScoreRule{}
        , _categoryCap{}
    {
    }

    bool HorrorRulesCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "HorrorRules", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool HorrorRulesCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "HorrorRules", root ) && loadRoot( root, sourceName ) > 0;
    }

    const HorrorScoreRule* HorrorRulesCatalog::findScoreRule( const hashed_string& action ) const
    {
        for ( const HorrorScoreRule& rule : _listScoreRule )
        {
            if ( rule._action == action )
                return &rule;
        }
        return nullptr;
    }

    uint32 HorrorRulesCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        HorrorRules& rules        = _rules;
        rules._survivorSpeed      = MathUtil::max( 0.1f, root.getAttributeFloat( "survivorSpeed", rules._survivorSpeed ) );
        rules._crawlSpeed         = MathUtil::max( 0.0f, root.getAttributeFloat( "crawlSpeed", rules._crawlSpeed ) );
        rules._hitHasteScale      = MathUtil::max( 1.0f, root.getAttributeFloat( "hitHasteScale", rules._hitHasteScale ) );
        rules._hitHasteTime       = MathUtil::max( 0.0f, root.getAttributeFloat( "hitHasteTime", rules._hitHasteTime ) );
        rules._bleedoutTime       = MathUtil::max( 0.1f, root.getAttributeFloat( "bleedoutTime", rules._bleedoutTime ) );
        rules._interactRange      = MathUtil::max( 0.1f, root.getAttributeFloat( "interactRange", rules._interactRange ) );
        rules._generatorsRequired = MathUtil::max( 1, root.getAttributeInt( "generatorsRequired", rules._generatorsRequired ) );

        if ( const XmlNode node = root.findChild( "Generator" ) )
        {
            rules._repairTime               = MathUtil::max( 0.1f, node.getAttributeFloat( "time", rules._repairTime ) );
            rules._repairMaxParticipants    = MathUtil::max( 1, node.getAttributeInt( "maxRepairers", rules._repairMaxParticipants ) );
            rules._repairSkillCheckInterval = MathUtil::max( 0.0f, node.getAttributeFloat( "skillCheckInterval", rules._repairSkillCheckInterval ) );
            rules._kickPenalty              = MathUtil::saturate( node.getAttributeFloat( "kickPenalty", rules._kickPenalty ) );
            rules._kickRegression           = MathUtil::max( 0.0f, node.getAttributeFloat( "kickRegression", rules._kickRegression ) );
            rules._skillCheckNoiseRadius    = MathUtil::max( 0.0f, node.getAttributeFloat( "noiseRadius", rules._skillCheckNoiseRadius ) );
            HorrorRulesInternal::readScales( node, rules._listRepairScale );
        }
        if ( const XmlNode node = root.findChild( "Heal" ) )
        {
            rules._healTime               = MathUtil::max( 0.1f, node.getAttributeFloat( "time", rules._healTime ) );
            rules._healMaxParticipants    = MathUtil::max( 1, node.getAttributeInt( "maxHealers", rules._healMaxParticipants ) );
            rules._healSkillCheckInterval = MathUtil::max( 0.0f, node.getAttributeFloat( "skillCheckInterval", rules._healSkillCheckInterval ) );
            HorrorRulesInternal::readScales( node, rules._listHealScale );
        }
        if ( const XmlNode node = root.findChild( "SkillCheck" ) )
        {
            rules._skillCheckLeadTime    = MathUtil::max( 0.0f, node.getAttributeFloat( "leadTime", rules._skillCheckLeadTime ) );
            rules._skillCheckFailPenalty = MathUtil::saturate( node.getAttributeFloat( "failPenalty", rules._skillCheckFailPenalty ) );
            _judge.loadFromNode( node );
            rules._skillCheckBonus.clear();
            for ( XmlNode window = node.findChild( "Window" ); window; window = window.findNextSibling( "Window" ) )
            {
                const utf8* pGrade = window.findAttribute( "grade" );
                if ( pGrade != nullptr )
                    rules._skillCheckBonus.setValue( hashed_string( pGrade ), window.getAttributeFloat( "bonus", 0.0f ) );
            }
        }
        if ( const XmlNode node = root.findChild( "Hook" ) )
        {
            rules._hookStageTime  = MathUtil::max( 0.1f, node.getAttributeFloat( "stageTime", rules._hookStageTime ) );
            rules._maxHookStage   = MathUtil::max( 1, node.getAttributeInt( "stages", rules._maxHookStage ) );
            rules._struggleGrace  = MathUtil::max( 0.0f, node.getAttributeFloat( "struggleGrace", rules._struggleGrace ) );
            rules._wiggleTime     = MathUtil::max( 0.1f, node.getAttributeFloat( "wiggleTime", rules._wiggleTime ) );
            rules._wiggleStunTime = MathUtil::max( 0.0f, node.getAttributeFloat( "wiggleStunTime", rules._wiggleStunTime ) );
        }
        if ( const XmlNode node = root.findChild( "Pallet" ) )
        {
            rules._palletStunTime  = MathUtil::max( 0.0f, node.getAttributeFloat( "stunTime", rules._palletStunTime ) );
            rules._palletStunRange = MathUtil::max( 0.0f, node.getAttributeFloat( "stunRange", rules._palletStunRange ) );
            rules._palletBreakTime = MathUtil::max( 0.0f, node.getAttributeFloat( "breakTime", rules._palletBreakTime ) );
        }
        if ( const XmlNode node = root.findChild( "Window" ) )
        {
            rules._fastVaultTime       = MathUtil::max( 0.0f, node.getAttributeFloat( "fastTime", rules._fastVaultTime ) );
            rules._mediumVaultTime     = MathUtil::max( rules._fastVaultTime, node.getAttributeFloat( "mediumTime", rules._mediumVaultTime ) );
            rules._killerVaultTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "killerTime", rules._killerVaultTime ) );
            rules._fastVaultSpeedRatio = MathUtil::max( 0.0f, node.getAttributeFloat( "fastSpeedRatio", rules._fastVaultSpeedRatio ) );
            rules._vaultNoiseRadius    = MathUtil::max( 0.0f, node.getAttributeFloat( "noiseRadius", rules._vaultNoiseRadius ) );
            rules._windowBlockCount    = MathUtil::max( 1, node.getAttributeInt( "blockCount", rules._windowBlockCount ) );
            rules._windowBlockTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "blockTime", rules._windowBlockTime ) );
        }
        if ( const XmlNode node = root.findChild( "Locker" ) )
            rules._lockerSearchTime = MathUtil::max( 0.0f, node.getAttributeFloat( "searchTime", rules._lockerSearchTime ) );
        if ( const XmlNode node = root.findChild( "Endgame" ) )
        {
            rules._gateOpenTime       = MathUtil::max( 0.1f, node.getAttributeFloat( "gateTime", rules._gateOpenTime ) );
            rules._collapseTime       = MathUtil::max( 0.1f, node.getAttributeFloat( "collapseTime", rules._collapseTime ) );
            rules._collapseSlowScale  = MathUtil::max( 0.0f, node.getAttributeFloat( "slowScale", rules._collapseSlowScale ) );
            rules._hatchSurvivorCount = MathUtil::max( 0, node.getAttributeInt( "hatchRemaining", rules._hatchSurvivorCount ) );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Killer" ); node; node = node.findNextSibling( "Killer" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            HorrorKillerDef killer;
            killer._id                 = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            killer._name               = pName != nullptr ? pName : pId;
            killer._speedRatio         = MathUtil::max( 0.1f, node.getAttributeFloat( "speedRatio", killer._speedRatio ) );
            killer._lungeRange         = MathUtil::max( 0.1f, node.getAttributeFloat( "lungeRange", killer._lungeRange ) );
            killer._lungeAngle         = MathUtil::clamp( node.getAttributeFloat( "lungeAngle", killer._lungeAngle ), 1.0f, 180.0f );
            killer._hitCooldown        = MathUtil::max( 0.0f, node.getAttributeFloat( "hitCooldown", killer._hitCooldown ) );
            killer._missCooldown       = MathUtil::max( 0.0f, node.getAttributeFloat( "missCooldown", killer._missCooldown ) );
            killer._cooldownSpeedScale = MathUtil::max( 0.0f, node.getAttributeFloat( "cooldownSpeedScale", killer._cooldownSpeedScale ) );
            killer._terrorRadius       = MathUtil::max( 0.0f, node.getAttributeFloat( "terrorRadius", killer._terrorRadius ) );
            killer._abilityCooldown    = MathUtil::max( 0.0f, node.getAttributeFloat( "abilityCooldown", killer._abilityCooldown ) );
            killer._carrySpeedScale    = MathUtil::max( 0.0f, node.getAttributeFloat( "carrySpeedScale", killer._carrySpeedScale ) );
            (void)_catalogKiller.add( killer );
            ++loadedCount;
        }
        for ( XmlNode node = root.findChild( "Category" ); node; node = node.findNextSibling( "Category" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId != nullptr )
                _categoryCap.setValue( hashed_string( pId ), MathUtil::max( 0.0f, node.getAttributeFloat( "cap", 0.0f ) ) );
        }
        for ( XmlNode node = root.findChild( "Score" ); node; node = node.findNextSibling( "Score" ) )
        {
            const utf8* pAction   = node.findAttribute( "action" );
            const utf8* pCategory = node.findAttribute( "category" );
            if ( pAction == nullptr || pCategory == nullptr )
            {
                SW_LOG_WARNING( "%#: <Score> needs action and category - skipped", sourceName );
                continue;
            }
            HorrorScoreRule rule;
            rule._action   = hashed_string( pAction );
            rule._category = hashed_string( pCategory );
            rule._points   = node.getAttributeFloat( "points", 1.0f );
            _listScoreRule.push_back( rule );
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Killer> - the rules need at least one", sourceName );
        return loadedCount;
    }
} // namespace sw
