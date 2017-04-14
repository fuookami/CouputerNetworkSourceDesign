#include "Server.h"

Server::Server() : tcpServer(new QTcpServer(nullptr))
{

}

Public::RetCode Server::listen(const QHostAddress host, quint16 port)
{
	tcpServer->listen(host, port);

	std::ostringstream sout;
	sout << "开始监听" << host.toString().toStdString() << ":" << port << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	connect(tcpServer, SIGNAL(newConnection()), this, SLOT(getConnection()));
}

Public::RetCode Server::close()
{
	for (auto & th : tcpSocketThreads)
	{
		th.second->stop();
		connect(th.second.get(), SIGNAL(stopped(unsigned int)), this, SLOT(socketHandleThreadStopped(unsigned int)));
	}
}

void Server::getMsg(const QString msg, unsigned int id)
{
	std::ostringstream sout;
	sout << "客户端" << id << "：";
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()) + std::move(msg));
}

void Server::getData(const std::string data, unsigned int id)
{
	std::ostringstream sout;
	sout << "客户端" << id << "有数据传入：" << data << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	std::string ret(dispose(data));
	sout.clear();
	sout << "客户端" << id << "：处理后输入数据后，准备回复" << ret << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	tcpSocketThreads[id]->sendData(ret);
}

void Server::getConnection()
{
	unsigned int thisID(SocketHandleThread::getThreadCounter());
	std::ostringstream sout;
	sout << "监听到有客户端接入，分配编号为" << thisID << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	tcpSocketThreads.insert(std::make_pair(thisID,
		std::shared_ptr<SocketHandleThread>(new SocketHandleThread(tcpServer->nextPendingConnection(), thisID))));
	SocketHandleThread *serverThread(tcpSocketThreads[thisID].get());
	connect(serverThread, SIGNAL(socketDisconnected(unsigned int)),
		this, SLOT(cilentDisconnected(unsigned short)));
	connect(serverThread, SIGNAL(pushMsg(const QString, unsigned int)),
		this, SLOT(getMsg(const QString, unsigned int)));
	connect(serverThread, SIGNAL(pushData(const std::string, unsigned int)),
		this, SLOT(getData(const std::string, unsigned int)));
	serverThread->start();
}

void Server::cilentDisconnected(const unsigned short id)
{
	std::ostringstream sout;
	sout << "客户端" << id << "断开连接，关闭对应的处理进程" << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
}

std::string Server::dispose(const std::string data)
{
	unsigned char count(0);
	for (unsigned int i(0), j(data.size()); i != j; ++i)
		count += data[i];
	return std::to_string(count);
}
