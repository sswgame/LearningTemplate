/**
 * @file RegistrationList.h
 * @brief 레지스트리의 공통 구조 둘입니다. `RegistrationList` 는 소유하지 않는 포인터 목록(중복 거절 · 정렬 · 이름 찾기 · 훑기 · 이름 사본),
 *        `NameRegistry` 는 이름 → 값을 소유하는 테이블(중복 거절 또는 덮어쓰기 · 이름 찾기 · 등록 순서 훑기 · 조건 제거)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @struct RegistrationReloadScope
     * @brief 핫 리로드가 **교체 이미지**를 올리는 동안 켜 두는 플래그입니다(`LiveReloadManager` 가 새 이미지를 올리는 동안 잡는다). 켜진 동안 `RegistrationList::add` 는
     *        같은 이름의 등록을 거절하지 않고 **대체**합니다.
     * @details 새 이미지의 정적 등록자는 옛 이미지가 아직 도는 동안 돈다. 옛 등록이 이름을 쥐고 있어 새 등록이 거절되면, 옛 이미지가 내려간 뒤 그 이름이 아무것도 가리키지 않는다
     *          (리로드한 개발 명령 · 탐침 · 시나리오 단계가 사라진다). 대체된 옛 등록은 목록이 그림자로 보관한다 — 옛 이미지가 내려가며 빼면 버리고, 새 이미지가 취소되면 되살린다.
     *          플래그는 프로세스에 하나이고 게임 스레드에서만 쓴다(정적 초기화가 로드하는 스레드에서 돈다). 중첩할 수 있다.
     */
    struct SW_API RegistrationReloadScope
    {
        RegistrationReloadScope();
        ~RegistrationReloadScope();
        RegistrationReloadScope( const RegistrationReloadScope& )            = delete;
        RegistrationReloadScope& operator=( const RegistrationReloadScope& ) = delete;

        /** @brief 교체 이미지를 올리는 중이면 true 입니다. */
        static bool isActive();
    };
} // namespace sw

namespace sw
{
    /**
     * @class RegistrationListBase
     * @brief 살아 있는 `RegistrationList` 를 한곳에서 열거하는 연결 고리입니다(진단 · 테스트용 — 소유 관리자가 아닙니다).
     * @details 목록은 만들어질 때 자기 이름(`getListName`)과 함께 전역 연결 목록에 오르고, 사라질 때 빠집니다. 노드는 목록 자신이라 할당이 없고
     *          연결 목록의 머리와 잠금이 상수 초기화라, 다른 번역 단위의 정적 초기화 · 모듈 언로드 중에도 안전합니다.
     *          열거(`collect`)는 잠금 안에서 포인터만 모읍니다. 항목 수와 이름은 각 목록의 소유자 잠금 밖에서 읽으므로, 등록이 멈춘 때(테스트 · 진단)에만 읽습니다.
     */
    class SW_API RegistrationListBase
    {
    public:
        RegistrationListBase( const RegistrationListBase& )            = delete;
        RegistrationListBase& operator=( const RegistrationListBase& ) = delete;

        /** @brief 목록의 이름입니다(만든 곳이 정한 영문 식별자 — 정적 테이블은 소유 레지스트리 이름, 인스턴스 테이블은 멤버 역할). */
        const utf8* getListName() const { return _pListName; }

        /** @brief 올라 있는 항목 수입니다. */
        virtual uint32 getCount() const = 0;
        /** @brief 순서대로 index 번째 항목을 올릴 때 복사한 이름입니다(이름 없는 등록은 빈 글). */
        virtual const string& getNameAt( uint32 index ) const = 0;

        /** @brief 지금 살아 있는 목록을 @p outListRegistration 에 담습니다(비운 뒤 채웁니다). 나중에 만든 것이 먼저입니다. */
        static void collect( vector<const RegistrationListBase*>& outListRegistration );
        /** @brief 지금 살아 있는 목록 수입니다. */
        static uint32 getListCount();

    protected:
        /** @brief 전역 연결 목록에 오릅니다. */
        explicit RegistrationListBase( const utf8* pListName );
        ~RegistrationListBase();

        /** @brief 연결 목록에서 빠집니다. 파생이 소멸자 첫머리에서 부릅니다(파생 부분이 부서진 뒤에 열거가 가상 함수를 부르지 않게). 두 번 불러도 됩니다. */
        void unlink();

    private:
        const utf8*           _pListName;
        RegistrationListBase* _pPrev;
        RegistrationListBase* _pNext;
        bool                  _bLinked;
    };
} // namespace sw

namespace sw
{
    /** @brief 목록이 항목을 늘어놓는 순서입니다. */
    enum class RegistrationOrder : uint8
    {
        Insertion,       ///< 등록한 순서(빼도 남은 것의 순서는 그대로)
        ByOrderThenName, ///< (순서 값, 이름) 사전순 — 정적 초기화 순서가 번역 단위마다 다른 등록자(에디터 확장)를 위해
    };

    /** @brief `RegistrationList::add` 의 결과입니다. */
    enum class RegistrationResult : uint8
    {
        Added,          ///< 새로 올렸습니다
        AlreadyPresent, ///< 같은 객체가 이미 올라 있습니다(멱등 — 할 일이 없습니다)
        NullItem,       ///< nullptr 은 올리지 않습니다
        EmptyName,      ///< 이름을 요구하는 목록(`bRequireName`)에 이름이 비었습니다
        DuplicateName,  ///< 같은 이름의 다른 객체가 이미 올라 있습니다 — 먼저 것을 둡니다
    };

    /**
     * @class RegistrationList
     * @brief 엔진 · 에디터 레지스트리가 같은 구조로 보관하는 목록입니다. 항목은 소유하지 않습니다(수명은 등록한 쪽).
     * @details 레지스트리마다 다시 짜기 쉬운 다섯 가지를 한 곳에 둡니다:
     *          - **중복 거절**: 같은 객체는 한 번만 오르고(`AlreadyPresent`), 이름을 주면 같은 이름의 다른 객체를 거절합니다(`DuplicateName`).
     *          - **정렬**: 등록 순서 또는 (순서 값, 이름). 빼기는 순서를 지킵니다.
     *          - **찾기**: 이름으로(`findByName`) · 객체로(`contains`).
     *          - **훑기**: `getItems()` 는 포인터만 든 연속 배열이라 훑는 쪽에 비용이 없습니다.
     *          - **이름 사본**: 이름은 **올릴 때** 복사합니다. 모듈이 내려간 뒤에도 그 이름으로 알리거나 찾을 수 있고, 죽은 객체의 가상 함수를 묻지 않습니다.
     *
     *          **저장소는 늘 `EngineMisc` 태그로 셉니다.** 프로세스 정적 레지스트리는 기동 뒤에 오른 모듈(에디터 · 게임)이 등록하며 용량을 키우는데, 넣는 쪽 태그로
     *          세면 그 모듈이 내려간 뒤에도 커진 용량이 그 태그에 남습니다. 종료 끝에서 `shrinkToFit` 으로 남은 항목만큼 다시 잡아 기준선으로 돌려놓습니다.
     *
     *          **만들어질 때 전역 열거에 오릅니다.** 목록마다 이름을 하나 받고(`RegistrationListBase::getListName`), `RegistrationListBase::collect` 가 살아 있는
     *          목록의 이름 · 항목 수 · 항목 이름을 한곳에서 보여 줍니다. 복사 · 이동은 막혀 있으니 목록은 멤버나 함수 지역 정적으로 둡니다.
     *
     *          **잠금은 들지 않습니다.** 여러 스레드가 만지는 레지스트리(카메라 · 빛 · 모듈 언로드 리스너)는 자기 잠금 안에서 부릅니다 — 그 잠금이
     *          "가장 안쪽" 이라는 각자의 규칙을 그대로 지키게 하려는 것입니다. 항목 수가 많고 슬롯 인덱스로 O(1) 빼기를 하는 레지스트리(프리미티브 ·
     *          틱 · 콜라이더)는 이 구조가 아닙니다.
     */
    template <typename T>
    class RegistrationList final : public RegistrationListBase
    {
    public:
        /**
         * @brief 등록 순서 · 이름 선택인 빈 목록입니다.
         * @param pListName 전역 열거(`RegistrationListBase::collect`)에 나오는 목록 이름입니다. 문자열 리터럴이어야 합니다.
         */
        explicit RegistrationList( const utf8* pListName )
            : RegistrationList( pListName, RegistrationOrder::Insertion, false )
        {
        }

        /**
         * @param pListName 전역 열거에 나오는 목록 이름입니다. 문자열 리터럴이어야 합니다.
         * @param order 늘어놓는 순서입니다.
         * @param bRequireName true 면 이름 없는 등록을 거절합니다(이름이 곧 id 인 목록).
         */
        RegistrationList( const utf8* pListName, RegistrationOrder order, bool bRequireName )
            : RegistrationList( pListName, order, bRequireName, NameCase::CaseSensitive )
        {
        }

        /**
         * @param pListName 전역 열거에 나오는 목록 이름입니다. 문자열 리터럴이어야 합니다.
         * @param order 늘어놓는 순서입니다.
         * @param bRequireName true 면 이름 없는 등록을 거절합니다(이름이 곧 id 인 목록).
         * @param nameCase 이름 찾기 · 중복 판정이 대소문자를 보는지입니다(콘솔 명령처럼 사람이 치는 이름은 `IgnoreCase`).
         */
        RegistrationList( const utf8* pListName, RegistrationOrder order, bool bRequireName, NameCase nameCase )
            : RegistrationListBase( pListName )
            , _listItem{}
            , _listName{}
            , _listOrder{}
            , _listShadowItem{}
            , _listShadowName{}
            , _listShadowOrder{}
            , _order{ order }
            , _nameCase{ nameCase }
            , _bRequireName{ bRequireName }
        {
        }

        ~RegistrationList() { unlink(); }

        /**
         * @brief 항목을 올립니다. 이름은 지금 복사합니다.
         * @param name 찾기 · 중복 판정 · 정렬에 쓰는 이름입니다. 비어 있으면 이름으로 찾을 수 없고 이름 중복도 보지 않습니다.
         * @param order `ByOrderThenName` 목록에서 앞뒤를 정하는 값입니다(작을수록 앞).
         */
        RegistrationResult add( T* pItem, string_view name = {}, int32 order = 0 )
        {
            SW_MEMORY_SCOPE( EngineMisc );
            if ( pItem == nullptr )
                return RegistrationResult::NullItem;
            if ( contains( pItem ) )
                return RegistrationResult::AlreadyPresent;
            if ( name.empty() )
            {
                if ( _bRequireName )
                    return RegistrationResult::EmptyName;
            }
            else if ( findByName( name ) != nullptr )
            {
                if ( RegistrationReloadScope::isActive() == false )
                    return RegistrationResult::DuplicateName;
                replaceByName( pItem, name, order );
                return RegistrationResult::Added;
            }

            size_t insertIndex = _listItem.size();
            if ( _order == RegistrationOrder::ByOrderThenName )
            {
                insertIndex = 0;
                while ( insertIndex < _listItem.size() && isBefore( _listOrder[insertIndex], _listName[insertIndex], order, name ) )
                {
                    ++insertIndex;
                }
            }
            _listItem.insert( _listItem.begin() + static_cast<ptrdiff_t>( insertIndex ), pItem );
            _listName.insert( _listName.begin() + static_cast<ptrdiff_t>( insertIndex ), string( name ) );
            _listOrder.insert( _listOrder.begin() + static_cast<ptrdiff_t>( insertIndex ), order );
            return RegistrationResult::Added;
        }

        /** @brief 그 객체가 올라 있으면 뺍니다(남은 것의 순서는 그대로). 뺐으면 true 입니다. 같은 이름의 다른 객체는 건드리지 않습니다. */
        [[nodiscard]] bool remove( const T* pItem )
        {
            const size_t index = findIndex( pItem );
            if ( index == kNotFound )
                return dropShadow( pItem );
            const string name = _listName[index];
            _listItem.erase( _listItem.begin() + static_cast<ptrdiff_t>( index ) );
            _listName.erase( _listName.begin() + static_cast<ptrdiff_t>( index ) );
            _listOrder.erase( _listOrder.begin() + static_cast<ptrdiff_t>( index ) );
            restoreShadow( name );
            return true;
        }

        /** @brief 모두 제거합니다(그림자도). */
        void clear()
        {
            _listItem.clear();
            _listName.clear();
            _listOrder.clear();
            _listShadowItem.clear();
            _listShadowName.clear();
            _listShadowOrder.clear();
        }

        /**
         * @brief 저장소를 지금 항목 수에 맞춰 다시 잡습니다(비었으면 놓기만 합니다). 순서와 이름은 그대로입니다.
         * @details 엔진 종료 끝이 부릅니다 — 언로드된 모듈이 키운 용량과, Debug STL 이 원소를 옮길 때 그 자리 태그로 새로 잡은 문자열 프록시를 돌려놓습니다.
         */
        void shrinkToFit()
        {
            SW_MEMORY_SCOPE( EngineMisc );
            vector<T*>     listItem;
            vector<string> listName;
            vector<int32>  listOrder;
            if ( _listItem.empty() == false )
            {
                listItem.reserve( _listItem.size() );
                listName.reserve( _listName.size() );
                listOrder.reserve( _listOrder.size() );
                for ( size_t index = 0; index < _listItem.size(); ++index )
                {
                    listItem.push_back( _listItem[index] );
                    listName.push_back( std::move( _listName[index] ) );
                    listOrder.push_back( _listOrder[index] );
                }
            }
            _listItem  = std::move( listItem );
            _listName  = std::move( listName );
            _listOrder = std::move( listOrder );
        }

        /** @brief 그 객체가 올라 있는지 봅니다. */
        bool contains( const T* pItem ) const { return findIndex( pItem ) != kNotFound; }

        /** @brief 올릴 때 복사한 이름으로 찾습니다. 없거나 이름이 비었으면 nullptr 입니다. */
        T* findByName( string_view name ) const
        {
            if ( name.empty() )
                return nullptr;
            for ( size_t index = 0; index < _listName.size(); ++index )
            {
                if ( StringUtil::equals( string_view{ _listName[index] }, name, _nameCase == NameCase::IgnoreCase ) )
                    return _listItem[index];
            }
            return nullptr;
        }

        /** @brief 올라 있는 항목 수입니다. */
        uint32 getCount() const override { return static_cast<uint32>( _listItem.size() ); }
        /** @brief 순서대로 index 번째 항목입니다. */
        T* getAt( uint32 index ) const { return _listItem[index]; }
        /** @brief index 번째 항목을 올릴 때 복사한 이름입니다(없으면 빈 글). */
        const string& getNameAt( uint32 index ) const override { return _listName[index]; }
        /** @brief 항목 전부(순서대로)입니다. 소유하지 않습니다. */
        const vector<T*>& getItems() const { return _listItem; }

    private:
        static constexpr size_t kNotFound = static_cast<size_t>( -1 );

        size_t findIndex( const T* pItem ) const
        {
            for ( size_t index = 0; index < _listItem.size(); ++index )
            {
                if ( _listItem[index] == pItem )
                    return index;
            }
            return kNotFound;
        }

        /** @brief 같은 이름의 올라 있는 항목을 @p pItem 으로 바꿉니다(제자리). 바뀐 옛 항목은 그림자로 보관합니다(`RegistrationReloadScope`). */
        void replaceByName( T* pItem, string_view name, int32 order )
        {
            for ( size_t index = 0; index < _listItem.size(); ++index )
            {
                if ( StringUtil::equals( string_view{ _listName[index] }, name, _nameCase == NameCase::IgnoreCase ) == false )
                    continue;
                _listShadowItem.push_back( _listItem[index] );
                _listShadowName.push_back( _listName[index] );
                _listShadowOrder.push_back( _listOrder[index] );
                _listItem[index]  = pItem;
                _listOrder[index] = order;
                return;
            }
        }

        /** @brief 그림자에 있던 항목을 버립니다(옛 이미지가 내려가며 뺐다). 있었으면 true 입니다. */
        bool dropShadow( const T* pItem )
        {
            for ( size_t index = _listShadowItem.size(); index > 0; --index )
            {
                if ( _listShadowItem[index - 1] != pItem )
                    continue;
                eraseShadow( index - 1 );
                return true;
            }
            return false;
        }

        /** @brief 올라 있던 항목이 제거됐고 그 이름의 그림자가 있으면 가장 나중 것을 복원합니다(새 이미지가 취소돼 옛 등록이 돌아온다). */
        void restoreShadow( const string& name )
        {
            for ( size_t index = _listShadowItem.size(); index > 0; --index )
            {
                if ( StringUtil::equals( string_view{ _listShadowName[index - 1] }, string_view{ name }, _nameCase == NameCase::IgnoreCase ) == false )
                    continue;
                T*           pItem       = _listShadowItem[index - 1];
                const int32  shadowOrder = _listShadowOrder[index - 1];
                const string shadowName  = _listShadowName[index - 1];
                eraseShadow( index - 1 );
                size_t insertIndex = _listItem.size();
                if ( _order == RegistrationOrder::ByOrderThenName )
                {
                    insertIndex = 0;
                    while ( insertIndex < _listItem.size() && isBefore( _listOrder[insertIndex], _listName[insertIndex], shadowOrder, shadowName ) )
                    {
                        ++insertIndex;
                    }
                }
                _listItem.insert( _listItem.begin() + static_cast<ptrdiff_t>( insertIndex ), pItem );
                _listName.insert( _listName.begin() + static_cast<ptrdiff_t>( insertIndex ), shadowName );
                _listOrder.insert( _listOrder.begin() + static_cast<ptrdiff_t>( insertIndex ), shadowOrder );
                return;
            }
        }

        void eraseShadow( size_t index )
        {
            _listShadowItem.erase( _listShadowItem.begin() + static_cast<ptrdiff_t>( index ) );
            _listShadowName.erase( _listShadowName.begin() + static_cast<ptrdiff_t>( index ) );
            _listShadowOrder.erase( _listShadowOrder.begin() + static_cast<ptrdiff_t>( index ) );
        }

        /** @brief (lhsOrder, lhsName) 가 (rhsOrder, rhsName) 보다 앞이면 true 입니다. */
        static bool isBefore( int32 lhsOrder, const string& lhsName, int32 rhsOrder, string_view rhsName )
        {
            if ( lhsOrder != rhsOrder )
                return lhsOrder < rhsOrder;
            return string_view{ lhsName } < rhsName;
        }

    private:
        vector<T*>        _listItem;        ///< 항목(순서대로). 훑기가 이것만 읽는다
        vector<string>    _listName;        ///< 항목마다 올릴 때 복사한 이름(같은 인덱스)
        vector<int32>     _listOrder;       ///< 항목마다 정렬 값(같은 인덱스)
        vector<T*>        _listShadowItem;  ///< 리로드의 교체 등록에 이름을 내준 옛 항목(옛 이미지가 내려갈 때까지 보관)
        vector<string>    _listShadowName;  ///< 그림자 항목의 이름
        vector<int32>     _listShadowOrder; ///< 그림자 항목의 정렬 값
        RegistrationOrder _order;           ///< 늘어놓는 순서
        NameCase          _nameCase;        ///< 이름 비교가 대소문자를 보는지
        bool              _bRequireName;    ///< 이름 없는 등록을 거절하는지
    };
} // namespace sw

namespace sw
{
    /**
     * @class NameRegistry
     * @brief 이름(`hashed_string`, 대소문자 무시) → 값을 **소유하는** 레지스트리입니다. 값은 등록 순서로 연속 배열에 둡니다.
     * @details 값을 보관해야 하는 테이블(변환기 · 활동 종류 · 연산 · 패널 그리기 함수)가 같은 구조로 씁니다. 빈 이름은 받지 않습니다.
     *          `find` 가 돌려준 포인터는 다음 `add` · `addOrReplace`(새 이름) · `removeIf` · `clear` 까지만 유효합니다.
     *          잠금은 들지 않습니다 — 여러 스레드가 만지면 쓰는 쪽이 자기 잠금 안에서 부릅니다.
     */
    template <typename T>
    class NameRegistry
    {
    public:
        NameRegistry()
            : _listName{}
            , _listValue{}
        {
        }

        /** @brief 값을 올립니다. 이름이 비었거나 같은 이름이 이미 있으면 올리지 않고 false 입니다(먼저 것을 둡니다). */
        bool add( const hashed_string& name, T value )
        {
            if ( name.empty() || findIndex( name ) != kNotFound )
                return false;
            _listName.push_back( name );
            _listValue.push_back( std::move( value ) );
            return true;
        }

        /** @brief 값을 올립니다. 같은 이름이 있으면 그 위치(순서)에서 값을 바꿉니다. 빈 이름은 무시합니다. */
        void addOrReplace( const hashed_string& name, T value )
        {
            if ( name.empty() )
                return;
            const size_t index = findIndex( name );
            if ( index != kNotFound )
            {
                _listName[index]  = name;
                _listValue[index] = std::move( value );
                return;
            }
            _listName.push_back( name );
            _listValue.push_back( std::move( value ) );
        }

        /** @brief 이름의 값입니다. 없으면 nullptr 입니다. */
        T* find( const hashed_string& name )
        {
            const size_t index = findIndex( name );
            return index == kNotFound ? nullptr : &_listValue[index];
        }

        /** @brief 이름의 값입니다. 없으면 nullptr 입니다. */
        const T* find( const hashed_string& name ) const
        {
            const size_t index = findIndex( name );
            return index == kNotFound ? nullptr : &_listValue[index];
        }

        /** @brief @p predicate( const T& ) 가 true 인 값을 모두 제거합니다(남은 것의 순서는 그대로). 뺀 수입니다. */
        template <typename Predicate>
        uint32 removeIf( Predicate predicate )
        {
            uint32 removedCount = 0;
            for ( size_t index = _listValue.size(); index > 0; --index )
            {
                if ( predicate( static_cast<const T&>( _listValue[index - 1] ) ) == false )
                    continue;
                _listName.erase( _listName.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                _listValue.erase( _listValue.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                ++removedCount;
            }
            return removedCount;
        }

        /** @brief 모두 제거합니다. */
        void clear()
        {
            _listName.clear();
            _listValue.clear();
        }

        /** @brief 올라 있는 값 수입니다. */
        uint32 getCount() const { return static_cast<uint32>( _listValue.size() ); }
        /** @brief index 번째 값을 올린 이름입니다. */
        const hashed_string& getNameAt( uint32 index ) const { return _listName[index]; }
        /** @brief 값 전부(등록 순서)입니다. */
        const vector<T>& getItems() const { return _listValue; }

    private:
        static constexpr size_t kNotFound = static_cast<size_t>( -1 );

        size_t findIndex( const hashed_string& name ) const
        {
            if ( name.empty() )
                return kNotFound;
            for ( size_t index = 0; index < _listName.size(); ++index )
            {
                if ( _listName[index] == name )
                    return index;
            }
            return kNotFound;
        }

    private:
        vector<hashed_string> _listName;  ///< 값마다 올린 이름(같은 인덱스)
        vector<T>             _listValue; ///< 값(등록 순서)
    };
} // namespace sw
