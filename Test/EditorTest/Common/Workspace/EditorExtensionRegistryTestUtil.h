#pragma once
#include "Core/Container/vector.h"

#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    /**
     * @class ScopedEmptyEditorExtensionRegistry
     * @brief 범위 동안 한 종류의 레지스트리에서 이미 오른 등록을 내려 두고, 범위가 닫히면 다시 올립니다.
     * @details 테스트 실행 파일은 EditorCore 오브젝트를 통째로 링크해 코어 파일의 정적 등록(프레임 단계 · 서브시스템)이 이미 올라 있다. 빈 목록을
     *          가정하는 테스트가 무엇이 링크됐는지에 기대지 않게 한다. 내려 둔 등록은 프로세스 수명의 정적 객체라 다시 올려도 안전하다.
     */
    template <typename TRegistration>
    class ScopedEmptyEditorExtensionRegistry
    {
    public:
        ScopedEmptyEditorExtensionRegistry()
            : _listSaved{ EditorExtensionRegistry<TRegistration>::getList().getRegistrations() }
        {
            for ( const EditorExtensionRegistration* pRegistration : _listSaved )
            {
                EditorExtensionRegistry<TRegistration>::getList().removeRegistration( *pRegistration );
            }
        }
        ~ScopedEmptyEditorExtensionRegistry()
        {
            for ( const EditorExtensionRegistration* pRegistration : _listSaved )
            {
                (void)EditorExtensionRegistry<TRegistration>::getList().addRegistration( *pRegistration ); // 내려 두기 전에 받아들여진 등록이라 거절되지 않는다
            }
        }

        ScopedEmptyEditorExtensionRegistry( const ScopedEmptyEditorExtensionRegistry& )            = delete;
        ScopedEmptyEditorExtensionRegistry& operator=( const ScopedEmptyEditorExtensionRegistry& ) = delete;

    private:
        vector<const EditorExtensionRegistration*> _listSaved;
    };
} // namespace sw::editor
