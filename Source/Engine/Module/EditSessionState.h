/**
 * @file Module/EditSessionState.h
 * @brief 에디터 인스턴스를 다시 만들 때(백엔드 교체 · 에디터 모듈 리로드) 새 인스턴스에 넘겨 줄 편집 세션 상태입니다 — 선택 · 미저장 표시.
 */
#pragma once
#include "Core/Foundation/EditorBuild.h"

#if SW_WITH_EDITOR == 0
    #error "EditSessionState(편집기 세션 상태) 는 편집기 빌드 전용이다 - 배포 빌드는 짓지 않는다"
#endif
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class EditSessionState
     * @brief 에디터 인스턴스가 내려갈 때 맡겨 두고 다음 인스턴스가 올라올 때 찾아가는 편집 세션 상태입니다. 엔진(`Engine`)이 소유해 모듈 리로드를 넘어 삽니다.
     * @details 되돌리기 스택(`CommandStack`)과 씬 오브젝트(같은 id 로 복원)는 이미 엔진 쪽에 있어 인스턴스를 넘는다. 선택과 미저장 표시는 에디터 인스턴스의
     *          워크스페이스에 있어 인스턴스와 함께 사라지므로 여기에 맡긴다. 유니티의 `SessionState`(도메인 리로드를 넘는 에디터 값)와 같은 자리다.
     *          맡긴 뒤 씬이 바뀌었으면(씬 세대가 다르면) 찾아가지 않는다 — 옛 씬의 선택과 표시는 새 씬에 뜻이 없다.
     */
    class SW_API EditSessionState final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditSessionState";
        SubsystemInitResult          initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                         shutdownSubsystem() override {}
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @struct Snapshot @brief 맡기는 상태입니다. */
        struct Snapshot
        {
            vector<uint64> _listSelectedObjectID{}; ///< 선택한 순서(첫째가 주 선택)
            vector<string> _listSelectedAsset{};    ///< 콘텐츠 브라우저에서 고른 에셋 경로
            uint64         _sceneGeneration{ 0 };   ///< 맡길 때의 씬 세대(`SceneManager::getSceneGeneration`)
            bool           _bSceneDirty{ false };   ///< 미저장 편집이 있었는지
        };

        /** @brief 빈 상태로 시작합니다. 생성자를 .cpp 에 두는 이유는 vtable 의 집입니다(`CommandStack` 과 같다). */
        EditSessionState();

        /** @brief 상태를 맡깁니다. 앞에 맡긴 것은 버립니다. */
        void store( Snapshot snapshot );
        /** @brief 맡긴 상태가 있으면(이 프로세스에서 앞선 에디터 인스턴스가 내려갔으면) true 입니다. */
        bool hasSnapshot() const { return _bStored; }

        /**
         * @brief 호스트가 에디터 인스턴스를 다시 만들되 모듈 라이브러리는 내리지 않는 동안(백엔드 교체) true 로 둡니다.
         * @details 그동안 내려가는 인스턴스는 자기 모듈 코드를 가진 되돌리기 명령(지형 붓질 · 문서 편집)을 스택에서 떼지 않는다 — 코드가 그대로 매핑돼 있어
         *          다음 인스턴스가 그 명령을 되돌릴 수 있다. 라이브러리를 내리는 리로드 · 종료에서는 false 이고, 인스턴스가 명령을 뗀다.
         */
        void setModuleLibraryKept( bool bKept ) { _bModuleLibraryKept = bKept; }
        bool isModuleLibraryKept() const { return _bModuleLibraryKept; }
        /**
         * @brief 맡긴 상태를 찾아갑니다. 맡긴 것이 없거나 씬 세대가 @p sceneGeneration 과 다르면 false 입니다. 찾아가든 못 하든 맡긴 것은 비웁니다.
         */
        [[nodiscard]] bool take( uint64 sceneGeneration, Snapshot& outSnapshot );

    private:
        Snapshot _snapshot;
        bool     _bStored;
        bool     _bModuleLibraryKept;
    };
} // namespace sw
