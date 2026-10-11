/**
 * @file EditorRegistry.h
 * @brief 에디터 확장(패널, 팝업, 인스펙터, 뷰포트 시각화)을 각자의 파일에서 등록하는 공통 정적 레지스트리입니다.
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
     * @brief 레지스트리 항목의 공통 헤더입니다. 종류별 항목(`EditorPanelRegistration` 등)이 이것을 상속해 필드를 추가합니다.
     * @details 문자열은 리터럴이어야 한다. 레지스트리는 포인터만 보관하고 복사하지 않는다.
     */
    struct EditorRegistration
    {
        const utf8* _pID;   ///< 종류 안에서 유일한 id. 같은 id 의 두 번째 등록은 거절된다
        int32       _order; ///< 표시 순서. 작을수록 앞이고, 같으면 id 사전순이다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRegistrationList
     * @brief 한 종류의 등록 목록입니다. 항상 (순서, id) 로 정렬되어 있습니다.
     * @details 정적 초기화 순서는 번역 단위 사이에서 정해지지 않으므로, 등록 순서가 아니라 `_order` 가 표시 순서를 정한다.
     *          목록 자체는 엔진 레지스트리와 같은 공통 구조(`RegistrationList`)이며 중복 거절, 정렬, id 찾기를 제공한다. 여기서는 거절을 종류 이름과 함께 알린다.
     */
    class SW_EDITOR_API EditorRegistrationList
    {
    public:
        explicit EditorRegistrationList( const utf8* pKindName );

        /** @brief 정렬된 위치에 넣습니다. id 가 비었거나 같은 id 가 이미 있으면 오류를 남기고 false 를 반환하며, 앞의 것을 유지합니다. */
        [[nodiscard]] bool addRegistration( const EditorRegistration& registration );
        /** @brief 그 객체가 등록되어 있으면 뺍니다. 같은 id 의 다른 객체는 건드리지 않습니다. */
        void removeRegistration( const EditorRegistration& registration );

        /** @brief id 로 찾습니다. 없으면 nullptr 입니다. */
        const EditorRegistration*                findRegistration( string_view id ) const { return _registered.findByName( id ); }
        const vector<const EditorRegistration*>& getRegistrations() const { return _registered.getItems(); }
        /** @brief 이 목록의 종류 이름(`kKindName`)을 반환합니다. */
        const utf8* getKindName() const { return _pKindName; }
        /**
         * @brief 등록이 바뀔 때마다(넣기, 빼기) 하나씩 올라가는 세대입니다.
         * @details 인스턴스를 만들어 두는 매니저(패널, 인스펙터, 팝업, 커맨드)는 자기가 맞춘 세대와 다르면 다시 맞춘다. 확장 모듈은 에디터가 뜬 뒤에도
         *          로드되고 언로드되기 때문이다(핫 리로드). 거절된 등록은 세대를 올리지 않는다.
         */
        uint32 getGeneration() const { return _generation; }

    private:
        const utf8*                                _pKindName;
        RegistrationList<const EditorRegistration> _registered; ///< (순서, id) 사전순이며 id 는 필수다
        uint32                                     _generation;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 종류 이름 하나의 등록 목록을 반환합니다. 목록은 EditorModule 이미지에 하나만 있고, 확장 모듈의 등록자도 이 함수를 거쳐 같은 목록에 등록됩니다.
     * @details 템플릿 안의 함수 정적 변수로 두면 DLL 마다 복사본이 생겨 확장의 등록이 EditorModule 에 보이지 않는다. 그래서 목록은 export 한 함수 하나가 보관한다.
     *          목록은 EditorModule 을 언로드할 때 사라진다. 확장 모듈은 EditorModule 에 의존하므로 항상 먼저 언로드된다(리로드 그래프와 종료 순서).
     */
    SW_EDITOR_API EditorRegistrationList& getEditorRegistrationList( const utf8* pKindName );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRegistry
     * @brief 등록 항목 타입 하나의 레지스트리입니다. `TRegistration` 은 `EditorRegistration` 을 상속하고 `kKindName` 을 가집니다.
     * @details 목록은 EditorModule 이 보관한다(`getEditorRegistrationList`). 확장 모듈(DLL)의 등록도 같은 목록에 오르고,
     *          EditorModule 을 핫 리로드하면 등록자와 함께 새로 만들어진다.
     */
    template <typename TRegistration>
    class EditorRegistry
    {
        static_assert( std::is_base_of_v<EditorRegistration, TRegistration>, "등록 줄은 EditorRegistration 을 상속해야 합니다" );

    public:
        /** @brief 이 종류의 목록입니다. EditorModule 의 것이며, 이 DLL 이 처음 호출할 때 한 번 찾아 둡니다. */
        static EditorRegistrationList& getList()
        {
            static EditorRegistrationList& s_list = getEditorRegistrationList( TRegistration::kKindName );
            return s_list;
        }

        /** @brief 등록 수를 반환합니다. */
        static uint32 getCount() { return static_cast<uint32>( getList().getRegistrations().size() ); }
        /** @brief 순서대로 index 번째 항목을 반환합니다. */
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
     * @brief 확장의 코드 파일에 정적 객체로 두면 항목을 등록하고, 모듈을 언로드할 때 뺍니다.
     * @details EditorModule 과 확장 모듈은 DLL 이므로 소스의 오브젝트 파일이 모두 링크된다. 아무도 참조하지 않는 등록자도 버려지지 않는다.
     *          직접 쓰지 말고 `SW_EDITOR_PANEL` 같은 종류별 매크로를 쓴다.
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
        /** @brief 등록이 받아들여졌으면 true 입니다. 같은 id 가 먼저 있었으면 false 입니다. */
        bool isRegistered() const { return _bRegistered; }

    private:
        TRegistration _registration;
        bool          _bRegistered;
    };
} // namespace sw::editor

/**
 * @brief 등록 항목 하나를 이 파일의 정적 등록자로 둡니다. `name` 은 파일 안에서 유일한 식별자 조각이며 보통 타입 이름입니다.
 * @details 유니티 빌드가 여러 .cpp 를 한 번역 단위로 합치므로 변수 이름에 `name` 을 붙여 겹치지 않게 합니다.
 */
#define SW_EDITOR_REGISTER( TRegistration, name, ... )                                  \
    static const ::sw::editor::EditorRegistrar<TRegistration> sw_editorRegistrar_##name \
    {                                                                                   \
        TRegistration { __VA_ARGS__ }                                                   \
    }
