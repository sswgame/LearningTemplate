/**
 * @file Editor/Common/Workspace/ObjectCopyBuffer.h
 * @brief 복사한 GameObject 의 직렬화 상태를 담는 버퍼입니다(복사 · 붙여 넣기).
 * @details 편집기 범위의 서브시스템이고, 인스턴스가 내려갈 때 호스트의 맡김 칸(`IModuleStateStore`)에 맡겨 리로드 · 백엔드 교체 뒤에도 복사한 내용이 남는다.
 *          언리얼은 액터 복사를 텍스트로 내보내 OS 클립보드에 두고(`edactCopySelected` → `FPlatformApplicationMisc::ClipboardCopy`),
 *          유니티는 에디터 프로세스의 내부 붙여 넣기 버퍼에 둔다. 여기는 후자다 — 바이너리 상태라 OS 클립보드에 올리지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

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

namespace sw::editor
{
    /**
     * @class ObjectCopyBuffer
     * @brief 오브젝트 복사 버퍼입니다(편집기 범위). 게임 스레드에서만 씁니다.
     */
    class SW_EDITOR_API ObjectCopyBuffer final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ObjectCopyBuffer";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 앞선 인스턴스가 맡긴 버퍼를 되찾습니다. */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 다음 인스턴스를 위해 버퍼를 맡깁니다. */
        void shutdownSubsystem() override;

        /** @brief 버퍼를 @p listEntry 로 바꿉니다. */
        void setEntries( vector<ObjectCopyEntry>&& listEntry ) { _listEntry = std::move( listEntry ); }
        /** @brief 지금 담긴 오브젝트들입니다. 비어 있으면 복사한 것이 없습니다. */
        [[nodiscard]] const vector<ObjectCopyEntry>& getEntries() const { return _listEntry; }
        /** @brief 버퍼를 비웁니다. */
        void clear() { _listEntry.clear(); }

    private:
        vector<ObjectCopyEntry> _listEntry;
    };
} // namespace sw::editor
