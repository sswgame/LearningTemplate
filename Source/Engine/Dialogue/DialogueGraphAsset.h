/**
 * @file DialogueGraphAsset.h
 * @brief 에디터와 런타임이 함께 쓰는 대화 그래프 JSON 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class TextGatherer;

    /**
     * @brief 대화 노드 타입입니다. 파일에는 열거자 이름(`"Choice"`)으로 적힙니다.
     * @details 종류를 추가하면 `kArrDialogueNodeInfo` 에 항목 하나와 `DialogueRunnerComponent::executeNode` 의 case 하나를 추가한다.
     *          테이블은 static_assert 가, 러너의 switch 는 -Wswitch-enum 오류가 누락을 알려 준다.
     */
    ENUM()
    enum class DialogueAssetNodeType : uint8
    {
        Start = 0,
        Dialogue,
        Choice,
        Branch,
        Action,
        End,
        Count /**< 표식입니다. 노드 타입이 아닙니다. */
    };

    /** @brief 노드가 다음으로 넘어가려면 필요한 조건입니다. 런타임 러너와 에디터 미리보기가 같은 값을 봅니다. */
    enum class DialogueNodeFlow : uint8
    {
        PassThrough, /**< 입력 없이 곧바로 다음 노드로 갑니다. */
        WaitAdvance, /**< 대사를 보이고 진행 입력을 기다립니다. */
        WaitChoice,  /**< 선택지를 보이고 고를 때까지 기다립니다. */
        Condition,   /**< 조건식 결과로 다음 노드를 고릅니다. 러너는 식을 평가하고, 미리보기에서는 사용자가 참이나 거짓을 고릅니다. */
        Finish       /**< 대화가 끝납니다. */
    };

    /** @brief 노드의 출력 핀 형태입니다. `DialogueCursor::step` 이 다음 노드를 찾는 규칙도 이 값으로 정해집니다. */
    enum class DialogueNodeOutput : uint8
    {
        None,   /**< 출력 핀이 없습니다. 다음 노드는 없습니다. */
        Next,   /**< 기본 출력 핀(`kPinOffsetOut`) 하나입니다. */
        Branch, /**< 참 핀과 거짓 핀이 있습니다. 연결되지 않은 쪽은 기본 출력 핀으로 갑니다. */
        Choice  /**< 선택지마다 핀이 하나씩 있습니다. 연결되지 않은 선택지는 기본 출력 핀으로 갑니다. */
    };

    /** @brief 노드가 편집하는 본문 필드입니다. */
    enum class DialogueNodeBody : uint8
    {
        None,      /**< 본문이 없습니다. */
        Text,      /**< `_text` 입니다. 대사나 선택지 질문이 들어갑니다. */
        Condition, /**< `_condition` 입니다. Branch 노드의 조건식이 들어갑니다. */
        Action     /**< `_actionCommand` 입니다. 실행할 명령이 들어갑니다. */
    };

    /** @brief 노드 종류 하나의 특성입니다. 이름, 핀, 진행 방식, 편집 필드, 기본값이 모두 이 항목에 있습니다. */
    struct DialogueNodeInfo
    {
        const utf8*           _pName;              /**< 화면 이름입니다(파일의 종류 이름은 열거자 이름이다). */
        const utf8*           _pDefaultSpeaker;    /**< 새 노드의 화자입니다. */
        const utf8*           _pDefaultBody;       /**< 새 노드의 본문입니다. `_body` 필드에 들어갑니다. */
        float4                _color;              /**< 그래프 노드 머리 색입니다. */
        DialogueAssetNodeType _type;               /**< 노드 타입입니다. 테이블의 순번과 같아야 합니다. */
        DialogueNodeFlow      _flow;               /**< 다음으로 넘어가는 방식입니다. */
        DialogueNodeOutput    _output;             /**< 출력 핀 형태입니다. */
        DialogueNodeBody      _body;               /**< 편집하는 본문 필드입니다. */
        bool                  _bHasInputPin;       /**< 입력 핀이 있는지입니다. */
        bool                  _bHasSpeaker;        /**< 화자 필드를 편집하는지입니다. */
        bool                  _bAddable;           /**< 에디터 추가 메뉴에 보이는지입니다. Start 는 그래프마다 하나뿐이라 보이지 않습니다. */
        uint8                 _defaultChoiceCount; /**< 새 노드에 넣을 선택지 수입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 노드 종류 테이블입니다. **종류마다 항목이 하나이고, 순서는 `DialogueAssetNodeType` 값 순서입니다.**
     * @details 에셋 이름 해석, 런타임 진행(`DialogueCursor`), 에디터 그리기, 추가 메뉴, 인스펙터가 모두 이 테이블을 읽는다.
     */
    inline constexpr DialogueNodeInfo kArrDialogueNodeInfo[] = {
        {   "Start",    "",                       "", float4( 0.2f, 0.9f, 0.3f, 1.0f ),    DialogueAssetNodeType::Start, DialogueNodeFlow::PassThrough,   DialogueNodeOutput::Next,      DialogueNodeBody::None, false, false, false, 0},
        {"Dialogue", "NPC", "Enter dialogue text...", float4( 0.4f, 0.7f, 1.0f, 1.0f ), DialogueAssetNodeType::Dialogue, DialogueNodeFlow::WaitAdvance,   DialogueNodeOutput::Next,      DialogueNodeBody::Text,  true,  true,  true, 0},
        {  "Choice",    "",         "Player options", float4( 0.8f, 0.5f, 1.0f, 1.0f ),   DialogueAssetNodeType::Choice,  DialogueNodeFlow::WaitChoice, DialogueNodeOutput::Choice,      DialogueNodeBody::Text,  true, false,  true, 2},
        {  "Branch",    "",           "visited == 1", float4( 1.0f, 0.8f, 0.2f, 1.0f ),   DialogueAssetNodeType::Branch,   DialogueNodeFlow::Condition, DialogueNodeOutput::Branch, DialogueNodeBody::Condition,  true, false,  true, 0},
        {  "Action",    "",     "give_item:potion:1", float4( 0.2f, 0.9f, 0.9f, 1.0f ),   DialogueAssetNodeType::Action, DialogueNodeFlow::PassThrough,   DialogueNodeOutput::Next,    DialogueNodeBody::Action,  true, false,  true, 0},
        {     "End",    "",                       "", float4( 0.9f, 0.3f, 0.3f, 1.0f ),      DialogueAssetNodeType::End,      DialogueNodeFlow::Finish,   DialogueNodeOutput::None,      DialogueNodeBody::None,  true, false,  true, 0},
    };

    static_assert( SW_COUNT_OF( kArrDialogueNodeInfo ) == static_cast<size_t>( DialogueAssetNodeType::Count ),
                   "DialogueAssetNodeType 을 늘렸으면 kArrDialogueNodeInfo 에도 줄을 더할 것" );

    /** @brief 대화 그래프 노드입니다. id 와 종류는 파일에 반드시 적습니다(`PROPERTY( Required )`). */
    REFLECT()
    struct SW_API DialogueAssetNode
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Node id, unique in the graph and 1 or more" )
        int32 _id{ 0 };
        PROPERTY( Required, Tooltip = "Node type" )
        DialogueAssetNodeType _type{ DialogueAssetNodeType::Dialogue };
        PROPERTY( Tooltip = "Speaker name or localization key" )
        string _speaker;
        PROPERTY( Tooltip = "Line text or localization key" )
        string _text;
        PROPERTY( Tooltip = "Condition the Branch node evaluates" )
        string _condition;
        PROPERTY( Tooltip = "Command the Action node runs" )
        string _actionCommand;
        /** @brief Choice 노드의 선택지입니다. 순서가 곧 핀 번호입니다. */
        PROPERTY( Tooltip = "Choices of the Choice node; the order is the pin number" )
        vector<string> _listChoice;
        /** @brief 그래프 에디터에서의 노드 위치입니다. */
        PROPERTY( Tooltip = "Node position in the graph editor" )
        float2 _position{ 40.0f, 40.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 대화 그래프 링크입니다. 모든 칸을 파일에 반드시 적습니다(`PROPERTY( Required )`). */
    REFLECT()
    struct SW_API DialogueAssetLink
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Link id, unique in the graph and 1 or more" )
        int32 _id{ 0 };
        /** @brief 출발 핀 번호입니다(`DialogueGraphAsset::encodePin` 참고). */
        PROPERTY( Required, Tooltip = "Source pin (node id * 100 + pin offset)" )
        int32 _fromPin{ 0 };
        PROPERTY( Required, Tooltip = "Target pin (node id * 100 + pin offset)" )
        int32 _toPin{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class DialogueGraphAsset
     * @brief 대화 그래프 JSON 입니다. 노드 text 는 로컬라이즈 키로 해석합니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigJSON` — 모르는 키 · 읽지 못한 값 · 모르는 노드 종류 · 빠진 필수 칸은 오류).
     *          키는 PROPERTY 이름(`_listNode`, `_listLink`)이 정본이다. 읽은 뒤 id 가 1 이상이고 겹치지 않는지, 링크의 핀이 있는지만 따로 본다.
     */
    REFLECT()
    class SW_API DialogueGraphAsset
    {
    public:
        REFLECT_BODY();

        /** @brief 대화 그래프 파일의 접미사입니다. 에디터 종류 표(`EditorAssetType.cpp`)가 이 값을 씁니다. */
        static constexpr string_view kExtension = ".dialogue.json";

        /** @brief 빈 그래프를 만듭니다. */
        DialogueGraphAsset() = default;

        /** @brief JSON 파일을 읽습니다. 읽지 못하면 false 이고 그래프는 비어 있습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief JSON 본문을 읽습니다. 틀린 글이면 false 이고 그래프는 비어 있습니다. */
        [[nodiscard]] bool parseJSON( string_view json );
        /** @brief JSON 본문을 만듭니다(들여쓰기 2, 모든 PROPERTY). */
        string toJSON() const;
        /** @brief 노드 타입의 화면 이름을 반환합니다. 모르는 값이면 "Unknown" 입니다. */
        static const utf8* nodeTypeName( DialogueAssetNodeType type );
        /** @brief 노드 타입의 특성 줄입니다. 범위 밖 값이면 nullptr 입니다. */
        static const DialogueNodeInfo* findNodeInfo( DialogueAssetNodeType type );
        /** @brief Start 노드를 반환합니다. 없으면 nullptr 입니다. */
        const DialogueAssetNode* findStartNode() const;
        /** @brief id 로 노드를 찾습니다. */
        const DialogueAssetNode* findNode( int32 nodeID ) const;

        // ------------------------------------------------------------------------------
        // 핀 번호 계약: **이 파일이 기준이다.**
        //
        // 핀 번호는 `노드 id * kPinScale + 오프셋` 이고 링크의 양 끝으로 디스크에 저장된다.
        // 인코딩과 디코딩을 모두 여기서 한다. 에디터 패널이 오프셋을 따로 가지면 한쪽만 바뀔 때 대화가 조용히 엉뚱한 분기를 탄다.
        // (증상은 "가끔 다른 대사가 나온다" 라서 추적이 어렵다).
        // ------------------------------------------------------------------------------

        /** @brief 핀 번호의 자릿수 기준입니다. 한 노드가 가질 수 있는 핀 오프셋 개수이기도 합니다. */
        static constexpr int32 kPinScale = 100;
        /** @brief 들어오는 핀의 오프셋입니다. */
        static constexpr int32 kPinOffsetIn = 1;
        /** @brief 기본으로 나가는 핀의 오프셋입니다. */
        static constexpr int32 kPinOffsetOut = 2;
        /** @brief Branch 참 핀의 오프셋입니다. */
        static constexpr int32 kPinOffsetTrue = 3;
        /** @brief Branch 거짓 핀의 오프셋입니다. */
        static constexpr int32 kPinOffsetFalse = 4;
        /** @brief 첫 선택지 핀의 오프셋입니다. 선택지 n 은 여기에 n 을 더한 값입니다. */
        static constexpr int32 kPinOffsetChoiceBase = 10;

        /**
         * @brief 노드 id 와 오프셋으로 핀 번호를 만듭니다.
         * @return 오프셋이 `kPinScale` 에 담기지 않거나 노드 id 가 1 미만이면 0 입니다.
         *         담기지 않는 오프셋은 **자릿수를 넘어 노드 id 를 오염시킵니다.**
         */
        static int32 encodePin( int32 nodeID, int32 pinOffset );
        /** @brief 선택지 하나의 핀 번호를 만듭니다. 담기지 않으면 0 입니다. */
        static int32 encodeChoicePin( int32 nodeID, int32 choiceIndex );
        /** @brief 한 노드가 가질 수 있는 최대 선택지 개수입니다. */
        static constexpr int32 getMaxChoiceCount() { return kPinScale - kPinOffsetChoiceBase; }

        /** @brief 핀 값(nodeID * kPinScale + offset)에서 노드 id 를 꺼냅니다. kPinScale 보다 작은 값은 핀이 아니라 0 입니다. */
        static int32 decodePinNodeID( int32 pin );
        /** @brief 핀 값에서 오프셋을 꺼냅니다. */
        static int32 decodePinOffset( int32 pin );
        /** @brief from 노드의 그 핀 오프셋이 가리키는 노드 id 입니다. 없으면 0 입니다. */
        int32 findLinkedNodeID( int32 fromNodeID, int32 pinOffset ) const;
        /** @brief 기본 Out 핀의 다음 노드 id 입니다. */
        int32 findDefaultNextNodeID( int32 fromNodeID ) const;
        /** @brief 선택지 핀의 다음 노드 id 입니다. */
        int32 findChoiceNextNodeID( int32 fromNodeID, int32 choiceIndex ) const;
        /** @brief Branch True/False 핀의 다음 노드 id 입니다. */
        int32 findBranchNextNodeID( int32 fromNodeID, bool bTrue ) const;
        /** @brief text 필드를 로컬라이즈 키로 해석합니다. 키가 없으면 원문을 반환합니다. */
        static string resolveLocalizedText( string_view textOrKey );
        /** @brief 화자, 대사, 선택지를 로컬라이제이션 수집기에 넣습니다. 키이거나 글 그대로인 값을 넣으며, `resolveLocalizedText` 가 같은 규칙으로 찾습니다. */
        void collectLocalizableText( TextGatherer& gatherer, string_view origin ) const;

        PROPERTY( Tooltip = "Nodes of the graph" )
        vector<DialogueAssetNode> _listNode;
        PROPERTY( Tooltip = "Links between node pins" )
        vector<DialogueAssetLink> _listLink;

    private:
        /** @brief 읽은 본문(@p json)을 이 그래프에 채우고 의미를 봅니다. 틀리면 그래프를 비우고 false 입니다. */
        [[nodiscard]] bool readText( string_view json, string_view sourceLabel );
        /** @brief 노드 · 링크 id 가 1 이상이고 겹치지 않는지, 링크의 두 핀이 있는지 봅니다. 어긋난 줄마다 오류를 남깁니다. */
        [[nodiscard]] bool validateFile( string_view sourceLabel ) const;
    };
} // namespace sw
