#include "Logger.h"

#include <cstdarg>

#include "driverlog.h"
#include "bindings.h"
#include <iostream>
#include <string>
#include <windows.h>
using namespace std;
static FILE* fpLog = NULL;
void _log(const char *format, va_list args, void (*logFn)(const char *), bool driverLog = false)
{
	char buf[1024];
	int count = vsnprintf(buf, sizeof(buf), format, args);
	if (count > (int)sizeof(buf))
		count = (int)sizeof(buf);
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	logFn(buf);

	//TODO: driver logger should concider current log level
#ifndef ALVR_DEBUG_LOG
	if (driverLog)
#endif
		DriverLog(buf);
}

Exception MakeException(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	Exception e = FormatExceptionV(format, args);
	va_end(args);

	return e;
}

void Error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	_log(format, args, LogError, true);
	va_end(args);
}

void Warn(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	_log(format, args, LogWarn, true);
	va_end(args);
}

void Info(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	// Don't log to SteamVR/writing to file for info level, this is mostly statistics info
	_log(format, args, LogInfo);
	va_end(args);
}

void Debug(const char *format, ...)
{
// Use our define instead of _DEBUG - see build.rs for details.
#ifdef ALVR_DEBUG_LOG
	va_list args;
	va_start(args, format);
	_log(format, args, LogDebug);
	va_end(args);
#else
	(void)format;
#endif
}

void LogPeriod(const char *tag, const char *format, ...)
{
	va_list args;
	va_start(args, format);

	char buf[1024];
	int count = vsnprintf(buf, sizeof(buf), format, args);
	if (count > (int)sizeof(buf))
		count = (int)sizeof(buf);
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	LogPeriodically(tag, buf);

	va_end(args);
}

void LogFileUpDate(string LogFile) {
     //LogGetLocalTime();
    if (fpLog != nullptr) {
        fclose(fpLog);
        fpLog = nullptr;
    }
	//string LogFile= "E:\\alvrdata\\TxtPrintf\\A_EyeGazeHistory.txt";
	//string LogFile= "D:\\AX\\Logs\\Debug\\A_Debug20test.txt";
	//string LogFile= ".\\A_Debug20test.txt";
    if(fpLog ==nullptr) {
         fpLog = _fsopen(LogFile.c_str(), "at+", _SH_DENYNO);
    }
    
}

void TxtPrint(const char *format, ...)
{   string Info_Type = "Info:";
	va_list args;
	va_start(args, format);
	//LogFileUpDate("E:\\alvrdata\\TxtPrintf\\A_TxtPrint1.txt");
	LogFileUpDate("D:\\AX\\Logs\\Debug\\A_TxtPrint12.txt");
	//LogFileUpDate("C:\\SHN\\ALVREXE\\OutPut\\Log\\A_TxtPrint12.txt");
    char buf[1024];
	//string sys_timeType=sys_time+Info_Type;    
    vsnprintf(buf, sizeof(buf), format, args);
    //fprintf(fpLog, sys_timeType.c_str());
    fprintf(fpLog, buf);
	va_end(args);
}

void TxtGaze(const char *format, ...)
{   string Info_Type = "Info:";
	va_list args;
	va_start(args, format);
	//LogFileUpDate("E:\\alvrdata\\TxtPrintf\\Gaze_NowHist.txt");
	LogFileUpDate("D:\\AX\\Logs\\Debug\\Gaze_NowHist.txt");
	//LogFileUpDate("C:\\SHN\\ALVREXE\\OutPut\\Log\\Gaze_NowHist.txt");
    char buf[1024];
	//string sys_timeType=sys_time+Info_Type;    
    vsnprintf(buf, sizeof(buf), format, args);
    //fprintf(fpLog, sys_timeType.c_str());
    fprintf(fpLog, buf);
	va_end(args);
}

void Txtwspeed(const char *format, ...)
{   string Info_Type = "Info:";
	va_list args;
	va_start(args, format);
	//LogFileUpDate("E:\\alvrdata\\TxtPrintf\\Gaze_wspeed.txt");
	LogFileUpDate("D:\\AX\\Logs\\Debug\\Gaze_wspeed.txt");
	//LogFileUpDate("C:\\SHN\\ALVREXE\\OutPut\\Log\\Gaze_wspeed.txt");
    char buf[1024];
	//string sys_timeType=sys_time+Info_Type;    
    vsnprintf(buf, sizeof(buf), format, args);
    //fprintf(fpLog, sys_timeType.c_str());
    fprintf(fpLog, buf);
	va_end(args);
}

void TxtNDCGaze(const char *format, ...)
{   string Info_Type = "Info:";
	va_list args;
	va_start(args, format);
	//LogFileUpDate("E:\\alvrdata\\TxtPrintf\\Gaze_wspeed.txt");
	LogFileUpDate("D:\\AX\\Logs\\Debug\\NDCGaze_location.txt");
    //LogFileUpDate("C:\\SHN\\ALVREXE\\OutPut\\Log\\Gaze_wspeed.txt");
    char buf[1024];
	//string sys_timeType=sys_time+Info_Type;    
    vsnprintf(buf, sizeof(buf), format, args);
    //fprintf(fpLog, sys_timeType.c_str());
    fprintf(fpLog, buf);
	va_end(args);
}
