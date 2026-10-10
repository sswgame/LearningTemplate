/**
 * @file EditorRegistry.h
 * @brief 에디터 확장(패널 · 팝업 · 인스펙터 · 뷰포트 시각화)을 자기 파일에서 등록하는 공통 정적 등록부입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/RegistrationList.h"

#include "Editor/Common/EditorExports.h"

#include <type_traits>

namespace sw::editor
{
    /**
     * @struct EditorRegistration
     * @brief 등록부 한 줄의 공통 머리입니다. 종류별 줄(`EditorPanelRegistration` …)이 이것을 상속해 칸을 더합니다.
     * @details 문자열은 리터럴이어야 합니다. 등록부는 포인터만 들고 복사하지 않습니다.
     */
    struct EditorRegistration
    {
        const utf8* _pID;   ///< 종류 안에서 유일한 id. 같은 id 의 둘째 등록은 거절됩니다
        int32       _order; ///< 보이는 순서. 작을수록 앞이고, 같으면 id 사전순입니다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRegistrationList
     * @brief 한 종류의 등록 목록입니다. 늘 (순서, id) 로 정렬돼 있습니다.
     * @details 정적 초기화 순서는 번역 단위 사이에서 정해지지 않으므로 등록 순서가 아니라 `_order` 가 보이는 순서를 정합니다.
     *          목록 자체는 엔진 등록부와 같은 공통 모양(`RegistrationList` — 중복 거절 · 정렬 · id 찾기)이고, 여기서는 거절을 종류 이름과 함께 알립니다.
     */
    class SW_EDITOR_API EditorRegistrationList
    {
    public:
        explicit EditorRegistrationList( const utf8* pKindName );

        /** @brief 정렬된 자리에 넣습니다. id 가 비었거나 같은 id 가 이미 있으면 오류를 남기고 false 입니다(앞의 것을 둡니다). */
        [[nodiscard]] bool addRegistration( const EditorRegistration& registration );
        /** @brief 그 객체가 등록돼 있으면 뺍니다. 같은 id 의 다른 객체는 건드리지 않습니다. */
        void removeRegistration( const EditorRegistration& registration );

        /** @brief id 로 찾습니다. 없으면 nullptr 입니다. */
        const EditorRegistration*                findRegistration( string_view id ) const { return _registered.findByName( id ); }
        const vector<const EditorRegistration*>& getRegistrations() const { return _registered.getItems(); }
        /** @brief 이 목록의 종류 이름(`kKindName`)입니다. */
        const utf8* getKindName() const { return _pKindName; }
        /**
         * @brief 등록이 바뀔 때마다(넣기 · 빼기) 하나씩 오르는 세대입니다.
         * @details 인스턴스를 만들어 두는 매니저(패널 · 인스펙터 · 팝업 · 커맨드)는 자기가 맞춘 세대와 다르면 다시 맞춥니다. 확장 모듈은 에디터가 뜬 뒤에도
         *          로드되고 언로드되기 때문입니다(핫 리로드). 거절된 등록은 세대를 올리지 않습니다.
         */
        uint32 getGeneration() const { return _generation; }

    private:
        const utf8*                                _pKindName;
        RegistrationList<const EditorRegistration> _registered; ///< (순서, id) 사전순 · id 필수
        uint32                                     _generation;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 종류 이름 하나의 등록 목록을 돌려줍니다. 목록은 **EditorModule 이미지에 하나**이고, 확장 모듈의 등록자도 이 함수를 거쳐 같은 목록에 오릅니다.
     * @details 템플릿 안의 함수 정적으로 두면 DLL 마다 사본이 생겨 확장의 등록이 EditorModule 에 보이지 않습니다. 그래서 목록은 내보낸 함수 하나가 보관합니다.
     *          목록은 EditorModule 이 언로드될 때 사라집니다. 확장 모듈은 EditorModule 에 의존하므로 늘 먼저 언로드됩니다(리로드 그래프와 종료 순서).
     */
    SW_EDITOR_API EditorRegistrationList& getEditorRegistrationList( const utf8* pKindName );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRegistry
     * @brief 등록 줄 타입 하나의 등록부입니다. `TRegistration` 은 `EditorRegistration` 을 상속하고 `kKindName` 을 둡니다.
     * @details 목록은 EditorModule 이 보관합니다(`getEditorRegistrationList`). 확장 모듈(DLL)의 등록도 같은 목록에 오르고,
     *          EditorModule 이 핫 리로드로 바뀌면 등록자와 함께 새로 만들어집니다.
     */
    template <typename TRegistration>
    class EditorRegistry
    {
        static_assert( std::is_base_of_v<EditorRegistration, TRegistration>, "등록 줄은 EditorRegistration 을 상속해야 합니다" );

    public:
        /** @brief 이 종류의 목록입니다(EditorModule 의 것). 이 DLL 이 처음 부를 때 한 번 찾아 둡니다. */
        static EditorRegistrationList& getList()
        {
            static EditorRegistrationList& s_list = getEditorRegistrationList( TRegistration::kKindName );
            return s_list;
        }

        /** @brief 등록 수입니다. */
        static uint32 getCount() { return static_cast<uint32>( getList().getRegistrations().size() ); }
        /** @brief 순서대로 index 번째 줄입니다. */
        static const TRegistration& getAt( uint32 index ) { return static_cast<const TRegistration&>( *getList().getRegistrations()[index] ); }
        /** @brief id 로 찾습니다. 없으면 nullptr 입니다. */
        static const TRegistration* find( string_view id )
        {
            return static_cast<const TRegistration*>( getList().findRegistration( id ) );
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRegistrar
     * @brief 확장의 코드 파일에 정적 객체로 두면 줄을 등록하고, 모듈이 내려갈 때 뺍니다.
     * @details EditorModule 과 확장 모듈은 DLL 이라 소스의 목적 파일이 모두 링크됩니다. 아무도 참조하지 않는 등록자도 버려지지 않습니다.
     *          직접 쓰지 말고 `SW_EDITOR_PANEL` 같은 종류별 매크로를 씁니다.
     */
    template <typename TRegistration>
    class EditorRegistrar
    {
    public:
        explicit EditorRegistrar( const TRegistration& registration )
            : _registration{ registration }
            , _bRegistered{ EditorRegistry<TRegistration>::getList().addRegistration( _registration ) }
        {
        }
        ~EditorRegistrar() { EditorRegistry<TRegistration>::getList().removeRegistration( _registration ); }

        EditorRegistrar( const EditorRegistrar& )            = delete;
        EditorRegistrar& operator=( const EditorRegistrar& ) = delete;

        const TRegistration& getRegistration() const { return _registration; }
        /** @brief 등록이 받아들여졌으면 true 입니다(같은 id 가 먼저 있었으면 false). */
        bool isRegistered() const { return _bRegistered; }

    private:
        TRegistration _registration;
        bool          _bRegistered;
    };
} // namespace sw::editor

/**
 * @brief 등록 줄 하나를 이 파일의 정적 등록자로 둡니다. `name` 은 파일 안에서 유일한 식별자 조각입니다(보통 타입 이름).
 * @details 유니티 빌드가 여러 .cpp 를 한 번역 단위로 합치므로 변수 이름에 `name` 을 붙여 겹치지 않게 합니다.
 */
#define SW_EDITOR_REGISTER( TRegistration, name, ... )                                  \
    static const ::sw::editor::EditorRegistrar<TRegistration> sw_editorRegistrar_##name \
    {                                                                                   \
        TRegistration { __VA_ARGS__ }                                                   \
    }
