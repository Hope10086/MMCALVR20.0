#include "gpulogger.h"

#include <cstdarg>

void _log(const char *format, va_list args, void (*logFn)(const char *))
{
	char buf[1024];
	int count = vsnprintf(buf, sizeof(buf), format, args);
	if (count > (int)sizeof(buf))
		count = (int)sizeof(buf);
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	logFn(buf);
}

void Info(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	_log(format, args, InfoLog);
	va_end(args);
}