/**
 * @file Serializer.h
 * @brief Binary / JSON / XML / ObjectDiff 직렬화기와 SerializeContext · SchemaMigrate 를 한 번에 include 하는 우산 헤더입니다.
 * @note 구현은 SerializeContext.cpp, SchemaMigrate.cpp, BinarySerializer.cpp, JsonSerializer.cpp,
 *       XmlSerializer.cpp, ObjectDiffSerializer.cpp, SerializerUtil.cpp 에 있습니다.
 */
#pragma once
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Serialization/Object/ObjectDiffSerializer.h"
