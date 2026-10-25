/**
 * @file AudioGraphEditUtil.h
 * @brief 소리 그래프 창의 캔버스 편집(노드 · 핀 · 연결 id, 잇기 · 끊기, 노드 넣기 · 지우기, 처음 배치)입니다. ImGui 에 의존하지 않아 EditorTest 가 테스트합니다.
 * @details 캔버스와 속성 그리드는 같은 모델(`AudioGraphDef` 사본)을 고친다. 연결의 기준은 받는 노드의 입력 이름 목록(`_listInput`)이고, 캔버스의 링크는
 *          그 목록을 그린 것뿐이다 — 링크 표를 따로 두면 두 표가 어긋난다. 노드마다 나가는 핀 하나와(소스가 아니면) 들어오는 핀 하나가 있고,
 *          들어오는 핀은 링크를 여럿 받는다(입력의 합).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Audio/Graph/AudioGraphDef.h"

namespace sw::editor
{
    /**
     * @struct AudioGraphEditUtil
     * @brief 캔버스 편집의 판단과 모델 변경입니다. 실패한 변경은 모델을 바꾸지 않습니다.
     */
    struct AudioGraphEditUtil
    {
        /** @brief 노드 하나가 쓰는 핀 id 칸의 수입니다(나가는 핀 0, 들어오는 핀 1). */
        static constexpr int32 kPinScale = 4;
        /** @brief 링크 id 에서 받는 노드 하나가 쓰는 칸의 수입니다(입력 목록의 자리). 노드 수 상한과 같다. */
        static constexpr int32 kLinkScale = static_cast<int32>( AudioGraphDef::kMaxNodeCount );
        /** @brief 처음 배치의 칸 간격(캔버스 좌표)입니다. */
        static constexpr float32 kLayoutColumnWidth = 240.0f;
        static constexpr float32 kLayoutRowHeight   = 130.0f;

        /** @brief 노드 번호(데이터 순서)의 캔버스 노드 id 입니다(0 은 쓰지 않는다). */
        static int32 makeNodeID( uint32 nodeIndex ) { return static_cast<int32>( nodeIndex ) + 1; }
        /** @brief 노드의 나가는 핀 id 입니다. */
        static int32 makeOutputPinID( uint32 nodeIndex ) { return makeNodeID( nodeIndex ) * kPinScale; }
        /** @brief 노드의 들어오는 핀 id 입니다. */
        static int32 makeInputPinID( uint32 nodeIndex ) { return makeNodeID( nodeIndex ) * kPinScale + 1; }
        /** @brief 받는 노드 @p readerIndex 의 입력 자리 @p inputSlot 을 그린 링크 id 입니다. */
        static int32 makeLinkID( uint32 readerIndex, uint32 inputSlot ) { return makeNodeID( readerIndex ) * kLinkScale + static_cast<int32>( inputSlot ); }
        /** @brief 캔버스 노드 id 의 노드 번호입니다. 범위 밖이면 false 입니다. */
        [[nodiscard]] static bool tryDecodeNodeID( const AudioGraphDef& def, int32 nodeID, uint32& outNodeIndex );
        /** @brief 핀 id 의 노드 번호와 방향입니다. 모르는 핀이면 false 입니다(소스 노드의 들어오는 핀 포함). */
        [[nodiscard]] static bool tryDecodePinID( const AudioGraphDef& def, int32 pinID, uint32& outNodeIndex, bool& outbInput );
        /** @brief 링크 id 의 받는 노드와 입력 자리입니다. 지금 모델에 없는 링크면 false 입니다. */
        [[nodiscard]] static bool tryDecodeLinkID( const AudioGraphDef& def, int32 linkID, uint32& outReaderIndex, uint32& outInputSlot );

        /**
         * @brief 노드 @p fromIndex 의 출력을 노드 @p toIndex 의 입력에 잇습니다(받는 쪽 `_listInput` 끝에 이름을 더한다).
         * @return 받는 쪽이 소스 · 자기 자신 · 이미 이은 노드이거나 고리가 생기면 false 이고 모델은 그대로입니다.
         */
        [[nodiscard]] static bool connect( AudioGraphDef& inoutDef, uint32 fromIndex, uint32 toIndex );
        /** @brief 받는 노드의 입력 자리 하나를 지웁니다. 자리가 없으면 false 입니다. */
        [[nodiscard]] static bool disconnect( AudioGraphDef& inoutDef, uint32 readerIndex, uint32 inputSlot );
        /**
         * @brief 종류 @p type 의 노드를 @p canvasPosition 에 넣고 번호를 돌려줍니다. 이름은 종류 이름 + 겹치지 않는 번호입니다(`Sine2`).
         * @return 노드 수 상한이면 false 이고 모델은 그대로입니다.
         */
        [[nodiscard]] static bool addNode( AudioGraphDef& inoutDef, AudioGraphNodeType type, const float2& canvasPosition, uint32& outNodeIndex );
        /** @brief 노드를 지우고 다른 노드의 입력에서 그 이름을 뺍니다. 출력 노드였으면 출력이 비어 검사가 저장을 막습니다(고를 때까지). */
        [[nodiscard]] static bool removeNode( AudioGraphDef& inoutDef, uint32 nodeIndex );
        /** @brief 노드 이름이 @p oldName 에서 @p newName 으로 바뀌었을 때 다른 노드의 입력과 그래프 출력의 이름을 함께 바꿉니다(속성 그리드에서 이름을 고친 뒤). */
        static void replaceNodeReferences( AudioGraphDef& inoutDef, const hashed_string& oldName, const hashed_string& newName );
        /** @brief 모든 노드의 위치가 원점이면(처음 여는 손으로 쓴 파일) 처리 깊이를 열, 같은 깊이의 순서를 행으로 놓습니다. 놓았으면 true 입니다. */
        static bool layoutIfUnplaced( AudioGraphDef& inoutDef );
    };
} // namespace sw::editor
