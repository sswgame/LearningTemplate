/**
 * @file TemporalMath.h
 * @brief 시간 누적(TAA · TAAU)과 자동 노출 히스토그램의 스칼라 식(`temporal_math.hlsli`)을 C++ 에서 그대로 부르게 하는 다리입니다.
 * @details 셰이더 파일 하나를 두 언어가 읽는다 — 테스트가 셰이더가 아닌 사본을 재지 않게(`ShadingMath.h` 와 같은 길). 셰이더 파일이 쓰는 HLSL 내장 함수는
 *          `sw::shadermath` 에 같은 이름으로 있다(ShadingMath.h — 더 쓰려면 거기에 한 줄을 더한다, 빠뜨리면 C++ 컴파일이 멈추므로 조용히 갈라지지 않는다).
 */
#pragma once
#include "temporal_math.hlsli"
