﻿#pragma once

#include <QtCore/QString>
#include <QtNetwork/QHostAddress>

using State = unsigned char;
using RetCode = unsigned char;

namespace BasicSetting
{
	static const QString host("localhost");
	static const unsigned short port(8080);
	static const unsigned char MaxRetryTime(10);
	static const unsigned short MSOfOnceTry(1000);

	static const enum RetCodes
	{
		NoError,
		StateError,
		ServerLose,
	};
}