#include "pch.h"

#include "GameFramework/Base/Gameplay/Quest/QuestCatalog.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "QuestCatalog" );

    namespace
    {
        /**
         * @brief 단계 원소 @p node 의 `<Reward>` 를 @p outReward 에 읽습니다. 보상이 없으면 아무것도 하지 않습니다.
         * @return 보상 `<Item>` 에 스키마(`QuestRewardItemRowDef`) 오류가 있으면 false 입니다(오류는 readFields 가 남긴다).
         */
        [[nodiscard]] bool readQuestReward( const XMLNode& node, string_view sourceName, QuestReward& outReward )
        {
            const XMLNode rewardNode = node.findChild( "Reward" );
            if ( rewardNode.isValid() == false )
                return true;
            outReward._values.addAttributes( rewardNode ); // 읽은 속성 수만 반환한다. 속성이 없으면 값 보상이 없다
            bool bValid = true;
            for ( XMLNode item = rewardNode.findChild( "Item" ); item; item = item.findNextSibling( "Item" ) )
            {
                QuestRewardItemRowDef row;
                if ( GameDataXML::readFields( item, row, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                outReward._items.addItem( hashed_string( row._item ), row._count );
            }
            return bValid;
        }
    } // namespace

    const QuestStage* QuestDef::findStage( const hashed_string& stageID ) const
    {
        return VectorUtil::findBy<&QuestStage::_id>( _listStage, stageID );
    }

    uint32 QuestCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode questNode = root.findChild( "Quest" ); questNode; questNode = questNode.findNextSibling( "Quest" ) )
        {
            const utf8* pID = XMLReadUtil::findRequiredID( questNode, sourceName );
            if ( pID == nullptr )
                continue;
            // 기본값 · 범위는 스키마(Quest*RowDef)가 정한다. 칸 하나라도 오류면 그 퀘스트를 넣지 않는다(오류는 readFields 가 남긴다).
            QuestRowDef questRow;
            bool        bValid = GameDataXML::readFields( questNode, questRow, sourceName, { "id", "name", "requires" } );
            QuestDef    quest;
            quest._id            = hashed_string( pID );
            const utf8* pName    = questNode.findAttribute( "name" );
            quest._name          = pName != nullptr ? pName : pID;
            quest._requiredLevel = questRow._level;
            quest._bRepeatable   = questRow._bRepeatable ? SW_TRUE : SW_FALSE;
            StringUtil::forEachToken( questNode.getAttributeText( "requires" ), ", ", [&]( string_view token )
            { quest._listRequiredQuest.push_back( hashed_string( token ) ); } );
            for ( XMLNode stageNode = questNode.findChild( "Stage" ); stageNode; stageNode = stageNode.findNextSibling( "Stage" ) )
            {
                QuestStageRowDef stageRow;
                bValid = GameDataXML::readFields( stageNode, stageRow, sourceName, { "id", "next" } ) && bValid;
                QuestStage stage;
                bValid           = XMLReadUtil::readRequiredName( stageNode, "id", sourceName, stage._id ) && bValid;
                stage._nextStage = XMLReadUtil::readOptionalName( stageNode, "next" ); // 마지막 단계는 다음이 없다
                stage._text      = stageRow._text;
                stage._timeLimit = stageRow._time;
                stage._bComplete = stageRow._bComplete ? SW_TRUE : SW_FALSE;
                stage._bFail     = stageRow._bFail ? SW_TRUE : SW_FALSE;
                for ( XMLNode node = stageNode.findChild( "Objective" ); node; node = node.findNextSibling( "Objective" ) )
                {
                    QuestObjectiveRowDef objectiveRow;
                    bValid = GameDataXML::readFields( node, objectiveRow, sourceName, { "kind", "target" } ) && bValid;
                    QuestObjective objective;
                    bValid               = XMLReadUtil::readRequiredName( node, "kind", sourceName, objective._kind ) && bValid;
                    bValid               = XMLReadUtil::readRequiredName( node, "target", sourceName, objective._target ) && bValid;
                    objective._text      = objectiveRow._text;
                    objective._count     = objectiveRow._count;
                    objective._bOptional = objectiveRow._bOptional ? SW_TRUE : SW_FALSE;
                    stage._listObjective.push_back( objective );
                }
                for ( XMLNode node = stageNode.findChild( "Branch" ); node; node = node.findNextSibling( "Branch" ) )
                {
                    QuestBranch branch;
                    bValid = XMLReadUtil::readRequiredName( node, "choice", sourceName, branch._choice ) && bValid;
                    bValid = XMLReadUtil::readRequiredName( node, "next", sourceName, branch._nextStage ) && bValid;
                    stage._listBranch.push_back( branch );
                }
                bValid = readQuestReward( stageNode, sourceName, stage._reward ) && bValid;
                quest._listStage.push_back( stage );
            }
            // 다음 단계와 분기가 가리키는 단계가 있는지 확인한다.
            for ( const QuestStage& stage : quest._listStage )
            {
                if ( stage._nextStage.empty() == false && quest.findStage( stage._nextStage ) == nullptr )
                    SW_LOG_WARNING( "%#: quest '%#' stage '%#' goes to unknown '%#'", sourceName, pID, stage._id.c_str(), stage._nextStage.c_str() );
                for ( const QuestBranch& branch : stage._listBranch )
                {
                    if ( quest.findStage( branch._nextStage ) == nullptr )
                        SW_LOG_WARNING( "%#: quest '%#' branch '%#' goes to unknown '%#'", sourceName, pID, branch._choice.c_str(), branch._nextStage.c_str() );
                }
            }
            if ( quest._listStage.empty() )
            {
                SW_LOG_WARNING( "%#: quest '%#' has no stages", sourceName, pID );
                continue;
            }
            if ( bValid == false )
            {
                SW_LOG_ERROR( "%#: quest '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            (void)add( quest );
            ++loadedCount;
        }
        return loadedCount;
    }
} // namespace sw

namespace sw
{
    /** @brief `QuestCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Quests, QuestCatalog::kDataKind, "game/", nullptr, "/data/quests.xml", &AssetDataKindUtil::loadCatalog<QuestCatalog> );
} // namespace sw
