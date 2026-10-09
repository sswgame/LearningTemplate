#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ActionPlatformerCatalog" );

    namespace
    {
        struct ActionPlatformerCatalogInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pName )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr ? hashed_string( pValue ) : hashed_string{};
            }

            static void readNameList( const XmlNode& node, const utf8* pName, vector<hashed_string>& outListName )
            {
                outListName.clear();
                GameDataXml::forEachToken( node.getAttributeText( pName ), ",; \t", [&]( string_view token )
                { outListName.push_back( hashed_string( token ) ); } );
            }

            static ActionGradeDef makeGrade( const utf8* pGrade, float32 minScore )
            {
                ActionGradeDef grade;
                grade._grade    = hashed_string( pGrade );
                grade._minScore = minScore;
                return grade;
            }

            static bool isGradeHigher( const ActionGradeDef& lhs, const ActionGradeDef& rhs ) { return lhs._minScore > rhs._minScore; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 ActionPatternDef::findStateIndex( const hashed_string& stateId ) const
    {
        for ( size_t index = 0; index < _listState.size(); ++index )
        {
            if ( _listState[index]._id == stateId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    ActionPlatformerCatalog::ActionPlatformerCatalog()
        : _stageCatalog{}
        , _comboCatalog{}
        , _patternCatalog{}
        , _grading{}
        , _bodySettings{}
        , _parryRules{}
    {
    }

    uint32 ActionPlatformerCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const XmlNode grading = root.findChild( "Grading" );
        _grading._listGrade.clear();
        if ( grading )
        {
            _grading._timeWeight    = MathUtil::max( 0.0f, grading.getAttributeFloat( "time", _grading._timeWeight ) );
            _grading._hitWeight     = MathUtil::max( 0.0f, grading.getAttributeFloat( "hits", _grading._hitWeight ) );
            _grading._collectWeight = MathUtil::max( 0.0f, grading.getAttributeFloat( "collect", _grading._collectWeight ) );
            for ( XmlNode node = grading.findChild( "Grade" ); node; node = node.findNextSibling( "Grade" ) )
            {
                const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
                if ( pId != nullptr )
                    _grading._listGrade.push_back( ActionPlatformerCatalogInternal::makeGrade( pId, node.getAttributeFloat( "min", 0.0f ) ) );
            }
        }
        if ( _grading._listGrade.empty() )
        {
            _grading._listGrade.push_back( ActionPlatformerCatalogInternal::makeGrade( "S", 90.0f ) );
            _grading._listGrade.push_back( ActionPlatformerCatalogInternal::makeGrade( "A", 75.0f ) );
            _grading._listGrade.push_back( ActionPlatformerCatalogInternal::makeGrade( "B", 55.0f ) );
            _grading._listGrade.push_back( ActionPlatformerCatalogInternal::makeGrade( "C", 0.0f ) );
        }
        std::stable_sort( _grading._listGrade.begin(), _grading._listGrade.end(), ActionPlatformerCatalogInternal::isGradeHigher );
        if ( _grading._timeWeight + _grading._hitWeight + _grading._collectWeight <= 0.0f )
        {
            SW_LOG_WARNING( "%#: <Grading> weights are all zero - using equal weights", sourceName );
            _grading._timeWeight    = 1.0f;
            _grading._hitWeight     = 1.0f;
            _grading._collectWeight = 1.0f;
        }

        const XmlNode body = root.findChild( "Body" );
        if ( body )
        {
            ActionBodySettings& settings       = _bodySettings;
            settings._glideFallSpeed           = MathUtil::max( 0.1f, body.getAttributeFloat( "glideFallSpeed", settings._glideFallSpeed ) );
            settings._glideGravityScale        = MathUtil::clamp( body.getAttributeFloat( "glideGravityScale", settings._glideGravityScale ), 0.0f, 1.0f );
            settings._grappleRange             = MathUtil::max( 0.0f, body.getAttributeFloat( "grappleRange", settings._grappleRange ) );
            settings._grappleMinLength         = MathUtil::max( 0.1f, body.getAttributeFloat( "grappleMinLength", settings._grappleMinLength ) );
            settings._grappleSwingAcceleration = MathUtil::max( 0.0f, body.getAttributeFloat( "grappleSwingAcceleration", settings._grappleSwingAcceleration ) );
            settings._grappleMaxSpeed          = MathUtil::max( 1.0f, body.getAttributeFloat( "grappleMaxSpeed", settings._grappleMaxSpeed ) );
            settings._drillSpeed               = MathUtil::max( 0.1f, body.getAttributeFloat( "drillSpeed", settings._drillSpeed ) );
            settings._drillExitSpeed           = MathUtil::max( 0.0f, body.getAttributeFloat( "drillExitSpeed", settings._drillExitSpeed ) );
            settings._drillJumpSpeed           = MathUtil::max( 0.0f, body.getAttributeFloat( "drillJumpSpeed", settings._drillJumpSpeed ) );
            settings._drillEntryTime           = MathUtil::max( 0.0f, body.getAttributeFloat( "drillEntryTime", settings._drillEntryTime ) );
        }

        const XmlNode parry = root.findChild( "Parry" );
        if ( parry )
        {
            _parryRules._windowFrames       = MathUtil::max( 0, parry.getAttributeInt( "window", _parryRules._windowFrames ) );
            _parryRules._radius             = MathUtil::max( 0.0f, parry.getAttributeFloat( "radius", _parryRules._radius ) );
            _parryRules._reflectSpeedScale  = MathUtil::max( 0.0f, parry.getAttributeFloat( "reflectSpeed", _parryRules._reflectSpeedScale ) );
            _parryRules._reflectDamageScale = MathUtil::max( 0.0f, parry.getAttributeFloat( "reflectDamage", _parryRules._reflectDamageScale ) );
            _parryRules._hitstopFrames      = MathUtil::max( 0, parry.getAttributeInt( "hitstop", _parryRules._hitstopFrames ) );
            _parryRules._attackBufferFrames = MathUtil::max( 0, parry.getAttributeInt( "buffer", _parryRules._attackBufferFrames ) );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Stage" ); node; node = node.findNextSibling( "Stage" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ActionStageDef stage;
            stage._id         = hashed_string( pId );
            const utf8* pName = node.findAttribute( "name" );
            stage._name       = pName != nullptr ? pName : pId;
            ActionPlatformerCatalogInternal::readNameList( node, "checkpoints", stage._listCheckpoint );
            ActionPlatformerCatalogInternal::readNameList( node, "secrets", stage._listSecret );
            stage._parTime      = MathUtil::max( 0.1f, node.getAttributeFloat( "parTime", stage._parTime ) );
            stage._lives        = MathUtil::max( 1, node.getAttributeInt( "lives", stage._lives ) );
            stage._hitTolerance = MathUtil::max( 1, node.getAttributeInt( "hitTolerance", stage._hitTolerance ) );
            (void)_stageCatalog.add( stage );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Combo" ); node; node = node.findNextSibling( "Combo" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ActionComboDef combo;
            combo._id = hashed_string( pId );
            ActionPlatformerCatalogInternal::readNameList( node, "moves", combo._listMove );
            if ( combo._listMove.empty() )
            {
                SW_LOG_WARNING( "%#: combo '%#' has no moves - skipped", sourceName, pId );
                continue;
            }
            (void)_comboCatalog.add( combo );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Pattern" ); node; node = node.findNextSibling( "Pattern" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            loadPattern( node, pId, sourceName );
            ++loadedCount;
        }
        return loadedCount;
    }

    void ActionPlatformerCatalog::loadPattern( const XmlNode& node, const utf8* pId, string_view sourceName )
    {
        ActionPatternDef pattern;
        pattern._id = hashed_string( pId );
        for ( XmlNode child = node.findChild( "State" ); child; child = child.findNextSibling( "State" ) )
        {
            const utf8* pStateId = GameDataXml::findRequiredId( child, sourceName );
            if ( pStateId == nullptr )
                continue;
            ActionPatternStateDef state;
            state._id        = hashed_string( pStateId );
            state._next      = ActionPlatformerCatalogInternal::readName( child, "next" );
            state._onNear    = ActionPlatformerCatalogInternal::readName( child, "onNear" );
            state._onHit     = ActionPlatformerCatalogInternal::readName( child, "onHit" );
            state._nearRange = MathUtil::max( 0.0f, child.getAttributeFloat( "near", state._nearRange ) );
            state._moveX     = MathUtil::clamp( child.getAttributeFloat( "moveX", state._moveX ), -1.0f, 1.0f );
            state._fireSpeed = MathUtil::max( 0.0f, child.getAttributeFloat( "fireSpeed", state._fireSpeed ) );
            state._frames    = MathUtil::max( 1, child.getAttributeInt( "frames", state._frames ) );
            state._bFire     = child.getAttributeBool( "fire", false ) ? SW_TRUE : SW_FALSE;
            state._bAttack   = child.getAttributeBool( "attack", false ) ? SW_TRUE : SW_FALSE;
            pattern._listState.push_back( state );
        }
        if ( pattern._listState.empty() )
        {
            SW_LOG_WARNING( "%#: pattern '%#' has no <State> - skipped", sourceName, pId );
            return;
        }
        pattern._start = ActionPlatformerCatalogInternal::readName( node, "start" );
        if ( pattern._start.empty() || pattern.findStateIndex( pattern._start ) < 0 )
            pattern._start = pattern._listState.front()._id;
        // 이어지는 상태 이름이 틀리면 그 자리에서 알린다(시험 · 에디터가 바로 본다).
        for ( const ActionPatternStateDef& state : pattern._listState )
        {
            const hashed_string arrTarget[] = { state._next, state._onNear, state._onHit };
            for ( const hashed_string& target : arrTarget )
            {
                if ( target.empty() == false && pattern.findStateIndex( target ) < 0 )
                    SW_LOG_WARNING( "%#: pattern '%#' state '%#' goes to an unknown state '%#'", sourceName, pId, state._id.c_str(), target.c_str() );
            }
        }
        (void)_patternCatalog.add( pattern );
    }
} // namespace sw
