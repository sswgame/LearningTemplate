/**
 * @file string_splitter.h
 * @brief 구분자 기반 고속 문자열 분할기 및 순방향 반복자(Forward Iterator)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) basic_string_split_iterator — Zero-Allocation 지연 평가 순방향 반복자
    // ------------------------------------------------------------------------------
    template <typename T>
    class basic_string_split_iterator
    {
    public:
        using value_type        = std::basic_string_view<T>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const value_type*;
        using reference         = const value_type&;
        using iterator_category = std::forward_iterator_tag;

        basic_string_split_iterator() noexcept
            : _str{}
            , _current{}
            , _start{ 0 }
            , _tokenOffset{ 0 }
            , _index{ 0 }
            , _charDelim{}
            , _strDelim{}
            , _multiChars{}
            , _listDelim{}
            , _mode{ Mode::End }
        {
        }

        explicit basic_string_split_iterator( std::basic_string_view<T> str, T delim );
        explicit basic_string_split_iterator( std::basic_string_view<T> str, std::basic_string_view<T> delim );
        explicit basic_string_split_iterator( std::basic_string_view<T> str, std::initializer_list<std::basic_string_view<T>> listDelim );

        value_type        operator*() const noexcept { return _current; }
        const value_type* operator->() const noexcept { return &_current; }

        /** @brief 현재 토큰의 순번 인덱스 (0, 1, 2, ...) */
        size_t getIndex() const noexcept { return _index; }

        /** @brief 원본 문자열 내에서 현재 토큰의 시작 오프셋 */
        size_t getOffset() const noexcept { return _tokenOffset; }

        basic_string_split_iterator& operator++();
        basic_string_split_iterator  operator++( int32 );

        bool operator==( const basic_string_split_iterator& other ) const noexcept
        {
            if ( _mode == Mode::End && other._mode == Mode::End )
                return true;
            if ( _mode == Mode::End || other._mode == Mode::End )
                return false;
            return _str.data() == other._str.data() && _start == other._start && _index == other._index;
        }

        bool operator!=( const basic_string_split_iterator& other ) const noexcept
        {
            return ( *this == other ) == false;
        }

    private:
        enum class Mode : uint8
        {
            Char,
            String,
            MultiChar,
            List,
            FinishedOnce,
            End
        };

        void advance();

        std::basic_string_view<T>         _str{};
        std::basic_string_view<T>         _current{};
        size_t                            _start{ 0 };
        size_t                            _tokenOffset{ 0 };
        size_t                            _index{ 0 };
        T                                 _charDelim{};
        std::basic_string_view<T>         _strDelim{};
        std::basic_string<T>              _multiChars{};
        vector<std::basic_string_view<T>> _listDelim{};
        Mode                              _mode{ Mode::End };
    };

    using string_split_iterator  = basic_string_split_iterator<utf8>;
    using wstring_split_iterator = basic_string_split_iterator<utf16>;

    // ------------------------------------------------------------------------------
    // 2) basic_string_splitter — 생성 시 분할 및 반복자/인덱스 접근 제공
    // ------------------------------------------------------------------------------
    template <typename T>
    /** @brief 구분자가 나타날 때마다 원본 뷰를 잘라 목록에 넣습니다. */
    class basic_string_splitter
    {
    public:
        using value_type       = T;
        using string_view_type = std::basic_string_view<value_type>;
        using iterator         = basic_string_split_iterator<value_type>;
        using const_iterator   = iterator;

        /** @brief str 을 단일 문자 delim 기준으로 분할합니다 (가장 빠른 SIMD memchr 경로). */
        explicit basic_string_splitter( string_view_type str, value_type delim );

        /** @brief str 을 단일 문자열 뷰 delim 기준으로 분할합니다. */
        explicit basic_string_splitter( string_view_type str, string_view_type delim );

        /** @brief str 을 listDelim 기준으로 분할합니다. */
        explicit basic_string_splitter( string_view_type str, std::initializer_list<string_view_type> listDelim );

        /** @brief 잘린 조각 개수입니다. */
        uint32 getCount() const noexcept { return static_cast<uint32>( _listSplit.size() ); }

        /** @brief 비어 있는지 확인합니다. */
        bool empty() const noexcept { return _listSplit.empty(); }

        /** @brief 인덱싱 연산자 (배열 접근) */
        string_view_type operator[]( size_t index ) const { return _listSplit[index]; }

        /** @brief 원본을 가리키는 부분 뷰 목록입니다. */
        const vector<string_view_type>& getSplitList() const noexcept { return _listSplit; }

        /** @brief 이터레이터 순회 지원 */
        iterator       begin() const { return _beginIt; }
        iterator       end() const noexcept { return iterator{}; }
        const_iterator cbegin() const { return _beginIt; }
        const_iterator cend() const noexcept { return iterator{}; }

    private:
        void split( value_type delim );
        void split( string_view_type delim );
        void split( std::initializer_list<string_view_type> listDelim );

        string_view_type         _str;
        iterator                 _beginIt;
        vector<string_view_type> _listSplit;
    };

    using string_splitter = basic_string_splitter<utf8>;

    using wstring_splitter = basic_string_splitter<utf16>;

#pragma region IMPLEMENTATION

    // ------------------------------------------------------------------------------
    // basic_string_split_iterator 구현
    // ------------------------------------------------------------------------------
    template <typename T>
    basic_string_split_iterator<T>::basic_string_split_iterator( std::basic_string_view<T> str, T delim )
        : _str{ str }
        , _current{}
        , _start{ 0 }
        , _tokenOffset{ 0 }
        , _index{ 0 }
        , _charDelim{ delim }
        , _strDelim{}
        , _multiChars{}
        , _listDelim{}
        , _mode{ Mode::Char }
    {
        if ( _str.empty() == true )
        {
            _mode = Mode::End;
        }
        else
        {
            advance();
        }
    }

    template <typename T>
    basic_string_split_iterator<T>::basic_string_split_iterator( std::basic_string_view<T> str, std::basic_string_view<T> delim )
        : _str{ str }
        , _current{}
        , _start{ 0 }
        , _tokenOffset{ 0 }
        , _index{ 0 }
        , _charDelim{}
        , _strDelim{ delim }
        , _multiChars{}
        , _listDelim{}
        , _mode{ Mode::String }
    {
        if ( _str.empty() == true )
        {
            _mode = Mode::End;
        }
        else if ( delim.length() == 1 )
        {
            _charDelim = delim[0];
            _mode      = Mode::Char;
            advance();
        }
        else
        {
            advance();
        }
    }

    template <typename T>
    basic_string_split_iterator<T>::basic_string_split_iterator( std::basic_string_view<T> str, std::initializer_list<std::basic_string_view<T>> listDelim )
        : _str{ str }
        , _current{}
        , _start{ 0 }
        , _tokenOffset{ 0 }
        , _index{ 0 }
        , _charDelim{}
        , _strDelim{}
        , _multiChars{}
        , _listDelim{ listDelim }
        , _mode{ Mode::List }
    {
        if ( _str.empty() == true )
        {
            _mode = Mode::End;
        }
        else if ( listDelim.size() == 0 )
        {
            _current     = _str;
            _tokenOffset = 0;
            _start       = _str.length();
            _mode        = Mode::FinishedOnce;
        }
        else if ( listDelim.size() == 1 )
        {
            const auto& d = *listDelim.begin();
            if ( d.length() == 1 )
            {
                _charDelim = d[0];
                _mode      = Mode::Char;
            }
            else
            {
                _strDelim = d;
                _mode     = Mode::String;
            }
            advance();
        }
        else
        {
            bool bAllSingleChar = true;
            for ( const auto& d : listDelim )
            {
                if ( d.length() != 1 )
                {
                    bAllSingleChar = false;
                    break;
                }
            }
            if ( bAllSingleChar )
            {
                _multiChars.reserve( listDelim.size() );
                for ( const auto& d : listDelim )
                    _multiChars.push_back( d[0] );
                _mode = Mode::MultiChar;
            }
            advance();
        }
    }

    template <typename T>
    basic_string_split_iterator<T>& basic_string_split_iterator<T>::operator++()
    {
        if ( _mode != Mode::End )
        {
            ++_index;
            advance();
        }
        return *this;
    }

    template <typename T>
    basic_string_split_iterator<T> basic_string_split_iterator<T>::operator++( int32 )
    {
        basic_string_split_iterator tmp = *this;
        ++( *this );
        return tmp;
    }

    template <typename T>
    void basic_string_split_iterator<T>::advance()
    {
        if ( _mode == Mode::FinishedOnce )
        {
            _mode = Mode::End;
            return;
        }

        const size_t length = _str.length();
        while ( _start < length )
        {
            size_t pos         = value_type::npos;
            size_t delimLength = 0;

            switch ( _mode )
            {
                case Mode::Char:
                    pos         = _str.find( _charDelim, _start );
                    delimLength = 1;
                    break;
                case Mode::String:
                    if ( _strDelim.empty() == true )
                    {
                        _tokenOffset = _start;
                        _current     = _str.substr( _start );
                        _start       = length;
                        return;
                    }
                    pos         = _str.find( _strDelim, _start );
                    delimLength = _strDelim.length();
                    break;
                case Mode::MultiChar:
                    pos         = _str.find_first_of( _multiChars, _start );
                    delimLength = 1;
                    break;
                case Mode::List:
                {
                    size_t minPos = value_type::npos;
                    for ( const auto& delim : _listDelim )
                    {
                        if ( delim.empty() == true )
                            continue;
                        const size_t p = _str.find( delim, _start );
                        if ( p != value_type::npos && p < minPos )
                        {
                            minPos      = p;
                            delimLength = delim.length();
                        }
                    }
                    pos = minPos;
                    break;
                }
                case Mode::FinishedOnce:
                case Mode::End:
                default:
                    _mode = Mode::End;
                    return;
            }

            if ( pos == value_type::npos )
            {
                _tokenOffset = _start;
                _current     = _str.substr( _start );
                _start       = length;
                return;
            }

            if ( pos > _start )
            {
                _tokenOffset = _start;
                _current     = _str.substr( _start, pos - _start );
                _start       = pos + delimLength;
                return;
            }

            _start = pos + delimLength;
        }

        _mode = Mode::End;
    }

    // ------------------------------------------------------------------------------
    // basic_string_splitter 구현
    // ------------------------------------------------------------------------------
    template <typename TChar>
    basic_string_splitter<TChar>::basic_string_splitter( string_view_type str, value_type delim )
        : _str{ str }
        , _beginIt{ str, delim }
        , _listSplit{}
    {
        split( delim );
    }

    template <typename TChar>
    basic_string_splitter<TChar>::basic_string_splitter( string_view_type str, string_view_type delim )
        : _str{ str }
        , _beginIt{ str, delim }
        , _listSplit{}
    {
        split( delim );
    }

    template <typename TChar>
    basic_string_splitter<TChar>::basic_string_splitter( string_view_type str, std::initializer_list<string_view_type> listDelim )
        : _str{ str }
        , _beginIt{ str, listDelim }
        , _listSplit{}
    {
        split( listDelim );
    }

    template <typename TChar>
    void basic_string_splitter<TChar>::split( value_type delim )
    {
        if ( _str.empty() == true )
            return;

        const size_t count = static_cast<size_t>( std::count( _str.begin(), _str.end(), delim ) );
        _listSplit.reserve( count + 1 );

        for ( auto it = _beginIt; it != end(); ++it )
            _listSplit.push_back( *it );
    }

    template <typename TChar>
    void basic_string_splitter<TChar>::split( string_view_type delim )
    {
        if ( _str.empty() == true )
            return;

        if ( delim.length() == 1 )
        {
            split( delim[0] );
            return;
        }

        _listSplit.reserve( 8 );
        for ( auto it = _beginIt; it != end(); ++it )
            _listSplit.push_back( *it );
    }

    template <typename TChar>
    void basic_string_splitter<TChar>::split( [[maybe_unused]] std::initializer_list<string_view_type> listDelim )
    {
        if ( _str.empty() == true )
            return;

        _listSplit.reserve( 8 );
        for ( auto it = _beginIt; it != end(); ++it )
            _listSplit.push_back( *it );
    }
#pragma endregion

} // namespace sw
