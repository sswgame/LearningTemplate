/**
 * @file EditorExtensionRegistry.h
 * @brief 에디터 확장(패널, 팝업, 인스펙터, 뷰포트 시각화)을 각자의 파일에서 등록하는 공통 정적 레지스트리입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Module/ModuleObjectRegistrar.h"

#include <type_traits>

namespace sw::editor
{
    /**
     * @struct EditorExtensionRegistration
     * @brief 레지스트리 항목의 공통 헤더입니다. 종류별 항목(`EditorPanelRegistration` 등)이 이것을 상속해 필드를 추가합니다.
     * @details 문자열은 리터럴이어야 한다. 레지스트리는 포인터만 보관하고 복사하지 않는다.
     */
    struct EditorExtensionRegistration
    {
        /** @brief 모듈을 내릴 때 떼는 차례(`ModuleObjectRegistrar::getDetachPass`)입니다. 다른 등록의 인스턴스가 쓰는 종류만 키운다. */
        static constexpr uint32 kDetachPass = 0;
        /** @brief 떼기 직전에 불립니다(`EditorExtensionRegistrar::detach`). 기본은 할 일이 없다 — 종류가 같은 이름으로 가린다(환경설정은 저장한다). */
        static void onDetaching( const EditorExtensionRegistration& registration ) { (void)registration; }
        /** @brief 목록에 붙은 직후(`attach`) 부릅니다. 종류가 같은 이름의 정적 함수로 덮는다(붙는 순간 할 일 — 예: 결속기는 이미 있는 UI 컨텍스트를 건다). */
        static void onAttached( const EditorExtensionRegistration& registration ) { (void)registration; }

        const utf8* _pID;   ///< 종류 안에서 유일한 id. 같은 id 의 두 번째 등록은 거절된다
        int32       _order; ///< 표시 순서. 작을수록 앞이고, 같으면 id 사전순이다

        /** @brief @p registration 을 종류 목록 @p list 에 (순서, id) 자리로 올립니다. id 가 비었거나 같은 id 가 이미 있으면 종류 이름으로 거절을 남기고 false 입니다(앞의 것을 둔다). */
        [[nodiscard]] SW_EDITOR_API static bool addTo( RegistrationList<const EditorExtensionRegistration>& list, const EditorExtensionRegistration& registration );
        /** @brief 그 객체가 올라 있으면 뺍니다. 같은 id 의 다른 객체는 건드리지 않습니다. 목록의 세대가 올라 인스턴스를 만든 쪽이 다음 맞추기에서 거둔다. */
        SW_EDITOR_API static void removeFrom( RegistrationList<const EditorExtensionRegistration>& list, const EditorExtensionRegistration& registration );
    };

    /**
     * @brief 한 종류의 등록 목록입니다. 공용 `RegistrationList` 그대로 — (순서, id) 사전순, id 필수, 중복 거절, 리로드 대체, 세대 번호(`getGeneration`).
     * @details 정적 초기화 순서는 번역 단위 사이에서 정해지지 않으므로 등록 순서가 아니라 `_order` 가 표시 순서를 정한다. 인스턴스를 만들어 두는 매니저(패널 · 인스펙터 ·
     *          팝업 · 커맨드)는 자기가 맞춘 세대와 다르면 다시 맞춘다 — 빠짐을 알리는 전역 신호는 없다. 모듈을 내릴 때는 떼기 차례마다
     *          `TypeRegistry::subscribeModuleObjectsDetached` 가 맞출 때를 알린다(그 모듈의 코드가 살아 있을 때).
     */
    using EditorExtensionRegistrationList = RegistrationList<const EditorExtensionRegistration>;
} // namespace sw::editor

/**
 * @brief 등록 줄 종류 구조체 안에 둡니다 — 종류 이름 `kKindName` 과 그 종류의 목록을 돌려주는 내보낸 함수 `getRegistrationList` 를 선언한다.
 * @details 목록은 EditorModule 이미지에 하나다. 템플릿 안 함수 정적이면 DLL 마다 사본이 생겨 확장의 등록이 EditorModule 에 보이지 않으므로, 정의는 종류의 .cpp(EditorModule)
 *          에서 `SW_DEFINE_EDITOR_EXTENSION_KIND` 로 한다. 목록은 EditorModule 을 언로드할 때 사라지고, 확장 모듈은 EditorModule 에 의존하므로 늘 먼저 내려간다.
 */
#define SW_EDITOR_EXTENSION_KIND( pKindName )                                                  \
    static constexpr const utf8*                                        kKindName = pKindName; \
    SW_EDITOR_API static ::sw::editor::EditorExtensionRegistrationList& getRegistrationList()

/** @brief 종류 @p TRegistration 의 등록 목록을 정의합니다(그 종류의 .cpp, `namespace sw::editor` 안 — 정적 등록 목록 공통 틀 `SW_DEFINE_REGISTRATION_LIST`). */
#define SW_DEFINE_EDITOR_EXTENSION_KIND( TRegistration ) SW_DEFINE_REGISTRATION_LIST( TRegistration::getRegistrationList, const ::sw::editor::EditorExtensionRegistration, TRegistration::kKindName, ::sw::RegistrationOrder::ByOrderThenName, true )

namespace sw::editor
{
    /**
     * @class EditorExtensionRegistry
     * @brief 등록 항목 타입 하나의 레지스트리입니다. `TRegistration` 은 `EditorExtensionRegistration` 을 상속하고 `kKindName` 을 가집니다.
     * @details 목록은 EditorModule 이 보관한다(`SW_EDITOR_EXTENSION_KIND`). 확장 모듈(DLL)의 등록도 같은 목록에 오르고,
     *          EditorModule 을 핫 리로드하면 등록자와 함께 새로 만들어진다.
     */
    template <typename TRegistration>
    class EditorExtensionRegistry
    {
        static_assert( std::is_base_of_v<EditorExtensionRegistration, TRegistration>, "등록 줄은 EditorExtensionRegistration 을 상속해야 합니다" );

    public:
        /** @brief 이 종류의 목록입니다. EditorModule 의 것이다(`SW_EDITOR_EXTENSION_KIND`). */
        static EditorExtensionRegistrationList& getList() { return TRegistration::getRegistrationList(); }

        /** @brief 등록 수를 반환합니다. */
        static uint32 getCount() { return getList().getCount(); }
        /** @brief 순서대로 index 번째 항목을 반환합니다. */
        static const TRegistration& getAt( uint32 index ) { return static_cast<const TRegistration&>( *getList().getAt( index ) ); }
        /** @brief id 로 찾습니다. 없으면 nullptr 입니다. */
        static const TRegistration* find( string_view id )
        {
            return static_cast<const TRegistration*>( getList().findByName( id ) );
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorExtensionRegistrar
     * @brief 확장의 코드 파일에 정적 객체로 두면, 그 모듈을 올릴 때 항목을 등록하고(`attach`) 내리기 전에 뺍니다(`detach`).
     * @details 만들 때는 링크 때 목록에만 달린다(`ModuleObjectRegistrar`) — 핫 리로드가 새 이미지를 먼저 올려도 옛 이미지의 같은 id 와 부딪히지 않는다.
     *          빼면 목록의 세대가 오르고, 떼기 차례가 끝날 때(`TypeRegistry::subscribeModuleObjectsDetached`) 에디터가 그 줄로 만든 인스턴스를 거둔다 — 코드 주소로 고르지 않는다.
     *          EditorModule 과 확장 모듈은 DLL 이므로 소스의 오브젝트 파일이 모두 링크된다. 아무도 참조하지 않는 등록자도 버려지지 않는다.
     *          직접 쓰지 말고 `SW_EDITOR_PANEL` 같은 종류별 매크로를 쓴다. 붙은 뒤 `TRegistration::onAttached`, 빼기 전에 `TRegistration::onDetaching` 을 부른다.
     */
    template <typename TRegistration>
    class EditorExtensionRegistrar : public ModuleObjectRegistrar
    {
    public:
        explicit EditorExtensionRegistrar( const TRegistration& registration )
            : _registration{ registration }
            , _bRegistered{ SW_FALSE }
        {
            (void)EditorExtensionRegistry<TRegistration>::getList(); // 목록을 먼저 세운다 — 같은 이미지의 목록이 이 등록자보다 먼저 사라지지 않게(정적 소멸은 역순)
        }
        /** @brief 모듈 경로 밖에서 붙은 채 사라지면(프로세스 끝) 뺍니다. */
        ~EditorExtensionRegistrar()
        {
            if ( _bRegistered )
                EditorExtensionRegistration::removeFrom( EditorExtensionRegistry<TRegistration>::getList(), _registration );
        }

        EditorExtensionRegistrar( const EditorExtensionRegistrar& )            = delete;
        EditorExtensionRegistrar& operator=( const EditorExtensionRegistrar& ) = delete;

        void attach( const ISubsystemLocator& /*scope*/ ) override
        {
            _bRegistered = EditorExtensionRegistration::addTo( EditorExtensionRegistry<TRegistration>::getList(), _registration );
            if ( _bRegistered )
                TRegistration::onAttached( _registration );
        }
        void detach( const ISubsystemLocator& /*scope*/ ) override
        {
            if ( _bRegistered == false )
                return;
            TRegistration::onDetaching( _registration );
            _bRegistered = SW_FALSE;
            EditorExtensionRegistration::removeFrom( EditorExtensionRegistry<TRegistration>::getList(), _registration );
        }
        uint32 getDetachPass() const override { return TRegistration::kDetachPass; }

        const TRegistration& getRegistration() const { return _registration; }
        /** @brief 등록이 받아들여져 올라 있으면 true 입니다. 같은 id 가 먼저 있었거나 아직 붙지 않았으면 false 입니다. */
        bool isRegistered() const { return _bRegistered; }

    private:
        TRegistration _registration;
        uint8         _bRegistered : 1;
    };
} // namespace sw::editor

/**
 * @brief 등록 항목 하나를 이 파일의 정적 등록자로 둡니다. `name` 은 파일 안에서 유일한 식별자 조각이며 보통 타입 이름입니다.
 * @details 유니티 빌드가 여러 .cpp 를 한 번역 단위로 합치므로 변수 이름에 `name` 을 붙여 겹치지 않게 합니다.
 */
#define SW_EDITOR_REGISTER( TRegistration, name, ... )                                     \
    static ::sw::editor::EditorExtensionRegistrar<TRegistration> sw_editorRegistrar_##name \
    {                                                                                      \
        TRegistration                                                                      \
        {                                                                                  \
            __VA_ARGS__                                                                    \
        }                                                                                  \
    }
