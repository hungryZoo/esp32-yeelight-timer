// 시리얼 로그 + 최근 로그 링버퍼 (웹 표시용)
#pragma once
#include <Arduino.h>

void logBegin();
void logf(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// 최근 로그를 오래된 순으로 개행 구분 문자열로 반환
String logDump();
