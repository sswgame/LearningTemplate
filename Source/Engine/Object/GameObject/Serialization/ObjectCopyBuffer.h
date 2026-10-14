/**
 * @file ObjectCopyBuffer.h
 * @brief 복사한 GameObject 의 직렬화 상태를 담는 프로세스 범위 버퍼입니다(복사 · 붙여 넣기).
 * @details 버퍼는 엔진 수명 범위(`ScopeOwned`)가 가진다. 복사를 하는 쪽(에디터 모듈)이 핫 리로드로 내려갔다 올라와도 복사한 내용이 남고, 엔진 종료에서 소멸한다.
 *          언리얼은 액터 복사를 텍스트로 내보내 OS 클립보드에 두고(`edactCopySelected` → `FPlatformApplicationMisc::ClipboardCopy`),
 *          유니티는 에디터 프로세스의 내부 붙여 넣기 버퍼에 둔다. 여기는 후자다 — 바이너리 상태라 OS 클립보드에 올리지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @struct ObjectCopyEntry
     * @brief 복사한 오브젝트 하나입니다. 서브트리는 부모부터 차례로 담긴다.
     */
    struct ObjectCopyEntry
    {
        hashed_string _name;
        uint64        _savedID{ 0 };   ///< 복사한 때의 오브젝트 id(붙여 넣을 때 사본끼리 잇는 열쇠)
        vector<uint8> _stateBytes;     ///< `ObjectStateSerializer::saveToBinaryBuffer` 결과
        string        _prefabPath;     ///< 프리팹 인스턴스였으면 그 경로
        bool          _bRoot{ false }; ///< 복사한 루트(붙여 넣을 때 씬의 루트로 둔다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ObjectCopyBuffer
     * @brief 프로세스에 하나인 오브젝트 복사 버퍼입니다. 게임 스레드에서만 씁니다.
     */
    class SW_API ObjectCopyBuffer final
    {
    public:
        ObjectCopyBuffer() = delete;

        /** @brief 버퍼를 @p listEntry 로 바꿉니다. */
        static void setEntries( vector<ObjectCopyEntry>&& listEntry );
        /** @brief 지금 담긴 오브젝트들입니다. 비어 있으면 복사한 것이 없습니다. */
        [[nodiscard]] static const vector<ObjectCopyEntry>& getEntries();
        /** @brief 버퍼를 비웁니다. */
        static void clear();
    };
} // namespace sw
