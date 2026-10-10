/**
 * @file InspectorComponentManager.h
 * @brief 컴포넌트 타입명 → IInspectorComponent (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class IInspectorComponent;

    /**
     * @struct EditorInspectorRegistration
     * @brief 컴포넌트 인스펙터 확장 하나의 등록 줄입니다. 확장의 .cpp 가 `SW_EDITOR_INSPECTOR` 로 둡니다.
     * @details id 는 컴포넌트 클래스 이름입니다. 컴포넌트 하나에 확장은 하나이고, 둘째 등록은 거절됩니다. 그리는 순서는 등록 순서가
     *          아니라 타입 계층(기반 → 파생)이 정하므로 `_order` 는 쓰지 않습니다.
     */
    struct EditorInspectorRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "inspector";

        const TypeInfo* ( *_pGetComponentType )();
        unique_ptr<IInspectorComponent> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 줄이 가리키는 확장 생성 함수입니다. */
    template <typename TInspector>
    unique_ptr<IInspectorComponent> createInspectorComponent()
    {
        return make_unique<TInspector>();
    }

    /** @brief 컴포넌트 타입 하나에 건 인스펙터 확장입니다. */
    struct InspectorComponentEntry
    {
        hashed_string                      _typeName;
        unique_ptr<IInspectorComponent>    _pInstance;
        const EditorInspectorRegistration* _pRegistration{ nullptr }; ///< 이 인스턴스를 만든 등록 줄(직접 registerType 한 것은 nullptr)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 컴포넌트 타입별 인스펙터 UI 관리자 (EditorContext 소유) */
    class InspectorComponentManager
    {
    public:
        InspectorComponentManager();
        ~InspectorComponentManager();
        InspectorComponentManager( const InspectorComponentManager& )            = delete;
        InspectorComponentManager& operator=( const InspectorComponentManager& ) = delete;

        /** @brief 타입 이름에 확장을 겁니다. 같은 이름이 있으면 바꿉니다. */
        void                 registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
        IInspectorComponent* find( string_view typeName ) const;
        /**
         * @brief 이 타입과 그 기반들에 등록된 확장을 기반 → 파생 순서로 모읍니다.
         * @details 언리얼 `IDetailCustomization` 처럼 하위 타입에도 걸립니다. 게임이 만든 SceneComponent 파생에도 트랜스폼 칸이 그려집니다.
         */
        void collectForType( const TypeInfo& type, vector<IInspectorComponent*>& outListInspector ) const;
        /** @brief `SW_EDITOR_INSPECTOR` 로 등록된 확장을 모두 만들어 둡니다. */
        void registerDefaults();
        /** @brief 등록 목록과 맞춥니다. 새 줄은 만들고 사라진 줄의 확장은 지웁니다. 세대가 같으면 바로 돌아갑니다. */
        void syncWithRegistry();
        /** @brief 등록 줄이 [@p pBegin, @p pEnd)(모듈 이미지) 안인 확장을 지웁니다. 지운 수를 돌려줍니다. 이미지를 언로드하기 전에 부릅니다. */
        uint32 releaseInspectorsWithin( const void* pBegin, const void* pEnd );

    private:
        vector<InspectorComponentEntry> _listEntry;        ///< 확장은 열 개 안팎이라 줄 찾기로 충분하다
        uint32                          _syncedGeneration; ///< 마지막으로 맞춘 등록 세대

        /** @brief 아직 등록 목록과 맞춘 적이 없다는 표시입니다. */
        static constexpr uint32 kNotSynced = invalid_index::kUint32;
    };
} // namespace sw::editor

/**
 * @brief 컴포넌트 인스펙터 확장을 그 확장의 .cpp 에서 등록합니다. 예: `SW_EDITOR_INSPECTOR( CameraComponent, CameraComponentInspector );`
 * @param TComponent 리플렉션 타입(`StaticType()`)이 있는 컴포넌트
 * @param TInspector 기본 생성자가 있는 `IInspectorComponent` 구현
 */
#define SW_EDITOR_INSPECTOR( TComponent, TInspector )                                                          \
    SW_EDITOR_REGISTER( ::sw::editor::EditorInspectorRegistration, Inspector_##TInspector, { #TComponent, 0 }, \
                        &TComponent::StaticType, &::sw::editor::createInspectorComponent<TInspector> )
