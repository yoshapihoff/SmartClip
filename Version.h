#pragma once

// Единая версия приложения. Значение приходит из файла VERSION в корне проекта
// через определение компилятора SMARTCLIP_VERSION (см. CMakeLists.txt).
//
// Файл VERSION — единственный источник правды. Правится вручную или скриптом
// scripts/bump-version.sh (обычные коммиты — patch; minor/major — по указанию).
//
// Если макрос не задан (например, при сборке отдельного теста без определения),
// подставляем "dev", чтобы сборка не падала.

#ifndef SMARTCLIP_VERSION
#define SMARTCLIP_VERSION "dev"
#endif

#define SMARTCLIP_VERSION_STRING SMARTCLIP_VERSION
