/**
 * @file QuestCatalog.h
 * @brief 퀘스트 정의 — 선행 퀘스트 · 레벨, 단계(목표 · 다음 단계 · 선택지 분기 · 완료 · 실패), 보상입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 목표 하나 — "무엇을(종류) 누구에게(대상) 몇 번" 입니다. 종류 · 대상은 문자열이라 게임이 정합니다("Kill" · "Collect" · "Talk" · "Reach"). */
    struct QuestObjective
    {
        hashed_string _kind{};
        hashed_string _target{};
        string        _text{};
        int32         _count{ 1 };
        uint8         _bOptional{ SW_FALSE }; ///< 끝내지 않아도 단계가 넘어간다(덤 보상 · 다른 결말 조건)
    };
} // namespace sw

namespace sw
{
    /** @brief 선택지 하나 — 고르면 그 단계로 갑니다. */
    struct QuestBranch
    {
        hashed_string _choice{};
        hashed_string _nextStage{};
    };
} // namespace sw

namespace sw
{
    /** @brief 보상입니다. 아이템은 값 목록(`ItemStackList`), 그 밖(경험치 · 돈 · 평판)은 이름 → 수치입니다. */
    struct QuestReward
    {
        ItemStackList _items{};
        StatBlock     _values{}; ///< `xp="100" gold="50" rep.town="10"` — 이름은 게임이 읽는다
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 하나입니다. */
    struct QuestStage
    {
        hashed_string          _id{};
        hashed_string          _nextStage{}; ///< 목표를 다 하면(비면 완료 · 실패 단계거나 선택지 단계)
        string                 _text{};      ///< 일지에 보일 글
        vector<QuestObjective> _listObjective{};
        vector<QuestBranch>    _listBranch{};      ///< 있으면 목표를 다 한 뒤 선택을 기다린다
        QuestReward            _reward{};          ///< 이 단계에 들어설 때 준다(완료 단계면 퀘스트 보상)
        float32                _timeLimit{ 0.0f }; ///< 초 — 넘으면 실패(0 = 없음)
        uint8                  _bComplete{ SW_FALSE };
        uint8                  _bFail{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 퀘스트 하나입니다. 첫 단계에서 시작합니다. */
    struct SW_GF_API QuestDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listRequiredQuest{}; ///< 끝낸(완료) 퀘스트
        vector<QuestStage>    _listStage{};
        int32                 _requiredLevel{ 0 };
        uint8                 _bRepeatable{ SW_FALSE };

        const QuestStage* findStage( const hashed_string& stageID ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class QuestCatalog
     * @brief XML 형식입니다.
     * @code
     *     <QuestCatalog>
     *       <Quest id="wolves" name="Wolf Trouble" level="2" requires="intro" repeatable="false">
     *         <Stage id="hunt" next="report" text="Kill wolves" time="0">
     *           <Objective kind="Kill" target="wolf" count="5" text="Wolves slain"/>
     *           <Objective kind="Collect" target="pelt" count="3" optional="true"/>
     *         </Stage>
     *         <Stage id="report"><Objective kind="Talk" target="elder"/><Branch choice="spare" next="mercy"/><Branch choice="kill" next="blood"/></Stage>
     *         <Stage id="mercy" complete="true"><Reward xp="100" rep.town="10"><Item item="ring" count="1"/></Reward></Stage>
     *         <Stage id="blood" complete="true"><Reward xp="150"/></Stage>
     *       </Quest>
     *     </QuestCatalog>
     * @endcode
     */
    class SW_GF_API QuestCatalog : public XMLCatalog<QuestCatalog>
    {
        friend class XMLCatalog<QuestCatalog>;

    public:
        const QuestDef*         findQuest( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<QuestDef>& getQuests() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXMLRootName = "QuestCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        GameCatalog<QuestDef> _catalog{};
    };
} // namespace sw
