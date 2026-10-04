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

namespace sw
{
    class JsonValue;
    class TextGatherer;

    /**
     * @brief 대화 노드 타입입니다.
     * @details 종류를 더하면 `kArrDialogueNodeInfo` 에 줄 하나와 `DialogueRunnerComponent::executeNode` 의 case 하나를 더합니다.
     *          표는 static_assert 가, 러너 switch 는 그 파일의 -Wswitch-enum 오류가 빠뜨림을 짚습니다.
     */
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

    /** @brief 노드가 다음으로 넘어가려면 무엇이 필요한지입니다. 런타임 러너와 에디터 미리보기가 같은 값을 봅니다. */
    enum class DialogueNodeFlow : uint8
    {
        PassThrough, /**< 입력 없이 곧바로 다음 노드로 갑니다. */
        WaitAdvance, /**< 대사를 보이고 진행 입력을 기다립니다. */
        WaitChoice,  /**< 선택지를 보이고 고를 때까지 기다립니다. */
        Condition,   /**< 조건식 결과로 갈 곳을 고릅니다. 러너는 식을 평가하고, 미리보기는 사람이 참 · 거짓을 고릅니다. */
        Finish       /**< 대화가 끝납니다. */
    };

    /** @brief 노드가 내는 출력 핀의 모양입니다. 다음 노드를 찾는 규칙(`DialogueCursor::step`)도 이것이 정합니다. */
    enum class DialogueNodeOutput : uint8
    {
        None,   /**< 출력 핀이 없습니다. 다음 노드는 없습니다. */
        Next,   /**< 기본 출력 핀(`kPinOffsetOut`) 하나입니다. */
        Branch, /**< 참 · 거짓 핀입니다. 이어지지 않은 쪽은 기본 출력 핀으로 갑니다. */
        Choice  /**< 선택지마다 핀 하나입니다. 이어지지 않은 선택지는 기본 출력 핀으로 갑니다. */
    };

    /** @brief 노드가 편집하는 본문 글 칸입니다. */
    enum class DialogueNodeBody : uint8
    {
        None,      /**< 본문이 없습니다. */
        Text,      /**< `_text` — 대사 · 선택지 질문입니다. */
        Condition, /**< `_condition` — Branch 조건식입니다. */
        Action     /**< `_actionCommand` — 실행할 명령입니다. */
    };

    /** @brief 노드 종류 하나의 특성입니다. 이름 · 핀 · 진행 방식 · 편집 칸 · 기본값이 모두 이 줄에서 옵니다. */
    struct DialogueNodeInfo
    {
        const utf8*           _pName;              /**< JSON `type` 값이자 화면 이름입니다. */
        const utf8*           _pDefaultSpeaker;    /**< 새 노드의 화자입니다. */
        const utf8*           _pDefaultBody;       /**< 새 노드의 본문(`_body` 칸)입니다. */
        float4                _color;              /**< 그래프 노드 머리 색입니다. */
        DialogueAssetNodeType _type;               /**< 노드 타입입니다. 표의 순번과 같아야 합니다. */
        DialogueNodeFlow      _flow;               /**< 다음으로 넘어가는 방식입니다. */
        DialogueNodeOutput    _output;             /**< 출력 핀 모양입니다. */
        DialogueNodeBody      _body;               /**< 편집하는 본문 칸입니다. */
        bool                  _bHasInputPin;       /**< 들어오는 핀이 있는지입니다. */
        bool                  _bHasSpeaker;        /**< 화자 칸을 편집하는지입니다. */
        bool                  _bAddable;           /**< 에디터 추가 메뉴에 보이는지입니다(Start 는 그래프마다 하나라 없음). */
        uint8                 _defaultChoiceCount; /**< 새 노드에 넣을 선택지 수입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 노드 종류 표입니다. **종류마다 한 줄이고, 순서는 `DialogueAssetNodeType` 값 순서입니다.**
     * @details 에셋 이름 해석 · 런타임 진행(`DialogueCursor`) · 에디터 그리기 · 추가 메뉴 · 인스펙터가 모두 이 표를 봅니다.
     */
    inline constexpr DialogueNodeInfo kArrDialogueNodeInfo[] = {
        {   "Start",    "",                       "", float4( 0.2f, 0.9f, 0.3f, 1.0f ),    DialogueAssetNodeType::Start, DialogueNodeFlow::PassThrough,   DialogueNodeOutput::Next,      DialogueNodeBody::None, false, false, false, 0},
        {"Dialogue", "NPC", "Enter dialogue text...", float4( 0.4f, 0.7f, 1.0f, 1.0f ), DialogueAssetNodeType::Dialogue, DialogueNodeFlow::WaitAdvance,   DialogueNodeOutput::Next,      DialogueNodeBody::Text,  true,  true,  true, 0},
        {  "Choice",    "",         "Player options", float4( 0.8f, 0.5f, 1.0f, 1.0f ),   DialogueAssetNodeType::Choice,  DialogueNodeFlow::WaitChoice, DialogueNodeOutput::Choice,      DialogueNodeBody::Text,  true, false,  true, 2},
        {  "Branch",    "",      "flag.visited == 1", float4( 1.0f, 0.8f, 0.2f, 1.0f ),   DialogueAssetNodeType::Branch,   DialogueNodeFlow::Condition, DialogueNodeOutput::Branch, DialogueNodeBody::Condition,  true, false,  true, 0},
        {  "Action",    "",     "give_item:potion:1", float4( 0.2f, 0.9f, 0.9f, 1.0f ),   DialogueAssetNodeType::Action, DialogueNodeFlow::PassThrough,   DialogueNodeOutput::Next,    DialogueNodeBody::Action,  true, false,  true, 0},
        {     "End",    "",                       "", float4( 0.9f, 0.3f, 0.3f, 1.0f ),      DialogueAssetNodeType::End,      DialogueNodeFlow::Finish,   DialogueNodeOutput::None,      DialogueNodeBody::None,  true, false,  true, 0},
    };

    static_assert( SW_COUNT_OF( kArrDialogueNodeInfo ) == static_cast<size_t>( DialogueAssetNodeType::Count ),
                   "DialogueAssetNodeType 을 늘렸으면 kArrDialogueNodeInfo 에도 줄을 더할 것" );

    /** @brief 대화 그래프 노드입니다. */
    struct DialogueAssetNode
    {
        int32                 _id{ 0 };                                 /**< 그래프 안에서 유일한 노드 id. 1 이상이어야 합니다. */
        DialogueAssetNodeType _type{ DialogueAssetNodeType::Dialogue }; /**< 노드 종류입니다. */
        string                _speaker;                                 /**< 화자 이름(또는 로컬라이즈 키)입니다. */
        string                _text;                                    /**< 대사 원문 또는 로컬라이즈 키입니다. */
        string                _condition;                               /**< Branch 노드가 평가할 조건식입니다. */
        string                _actionCommand;                           /**< Action 노드가 실행할 명령입니다. */
        vector<string>        _listChoice;                              /**< Choice 노드의 선택지입니다. 순서가 곧 핀 번호입니다. */
        /** @brief 그래프 에디터에서의 노드 위치입니다. JSON 키는 그대로 "x"/"y" 라 파일 형식은 바뀌지 않습니다. */
        float2 _position{ 40.0f, 40.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 대화 그래프 링크입니다. */
    struct DialogueAssetLink
    {
        int32 _id{ 0 };      /**< 그래프 안에서 유일한 링크 id 입니다. */
        int32 _fromPin{ 0 }; /**< 출발 핀 번호입니다(`DialogueGraphAsset::encodePin` 참고). */
        int32 _toPin{ 0 };   /**< 도착 핀 번호입니다. */
    };
} // namespace sw

namespace sw
{
    /**
     * @class DialogueGraphAsset
     * @brief 대화 그래프 JSON 입니다. 노드 text 는 로컬라이즈 키로 해석합니다.
     */
    class SW_API DialogueGraphAsset
    {
    public:
        /** @brief 빈 그래프를 만듭니다. */
        DialogueGraphAsset() = default;

        /** @brief JSON 파일을 읽습니다. */
        [[nodiscard]] bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief JSON 본문을 파싱합니다. */
        [[nodiscard]] bool parseJson( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJson() const;
        /** @brief 노드 타입 이름을 반환합니다. 모르는 값이면 "Unknown" 입니다. */
        static const utf8* nodeTypeName( DialogueAssetNodeType type );
        /** @brief 노드 타입 문자열을 파싱합니다. 모르는 이름이면 Dialogue 입니다. */
        static DialogueAssetNodeType parseNodeType( string_view typeStr );
        /** @brief 노드 타입의 특성 줄입니다. 범위 밖 값이면 nullptr 입니다. */
        static const DialogueNodeInfo* findNodeInfo( DialogueAssetNodeType type );
        /** @brief Start 노드를 반환합니다. 없으면 nullptr 입니다. */
        const DialogueAssetNode* findStartNode() const;
        /** @brief id 로 노드를 찾습니다. */
        const DialogueAssetNode* findNode( int32 nodeId ) const;

        // ------------------------------------------------------------------------------
        // 핀 번호 계약: **이 파일이 기준이다.**
        //
        // 핀 번호는 `노드 id * kPinScale + 오프셋` 이고 링크의 양 끝으로 디스크에 저장된다.
        // 인코딩 · 디코딩 모두 여기 있다 — 에디터 패널이 오프셋을 따로 들면 한쪽만 바뀔 때 대화가 조용히 엉뚱한 분기를 탄다
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
        static int32 encodePin( int32 nodeId, int32 pinOffset );
        /** @brief 선택지 하나의 핀 번호를 만듭니다. 담기지 않으면 0 입니다. */
        static int32 encodeChoicePin( int32 nodeId, int32 choiceIndex );
        /** @brief 한 노드가 가질 수 있는 최대 선택지 개수입니다. */
        static constexpr int32 getMaxChoiceCount() { return kPinScale - kPinOffsetChoiceBase; }

        /** @brief 핀 값(nodeId * kPinScale + offset)에서 노드 id 를 꺼냅니다. kPinScale 보다 작은 값은 핀이 아니라 0 입니다. */
        static int32 decodePinNodeId( int32 pin );
        /** @brief 핀 값에서 오프셋을 꺼냅니다. */
        static int32 decodePinOffset( int32 pin );
        /** @brief from 노드의 그 핀 오프셋이 가리키는 노드 id 입니다. 없으면 0 입니다. */
        int32 findLinkedNodeId( int32 fromNodeId, int32 pinOffset ) const;
        /** @brief 기본 Out 핀의 다음 노드 id 입니다. */
        int32 findDefaultNextNodeId( int32 fromNodeId ) const;
        /** @brief 선택지 핀의 다음 노드 id 입니다. */
        int32 findChoiceNextNodeId( int32 fromNodeId, int32 choiceIndex ) const;
        /** @brief Branch True/False 핀의 다음 노드 id 입니다. */
        int32 findBranchNextNodeId( int32 fromNodeId, bool bTrue ) const;
        /** @brief text 필드를 로컬라이즈 키로 해석합니다. 키가 없으면 원문을 반환합니다. */
        static string resolveLocalizedText( string_view textOrKey );
        /** @brief 화자 · 대사 · 선택지를 로컬라이제이션 수집기에 넣습니다(키이거나 글 그대로 — `resolveLocalizedText` 가 같은 규칙으로 찾는다). */
        void collectLocalizableText( TextGatherer& gatherer, string_view origin ) const;

        vector<DialogueAssetNode> _listNode; /**< 노드 목록입니다. */
        vector<DialogueAssetLink> _listLink; /**< 링크 목록입니다. */

    private:
        /** @brief 이미 파싱된 JSON 루트에서 노드 · 링크를 읽습니다. */
        void parseRoot( const JsonValue& root );
    };
} // namespace sw
