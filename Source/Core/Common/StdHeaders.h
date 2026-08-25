/**
 * @file StdHeaders.h
 * @brief Foundation에서 쓰는 C/C++ 표준 라이브러리 헤더 묶음.
 * @details Third Party(imgui, vulkan, glad 등)는 여기 넣지 말고 사용처에서 include 합니다.
 *
 * @warning PCH 없이 이 파일을 직접 포함하면 빌드가 느려질 수 있습니다.
 *          - <regex>   : 파싱 테이블 생성으로 컴파일 시간이 매우 깁니다.
 *          - <random>  : 템플릿 전개 비용이 큽니다.
 *          - <iostream>: 정적 초기화 오버헤드가 있습니다.
 *          자주 바뀌지 않는 TU에서는 pch.h 를 통해 포함하고,
 *          필요한 헤더만 직접 include 하는 방식을 지향하세요.
 */
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
