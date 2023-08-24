#pragma once

#include "ALVR-common/exception.h"
#include "Settings.h"
#include <codecvt>

Exception MakeException(const char *format, ...);

void Error(const char *format, ...);
void Warn(const char *format, ...);
void Info(const char *format, ...);
void Debug(const char *format, ...);
void LogPeriod(const char *tag, const char *format, ...);
void TxtPrint(const char *format, ...);
void TxtGaze(const char *format, ...);
void Txtwspeed(const char *format, ...);
void TxtNDCGaze(const char *format, ...);
void TxtLatency(const char *format, ...); 
void ThreadLatency(const char *format, ...);
void TxtDeltaLocat(const char *format, ...);
