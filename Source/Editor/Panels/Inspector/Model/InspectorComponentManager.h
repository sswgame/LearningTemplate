/**
 * @file InspectorComponentManager.h
 * @brief 컴포넌트 타입 이름에서 IInspectorComponent 로의 대응을 관리합니다. `EditorContext` 가 소유합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class IInspectorComponent;

    /**
     * @struct EditorInspectorRegistration
     * @brief 컴포넌트 인스펙터 확장 하나의 등록 항목입니다. 확장의 .cpp 가 `SW_EDITOR_INSPECTOR` 로 둡니다.
     * @details id 는 컴포넌트 클래스 이름이다. 컴포넌트 하나에 확장은 하나이고, 두 번째 등록은 거절된다. 그리는 순서는 등록 순서가
     *          아니라 타입 계층(기반에서 파생 순서)이 정하므로 `_order` 는 쓰지 않는다.
     */
    struct EditorInspectorRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "inspector";

        const TypeInfo* ( *_pGetComponentType )();
        unique_ptr<IInspectorComponent> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 항목이 가리키는 확장 생성 함수입니다. */
    template <typename TInspector>
    unique_ptr<IInspectorComponent> createInspectorComponent()
    {
        return make_unique<TInspector>();
    }

    /** @brief 컴포넌트 타입 하나에 연결한 인스펙터 확장입니다. */
    struct InspectorComponentEntry
    {
        hashed_string                      _typeName;
        unique_ptr<IInspectorComponent>    _pInstance;
        const EditorInspectorRegistration* _pRegistration{ nullptr }; ///< 이 인스턴스를 만든 등록 항목이다. 직접 registerType 한 것은 nullptr 이다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 컴포넌트 타입별 인스펙터 UI 를 관리합니다. `EditorContext` 가 소유합니다. */
    class InspectorComponentManager : public EditorSubsystem
    {
    public:
        InspectorComponentManager();
        ~InspectorComponentManager() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        SubsystemInitResult initializeSubsystem() override
        {
            registerDefaults();
            return SubsystemInitResult::Succeeded;
        }
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "InspectorComponentManager";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        InspectorComponentManager( const InspectorComponentManager& )            = delete;
        InspectorComponentManager& operator=( const InspectorComponentManager& ) = delete;

        /** @brief 타입 이름에 확장을 등록합니다. 같은 이름이 있으면 교체합니다. */
        void                 registerType( string_view typeName, unique_ptr<IInspectorComponent> pInspector );
        IInspectorComponent* find( string_view typeName ) const;
        /**
         * @brief 이 타입과 그 기반 타입들에 등록된 확장을 기반에서 파생 순서로 모읍니다.
         * @details 언리얼의 `IDetailCustomization` 처럼 하위 타입에도 적용된다. 게임이 만든 SceneComponent 파생에도 트랜스폼 항목이 그려진다.
         *          타입마다 한 번 모아 두고 확장 목록이 바뀌거나(`resetTypeCache`) 타입 레지스트리 세대가 바뀔 때만 다시 모은다 — 인스펙터가 매 프레임 컴포넌트마다 부른다.
         *          돌려준 목록은 다음 확장 목록 변경(프레임 앞의 `syncWithRegistry`, 모듈 언로드)까지 유효하다.
         */
        const vector<IInspectorComponent*>& getInspectorsForType( const TypeInfo& type );
        /** @brief `SW_EDITOR_INSPECTOR` 로 등록된 확장을 모두 만들어 둡니다. */
        void registerDefaults();
        /** @brief 등록 목록과 맞춥니다. 새 항목은 만들고 사라진 항목의 확장은 지웁니다. 세대가 같으면 바로 반환합니다. */
        void syncWithRegistry();

    private:
        /** @brief 타입별 확장 목록을 버리고 그 기준 세대를 @p typeGeneration 으로 둡니다. `_listEntry` 를 바꾸는 곳은 모두 바꾸기 전에 부른다. */
        void resetTypeCache( uint32 typeGeneration );

        vector<InspectorComponentEntry>                              _listEntry;           ///< 확장은 열 개 안팎이므로 선형 탐색으로 충분하다
        unordered_map<const TypeInfo*, vector<IInspectorComponent*>> _mapTypeInspector;    ///< 타입 → 기반에서 파생 순서의 확장(`getInspectorsForType`)
        uint32                                                       _syncedGeneration;    ///< 마지막으로 맞춘 등록 세대
        uint32                                                       _cacheTypeGeneration; ///< `_mapTypeInspector` 를 모은 타입 레지스트리 세대

        /** @brief 아직 등록 목록과 맞춘 적이 없다는 표시입니다. */
        static constexpr uint32 kNotSynced = invalid_index::kUint32;
    };
} // namespace sw::editor

/**
 * @brief 컴포넌트 인스펙터 확장을 그 확장의 .cpp 에서 등록합니다. 예: `SW_EDITOR_INSPECTOR( CameraComponent, CameraComponentInspector );`
 * @param TComponent 리플렉션 타입(`StaticType()`)이 있는 컴포넌트입니다.
 * @param TInspector 기본 생성자가 있는 `IInspectorComponent` 구현입니다.
 */
#define SW_EDITOR_INSPECTOR( TComponent, TInspector )                                                          \
    SW_EDITOR_REGISTER( ::sw::editor::EditorInspectorRegistration, Inspector_##TInspector, { #TComponent, 0 }, \
                        &TComponent::StaticType, &::sw::editor::createInspectorComponent<TInspector> )
