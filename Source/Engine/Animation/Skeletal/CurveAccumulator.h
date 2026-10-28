/**
 * @file CurveAccumulator.h
 * @brief 한 프레임의 커브 값을 이름별로 모읍니다(같은 이름은 더합니다).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class CurveAccumulator
     * @brief 이름 목록 · 값 목록 · 이름→인덱스 맵을 한 몸으로 묶어 셋이 어긋나지 않게 합니다.
     * @details 더하는 비용이 곡선 수와 무관한 O(1) 이다(예전에는 곡선마다 이름 목록을 선형으로 찾아 곡선 수의 제곱이었다).
     *          이름 순서는 처음 더한 순서이고 값 목록은 같은 인덱스를 쓴다 — 읽는 쪽은 인덱스로 훑으면 된다.
     */
    class CurveAccumulator
    {
    public:
        /** @brief 모두 비웁니다. 용량은 남겨 프레임마다 다시 할당하지 않습니다. */
        void clear()
        {
            _listName.clear();
            _listValue.clear();
            _mapIndex.clear();
        }

        /** @brief @p name 의 값에 @p value 를 더합니다. 처음 보는 이름이면 목록 끝에 새로 둡니다. */
        void add( const hashed_string& name, float32 value )
        {
            const auto found = _mapIndex.find( name );
            if ( found == _mapIndex.end() )
            {
                _mapIndex.emplace( name, _listName.size() );
                _listName.push_back( name );
                _listValue.push_back( value );
                return;
            }
            _listValue[found->second] += value;
        }

        /** @brief @p name 의 값입니다. 없으면 0 입니다. */
        [[nodiscard]] float32 getValue( const hashed_string& name ) const
        {
            const auto found = _mapIndex.find( name );
            return found == _mapIndex.end() ? 0.0f : _listValue[found->second];
        }

        [[nodiscard]] const vector<hashed_string>& getNames() const { return _listName; }
        [[nodiscard]] const vector<float32>&       getValues() const { return _listValue; }

    private:
        vector<hashed_string>                                         _listName;
        vector<float32>                                               _listValue; ///< `_listName` 과 인덱스가 같은 값
        unordered_map<hashed_string, size_t, hashed_string::HashFunc> _mapIndex;
    };
} // namespace sw
