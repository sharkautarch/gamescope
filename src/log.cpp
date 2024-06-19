#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <memory>

#include "defer.hpp"
#include "log.hpp"

LogScope::LogScope(const char *name) {
	this->name = name;
	this->priority = LOG_DEBUG;
}

LogScope::LogScope(const char *name, enum LogPriority priority) {
	this->name = name;
	this->priority = priority;
}

bool LogScope::has(enum LogPriority priority) {
	return priority <= this->priority;
}

void LogScope::vlogf(enum LogPriority priority, const char *fmt, va_list args) {
	if (!this->has(priority)) {
		return;
	}

	char *buf = nullptr;
	vasprintf(&buf, fmt, args);
	if (!buf)
		return;
	defer( free(buf); );

	for (auto& listener : m_LoggingListeners)
		listener.second( priority, this->name, buf );

	fprintf(stderr, "%s: %s\n", this->name, buf);
}

void LogScope::logf(enum LogPriority priority, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	this->vlogf(priority, fmt, args);
	va_end(args);
}

void LogScope::errorf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	this->vlogf(LOG_ERROR, fmt, args);
	va_end(args);
}

void LogScope::infof(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	this->vlogf(LOG_INFO, fmt, args);
	va_end(args);
}

void LogScope::debugf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	this->vlogf(LOG_DEBUG, fmt, args);
	va_end(args);
}

void LogScope::errorf_errno(const char *fmt, ...) {
	const char *err = strerror(errno);

	static char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	this->logf(LOG_ERROR, "%s: %s", buf, err);
}
