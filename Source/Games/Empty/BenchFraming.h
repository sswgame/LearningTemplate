/**
 * @file BenchFraming.h
 * @brief 벤치 카메라 맞춤의 보기 모드와 입력 값입니다. 장면에 묶이지 않은 순수 구조라 단위 테스트가 직접 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include <cstring>
#include <type_traits>

namespace sw
{
    /** @brief 벤치가 카메라를 맞추는 보기 모드입니다. 여러 벤치를 함께 켜면 열거 순서가 높은 모드가 정합니다. `despawn` 이 격자로 되돌립니다. */
    enum class FramingMode : uint32
    {
        Grid,      ///< 큐브 격자(기본).
        Character, ///< `-gv_benchCharacters` 캐릭터 줄(또는 격자).
        Combat,    ///< `-gv_benchCombat` 전투 연출.
        Ragdoll,   ///< `-gv_benchRagdolls` 래그돌 군중.
        Rig,       ///< `-gv_benchRig` 후처리 리그 기사.
        Face,      ///< `-gv_benchFaces` 테스트 머리 줄.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FramingInput
     * @brief 카메라 맞춤이 읽는 입력 값을 모은 구조입니다. 마지막으로 맞춘 값과 달라지면 다시 맞춥니다.
     * @details 맞춤이 새 입력(`-gv_bench…`)을 읽게 되면 여기에 필드를 더하고 `captureFramingInput` 이 채웁니다. 비교는 바이트 단위라
     *          필드를 더해도 비교에서 빠지지 않습니다. 모든 필드가 정수라 빈틈 · 부동소수점이 없음을 아래 `static_assert` 가 지킵니다
     *          (부동소수점을 더하면 컴파일이 멈춥니다. 정수로 바꾸어 담습니다). 읽지 않는 필드는 0 으로 두어 상관없는 값이 맞춤을 부르지 않게 합니다.
     */
    struct FramingInput
    {
        FramingMode mode{ FramingMode::Grid };
        uint32      cameraCount{ 0 }; ///< 씬의 카메라 수. 맞춘 뒤에 생긴 카메라(늦게 만들어지는 에디터 씬 뷰 카메라)도 맞추게 합니다.
        uint32      gridSide{ 0 };    ///< 격자 한 변의 큐브 수(Grid).
        int32       characters{ 0 };  ///< `-gv_benchCharacters`(Character · Combat · Ragdoll 의 깊이 계산이 읽습니다).
        int32       columns{ 0 };     ///< `-gv_benchCharacterColumns`(위와 같음).
        int32       faces{ 0 };       ///< `-gv_benchFaces`(Face).
        uint32      rigCount{ 0 };    ///< 리그 기사 수(Rig).
        int32       rigView{ 0 };     ///< `-gv_benchRigView`(Rig).

        bool operator==( const FramingInput& other ) const { return std::memcmp( this, &other, sizeof( FramingInput ) ) == 0; }
        bool operator!=( const FramingInput& other ) const { return operator==( other ) == false; }
    };
    static_assert( std::has_unique_object_representations_v<FramingInput>, "FramingInput 은 빈틈 · 부동소수점 없이 정수 필드만 가져야 바이트 비교가 맞다." );

    /** @brief 지금 모드 @p current 와 새 스폰이 요청한 @p requested 중 카메라를 정할 모드를 고릅니다(열거 순서가 높은 쪽). */
    inline FramingMode selectFramingMode( FramingMode current, FramingMode requested )
    {
        return static_cast<uint32>( requested ) > static_cast<uint32>( current ) ? requested : current;
    }
} // namespace sw
