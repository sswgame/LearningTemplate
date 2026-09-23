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

    /** @brief 대화 노드 타입입니다. */
    enum class DialogueAssetNodeType : uint8
    {
        Start = 0,
        Dialogue,
        Choice,
        Branch,
        Action,
        End
    };

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

    /** @brief 대화 그래프 링크입니다. */
    struct DialogueAssetLink
    {
        int32 _id{ 0 };      /**< 그래프 안에서 유일한 링크 id 입니다. */
        int32 _fromPin{ 0 }; /**< 출발 핀 번호입니다(`DialogueGraphAsset::encodePin` 참고). */
        int32 _toPin{ 0 };   /**< 도착 핀 번호입니다. */
    };

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
        bool loadFromFile( string_view path );
        /** @brief JSON 파일을 씁니다. */
        bool saveToFile( string_view path ) const;
        /** @brief JSON 본문을 파싱합니다. */
        bool parseJson( string_view json );
        /** @brief JSON 본문을 만듭니다. */
        string toJson() const;
        /** @brief 노드 타입 이름을 반환합니다. 모르는 값이면 "Unknown" 입니다. */
        static const utf8* nodeTypeName( DialogueAssetNodeType type );
        /** @brief 노드 타입 문자열을 파싱합니다. 모르는 이름이면 Dialogue 입니다. */
        static DialogueAssetNodeType parseNodeType( string_view typeStr );
        /** @brief Start 노드를 반환합니다. 없으면 nullptr 입니다. */
        const DialogueAssetNode* findStartNode() const;
        /** @brief id 로 노드를 찾습니다. */
        const DialogueAssetNode* findNode( int32 nodeId ) const;

        // ------------------------------------------------------------------------------
        // 핀 번호 계약: **이 파일이 기준이다.**
        //
        // 핀 번호는 `노드 id * kPinScale + 오프셋` 이고 링크의 양 끝으로 디스크에 저장된다.
        // 예전에는 **에디터 패널이 인코딩을, 이 에셋이 디코딩을 각자 적고 있었다.** 오프셋
        // 상수가 두 파일에 따로 있었고, 한쪽만 바뀌면 대화가 조용히 엉뚱한 분기를 탄다(증상은
        // "가끔 다른 대사가 나온다" 라서 추적이 어렵다). 인코딩도 여기로 가져왔다.
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

        /** @brief 핀 값에서 노드 id 를 꺼냅니다(nodeId*100+offset, 레거시 *10). */
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

        vector<DialogueAssetNode> _listNode; /**< 노드 목록입니다. */
        vector<DialogueAssetLink> _listLink; /**< 링크 목록입니다. */

    private:
        /** @brief 이미 파싱된 JSON 루트에서 노드 · 링크를 읽습니다. */
        void parseRoot( const JsonValue& root );
    };
} // namespace sw
