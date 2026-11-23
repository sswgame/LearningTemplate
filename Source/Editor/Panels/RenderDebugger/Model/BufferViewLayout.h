/**
 * @file BufferViewLayout.h
 * @brief 구조 버퍼 바이트를 표 행으로 푸는 규칙입니다 — 원소 보폭(등록 크기 `RenderBufferEntry::_elementSize`)마다 한 행, 4 바이트 칸마다 한 열(언리얼 RDG Insights 의 버퍼 보기 · RenderDoc Buffer Viewer 의 형식 지정 자리).
 * @details 칸의 해석은 버퍼를 거는 컴퓨트 셰이더의 멤버(`RenderBufferEntry::_listMember`)이고, 모르거나 다르게 보려면 사람이 고른다(실수 · 부호 없는 정수 · 정수 · 16 진수).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

namespace sw::editor
{
    /** @brief 4 바이트 칸 하나를 어떻게 읽을지입니다. */
    enum class BufferViewScalar : uint8
    {
        Shader, ///< 그 버퍼를 거는 컴퓨트 셰이더의 멤버 형식(모르는 칸은 16 진수)
        Float,  ///< float32(HZB 깊이 · 가림 타일)
        Uint,   ///< uint32(간접 인자 · 번호)
        Int,    ///< int32
        Hex,    ///< 날 비트(16 진수)
        Count,
    };

    /**
     * @struct BufferViewLayout
     * @brief 파일 머리말 참고.
     */
    struct BufferViewLayout
    {
        static constexpr uint32 kCellBytes      = 4;    ///< 칸 하나의 바이트
        static constexpr uint32 kMaxColumnCount = 16;   ///< 표가 보이는 칸 수의 위 한계(보폭 64 B) — 넘는 칸은 자른다
        static constexpr uint32 kMaxRowCount    = 4096; ///< 한 번에 표로 만드는 행 수의 위 한계(큰 피라미드를 통째로 글로 만들지 않는다)

        uint32                   _stride{ 0 };      ///< 원소 바이트(0 이면 풀 수 없다)
        uint32                   _columnCount{ 0 }; ///< 보이는 칸 수
        BufferViewScalar         _scalar{ BufferViewScalar::Shader };
        vector<string>           _listColumnName;   ///< 칸마다의 이름 — 멤버를 알면 `멤버.x`, 모르면 바이트 오프셋(`+4`)
        vector<BufferViewScalar> _listColumnScalar; ///< `Shader` 로 볼 때 칸마다의 해석

        /** @brief 원소 @p elementSize 바이트 · 칸 해석 @p scalar 의 배치입니다. 4 의 배수가 아닌 끝 바이트는 칸이 되지 못한다. */
        static BufferViewLayout make( uint32 elementSize, BufferViewScalar scalar, const vector<RenderBufferEntry::Member>& listMember = {} );
        /** @brief @p byteCount 바이트에 든 행 수입니다(`kMaxRowCount` 에서 자른다). */
        uint32 getRowCount( size_t byteCount ) const;
        /** @brief 행 @p row 의 칸을 글로 @p outListCell 에 채웁니다(버퍼를 다시 쓴다). 행이 바이트 밖이면 false 입니다. */
        [[nodiscard]] bool formatRow( const vector<uint8>& byte, uint32 row, vector<string>& outListCell ) const;
        /** @brief 칸 해석의 표시 이름입니다("float" · "uint" · "int" · "hex"). */
        static const utf8* getScalarLabel( BufferViewScalar scalar );
    };
} // namespace sw::editor
