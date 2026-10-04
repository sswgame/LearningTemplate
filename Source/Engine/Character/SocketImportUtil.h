/**
 * @file SocketImportUtil.h
 * @brief 임포트 데이터에서 소켓 에셋 초안을 만드는 도구 도우미입니다 — 본에 붙은 강체 메시(본 + 로컬 변환)가 소켓 하나가 됩니다.
 * @details 파일은 **처음 한 번만** 씁니다(`writeIfMissing`). 그 뒤로는 사람이 고치는 원본이고, 재임포트가 덮어쓰지 않습니다 — 소켓을 임포트
 *          산출물과 따로 두는 이유가 그것입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class SocketSet;

    /** @brief 임포트가 본 강체 메시 하나(본에 붙은 노드)입니다. */
    struct SocketImportNode
    {
        hashed_string _name{};
        hashed_string _parentBone{};
        hashed_string _kind{};
        string        _previewMesh{};
        float4x4      _localTransform{};
    };
} // namespace sw

namespace sw
{
    /** @brief 소켓 초안 도우미입니다(전부 static). */
    struct SW_API SocketImportUtil
    {
        /** @brief 노드마다 소켓 하나를 만듭니다(지금 내용을 비우고). 변환은 이동 · 회전 · 스케일로 나눠 적습니다. */
        static void createSocketSet( vector_reference<const SocketImportNode> listNode, SocketSet& outSockets );
        /**
         * @brief 파일이 없을 때만 XML 로 씁니다. 있으면 건드리지 않습니다(@p outWritten = false).
         * @return 쓰기를 시도했다가 실패하면 false 입니다. 파일이 이미 있으면 true 입니다.
         */
        [[nodiscard]] static bool writeIfMissing( string_view absolutePath, const SocketSet& sockets, bool& outWritten );
    };
} // namespace sw
