#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class LocalizedTextArena
     * @brief 번역 문자열의 저장소입니다. **추가만 하고 주인(`LocalizationManager`)이 소멸할 때까지 해제하지 않습니다.** 같은 내용은 한 번만 둡니다.
     * @details `StringTable` 과 `LocalizationManager` 의 조회는 `const utf8*` 를 돌려주고 UI 와 워커가 그것을 가지고 있다. 테이블이 문자열을
     *          값으로 가지면 락을 놓은 뒤 `setString`, 다시 읽기, 언어 언로드가 그 저장소를 바꿀 때 가지고 있던 포인터가 해제된 메모리를
     *          가리킨다(조밀 해시 테이블이라 **다른 키를 넣기만 해도** 짧은 문자열이 옮겨진다). 언리얼 FText 가 공유 문자열을 가지고
     *          있는 것과 같은 보장을, 여기서는 문자열이 사라지지 않게 해서 준다. 같은 파일을 다시 읽으면 내용이 같으니 늘지 않는다.
     *          예전에는 소유자 없는 엔진 수명 값(`ScopeOwned`)이었다 — 이제 테이블을 만드는 관리자가 값으로 들고 테이블이 생성자에서 받는다.
     */
    class SW_API LocalizedTextArena
    {
    public:
        LocalizedTextArena()                                       = default;
        LocalizedTextArena( const LocalizedTextArena& )            = delete;
        LocalizedTextArena& operator=( const LocalizedTextArena& ) = delete;

        /** @brief @p text 와 같은 내용의 안정된 C 문자열을 돌려줍니다(없으면 복사해 둡니다). 여러 스레드가 함께 불러도 됩니다. */
        const utf8* store( string_view text ) SW_EXCLUDES_LOCK( _mutex );

    private:
        static constexpr size_t kBlockBytes = 64 * 1024;

        mutex                                            _mutex;
        vector<MemoryBlock> _listBlock                   SW_GUARDED_BY( _mutex );
        unordered_map<uint64, const utf8*> _mapByContent SW_GUARDED_BY( _mutex );
        utf8* _pCursor                                   SW_GUARDED_BY( _mutex ){ nullptr };
        size_t _remainingBytes                           SW_GUARDED_BY( _mutex ){ 0 };
    };

    /**
     * @class StringTable
     * @brief 문화권 하나의 실행 테이블(키 해시 → 텍스트)입니다. 파일을 읽지 않으며, `LocalizationManager` 가 원문과 번역 테이블 문서에서 채웁니다.
     */
    class SW_API StringTable
    {
    public:
        /** @brief 문자열을 @p arena 에 두는 빈 테이블입니다. 저장소는 테이블보다 오래 삽니다(둘 다 `LocalizationManager` 가 든다). */
        explicit StringTable( LocalizedTextArena& arena );
        StringTable( const StringTable& )            = delete;
        StringTable& operator=( const StringTable& ) = delete;

        /**
         * @brief 키를 찾습니다. 없으면 nullptr 입니다.
         * @details 표는 대체 글을 받지 않는다 — 없을 때 무엇을 보일지는 부르는 쪽(`LocalizationManager::getString` 은 키 이름)이 정한다.
         */
        [[nodiscard]] const utf8* findString( const hashed_string& key ) const;
        /**
         * @brief 키를 **intern 하지 않고** 조회합니다. 없으면 nullptr 입니다.
         * @details 테이블은 해시로만 열리므로 조회에 intern 이 필요 없다. 키가 아닐 수도 있는 텍스트로
         *          물어보는 곳(대사 원문 등)은 반드시 이쪽을 쓴다. `hashed_string` 을 만들면
         *          그 텍스트가 intern 아레나에 **영구히** 남는다.
         */
        [[nodiscard]] const utf8* findStringByText( string_view keyText ) const;
        /**
         * @brief 미리 구한 해시로 곧바로 조회합니다. 없으면 nullptr 입니다.
         * @details `LocalizationManager` 가 활성 언어와 폴백 언어를 훑을 때 **해시를 한 번만**
         *          구하려고 이것을 씁니다. 두 테이블에 같은 문자열을 두 번 해싱할 이유가 없습니다.
         */
        [[nodiscard]] const utf8* findByHash( uint64 keyHash ) const;
        bool                      contains( const hashed_string& key ) const;
        void                      setString( const hashed_string& key, const string& value );
        /** @brief 키 해시로 넣습니다. 키를 intern 하지 않습니다(`hashed_string::computeHash` 와 같은 해시). */
        void   setStringByHash( uint64 keyHash, string_view value );
        void   clear();
        size_t size() const;
        bool   empty() const;

    private:
        LocalizedTextArena&  _arena; ///< 문자열 저장소(주인이 같다)
        mutable shared_mutex _mutex;
        /// @brief 키 해시 → 번역 문자열 테이블입니다. 문자열은 추가 전용 저장소(`LocalizedTextArena`)에 있어 테이블이 바뀌어도 관리자가 소멸할 때까지 사라지지 않습니다.
        unordered_map<uint64, const utf8*> _mapTable SW_GUARDED_BY( _mutex );
    };
} // namespace sw
