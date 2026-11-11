#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 단계 원소 @p node 의 `<Reward>` 를 @p outReward 에 읽습니다. 보상이 없으면 아무것도 하지 않습니다.
         * @return 보상 `<Item>` 에 스키마(`QuestRewardItemRowDef`) 오류가 있으면 false 입니다(오류는 readProperties 가 남긴다).
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
                if ( XMLFields::readProperties( item, row, sourceName ) == false )
                {
                    bValid = false;
                    continue;
                }
                outReward._items.addItem( hashed_string( row._item ), row._count );
            }
            for ( XMLNode action = rewardNode.findChild( "Action" ); action; action = action.findNextSibling( "Action" ) )
            {
                QuestRewardActionRowDef row;
                GameActionCall          call;
                if ( XMLFields::readProperties( action, row, sourceName ) == false || GameActionCall::compileText( row._call, sourceName, call ) == false )
                {
                    bValid = false;
                    continue;
                }
                outReward._listAction.push_back( std::move( call ) );
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
        using Element                          = XMLCatalogElement<QuestCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&QuestCatalog::loadQuest>( "Quest" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool QuestCatalog::loadQuest( const XMLNode& questNode, string_view sourceName )
    {

        // 기본값 · 범위는 스키마가 정한다. 칸 하나라도 오류면 그 퀘스트를 넣지 않는다(오류는 readProperties 가 남긴다).
        QuestDef quest;
        bool     bValid = XMLFields::readProperties( questNode, quest, sourceName );
        if ( quest._id.empty() )
            return false;
        const utf8* const pID = quest._id.c_str();
        if ( quest._name.empty() )
            quest._name = pID;
        for ( XMLNode stageNode = questNode.findChild( "Stage" ); stageNode; stageNode = stageNode.findNextSibling( "Stage" ) )
        {
            QuestStage stage;
            bValid = XMLFields::readProperties( stageNode, stage, sourceName ) && bValid;
            for ( XMLNode node = stageNode.findChild( "Objective" ); node; node = node.findNextSibling( "Objective" ) )
            {
                QuestObjective objective;
                bValid = XMLFields::readProperties( node, objective, sourceName ) && bValid;
                stage._listObjective.push_back( objective );
            }
            for ( XMLNode node = stageNode.findChild( "Branch" ); node; node = node.findNextSibling( "Branch" ) )
            {
                QuestBranch branch;
                bValid = XMLFields::readProperties( node, branch, sourceName ) && bValid;
                stage._listBranch.push_back( branch );
            }
            bValid = readQuestReward( stageNode, sourceName, stage._reward ) && bValid;
            quest._listStage.push_back( stage );
        }
        // 다음 단계와 분기가 가리키는 단계가 있는지, 끝나지 못하는 단계가 없는지 확인한다.
        for ( size_t stageIndex = 0; stageIndex < quest._listStage.size(); ++stageIndex )
        {
            const QuestStage& stage = quest._listStage[stageIndex];
            if ( quest.findStage( stage._id ) != &stage )
            {
                SW_LOG_ERROR_ID( "QuestCatalog.QuestDuplicateStage", "%#: quest '%#' has a duplicate stage '%#'", sourceName, pID, stage._id.c_str() ); // 찾기는 앞의 것만 돌려주어 뒤의 것에는 닿지 못한다
                bValid = false;
            }
            const bool bDeadEnd = stage._bComplete == false && stage._bFail == false && stage._nextStage.empty() && stage._listBranch.empty();
            if ( bDeadEnd )
                SW_LOG_WARNING_ID( "QuestCatalog.QuestStageDeadEnd", "%#: quest '%#' stage '%#' has no next stage and is neither complete nor fail - the quest stays active forever", sourceName, pID, stage._id.c_str() );
            // 보상은 완료 단계에서만 준다 — 앞 단계의 보상은 퀘스트를 포기 · 실패한 뒤 다시 받으면 다시 나간다(받은 단계 기록이 없다, 감사 G-4 — 보상 복제 길).
            const bool bRewarded = stage._reward._items.isEmpty() == false || stage._reward._values.isEmpty() == false || stage._reward._listAction.empty() == false;
            if ( bRewarded && stage._bComplete == false )
            {
                SW_LOG_ERROR_ID( "QuestCatalog.StageRewardBeforeComplete",
                                 "%#: quest '%#' stage '%#' gives a reward but is not the complete stage - abandoning and taking the quest again would repeat it; move the reward to the complete stage",
                                 sourceName, pID, stage._id.c_str() );
                bValid = false;
            }
            // 없는 단계로 가는 다음 · 갈래는 참조 표(`engine/settings/datacatalogreferences.xml` — 같은 퀘스트 안)가 오류로 알리고 로드를 멈춘다(예전에는 경고만 하고 영원히 도는 퀘스트가 섰다).
        }
        if ( quest._listStage.empty() )
        {
            SW_LOG_WARNING_ID( "QuestCatalog.QuestNoStages", "%#: quest '%#' has no stages", sourceName, pID );
            return false;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "QuestCatalog.QuestInvalidFieldsSkipped", "%#: quest '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        (void)add( quest );
        return true;
    }
} // namespace sw

namespace sw
{
    /** @brief `QuestCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Quests, QuestCatalog, SW_GF_API, AssetTypeDef::makeDataFile( QuestCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/quests.xml" }, AssetTypeLoader::of<QuestCatalog>( MemoryTag::Game ) ) );
} // namespace sw
