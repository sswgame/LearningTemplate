/**
 * @file MaterialGlobals.h
 * @brief 모든 머티리얼 셰이더가 함께 읽는 값입니다(PassCB `g_MaterialGlobal*` — 언리얼 Material Parameter Collection 의 유니폼 버퍼, 유니티 `Shader.SetGlobalVector` 자리).
 * @details 머티리얼마다 같은 값을 자기 원소에 들고 있으면 값 하나가 바뀔 때 그 머티리얼 수 · 인스턴스 수만큼 사본을 다시 만들고 올린다(날씨 · 바람 시간이 그랬다).
 *          여기 있는 값은 패킷이 한 벌 실어 프레임 상수에 한 번 쓴다 — 바꾸는 비용이 머티리얼 수와 무관하다.
 *          값을 더하려면 이 구조체에 칸, `binding.hlsli` 에 같은 이름의 상수, `PassConstantNames` 에 이름, 파일에서 오는 값이면 `MaterialGlobalSlot` 에 열거자를 더한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 파라미터 묶음(`*.matparams.xml`)의 값이 먹이는 전역 값입니다(`MaterialCollectionParameterDef::_global`). */
    ENUM()
    enum class MaterialGlobalSlot : uint8
    {
        None,    ///< 전역이 아니다 — 머티리얼 프로퍼티가 이름으로 묶는다
        Weather, ///< `MaterialGlobals::_weather`
        Rain,    ///< `MaterialGlobals::_rain`
    };
} // namespace sw

namespace sw
{
    /** @struct MaterialGlobals @brief 한 프레임의 전역 머티리얼 값입니다(파일 머리말 참고). */
    struct MaterialGlobals
    {
        float4 _weather{};     ///< x 젖음(0..1), y 웅덩이(0..1), z 눈(0..1), w 설선 높이(m) — PassCB `g_MaterialGlobalWeather`
        float4 _rain{};        ///< x 비 세기(0..1), yzw 0(예비) — PassCB `g_MaterialGlobalRain`
        float4 _environment{}; ///< xyz 환경의 기준 시점(게임 카메라 위치), w 환경 시간(초 — 바람) — PassCB `g_MaterialGlobalEnvironment`
    };
} // namespace sw
