#include "pch.h"

#include "GameFramework/Quest/QuestCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "QuestCatalog" );

    namespace
    {
        struct QuestCatalogInternal
        {
            static hashed_string readId( const XmlNode& node, const utf8* pName )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr ? hashed_string( pValue ) : hashed_string{};
            }

            static void readReward( const XmlNode& node, QuestReward& outReward )
            {
                const XmlNode rewardNode = node.findChild( "Reward" );
                if ( rewardNode.isValid() == false )
                    return;
                (void)outReward._values.loadFromAttributes( rewardNode );
                for ( XmlNode item = rewardNode.findChild( "Item" ); item; item = item.findNextSibling( "Item" ) )
                {
                    const utf8* pItem = item.findAttribute( "item" );
                    if ( pItem != nullptr )
                        outReward._items.addItem( hashed_string( pItem ), MathUtil::max( 1, item.getAttributeInt( "count", 1 ) ) );
                }
            }
        };
    } // namespace

    const QuestStage* QuestDef::findStage( const hashed_string& stageId ) const
    {
        for ( const QuestStage& stage : _listStage )
        {
            if ( stage._id == stageId )
                return &stage;
        }
        return nullptr;
    }

    bool QuestCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &QuestCatalog::loadRoot, path, "QuestCatalog" );
    }

    bool QuestCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &QuestCatalog::loadRoot, xmlText, sourceName, "QuestCatalog" );
    }

    uint32 QuestCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode questNode = root.findChild( "Quest" ); questNode; questNode = questNode.findNextSibling( "Quest" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( questNode, sourceName );
            if ( pId == nullptr )
                continue;
            QuestDef quest;
            quest._id            = hashed_string( pId );
            const utf8* pName    = questNode.findAttribute( "name" );
            quest._name          = pName != nullptr ? pName : pId;
            quest._requiredLevel = questNode.getAttributeInt( "level", 0 );
            quest._bRepeatable   = questNode.getAttributeBool( "repeatable", false ) ? SW_TRUE : SW_FALSE;
            GameDataXml::forEachToken( questNode.getAttributeText( "requires" ), ", ", [&]( string_view token )
            { quest._listRequiredQuest.push_back( hashed_string( token ) ); } );
            for ( XmlNode stageNode = questNode.findChild( "Stage" ); stageNode; stageNode = stageNode.findNextSibling( "Stage" ) )
            {
                QuestStage stage;
                stage._id         = QuestCatalogInternal::readId( stageNode, "id" );
                stage._nextStage  = QuestCatalogInternal::readId( stageNode, "next" );
                const utf8* pText = stageNode.findAttribute( "text" );
                stage._text       = pText != nullptr ? pText : "";
                stage._timeLimit  = MathUtil::max( 0.0f, stageNode.getAttributeFloat( "time", 0.0f ) );
                stage._bComplete  = stageNode.getAttributeBool( "complete", false ) ? SW_TRUE : SW_FALSE;
                stage._bFail      = stageNode.getAttributeBool( "fail", false ) ? SW_TRUE : SW_FALSE;
                for ( XmlNode node = stageNode.findChild( "Objective" ); node; node = node.findNextSibling( "Objective" ) )
                {
                    QuestObjective objective;
                    objective._kind      = QuestCatalogInternal::readId( node, "kind" );
                    objective._target    = QuestCatalogInternal::readId( node, "target" );
                    const utf8* pObjText = node.findAttribute( "text" );
                    objective._text      = pObjText != nullptr ? pObjText : "";
                    objective._count     = MathUtil::max( 1, node.getAttributeInt( "count", 1 ) );
                    objective._bOptional = node.getAttributeBool( "optional", false ) ? SW_TRUE : SW_FALSE;
                    stage._listObjective.push_back( objective );
                }
                for ( XmlNode node = stageNode.findChild( "Branch" ); node; node = node.findNextSibling( "Branch" ) )
                    stage._listBranch.push_back( QuestBranch{ QuestCatalogInternal::readId( node, "choice" ), QuestCatalogInternal::readId( node, "next" ) } );
                QuestCatalogInternal::readReward( stageNode, stage._reward );
                if ( stage._id.empty() )
                    SW_LOG_WARNING( "%#: quest '%#' has a stage without id", sourceName, pId );
                quest._listStage.push_back( stage );
            }
            // 다음 단계 · 분기가 가리키는 단계가 있는지.
            for ( const QuestStage& stage : quest._listStage )
            {
                if ( stage._nextStage.empty() == false && quest.findStage( stage._nextStage ) == nullptr )
                    SW_LOG_WARNING( "%#: quest '%#' stage '%#' goes to unknown '%#'", sourceName, pId, stage._id.c_str(), stage._nextStage.c_str() );
                for ( const QuestBranch& branch : stage._listBranch )
                {
                    if ( quest.findStage( branch._nextStage ) == nullptr )
                        SW_LOG_WARNING( "%#: quest '%#' branch '%#' goes to unknown '%#'", sourceName, pId, branch._choice.c_str(), branch._nextStage.c_str() );
                }
            }
            if ( quest._listStage.empty() )
            {
                SW_LOG_WARNING( "%#: quest '%#' has no stages", sourceName, pId );
                continue;
            }
            (void)_catalog.add( quest );
            ++loadedCount;
        }
        return loadedCount;
    }
} // namespace sw
