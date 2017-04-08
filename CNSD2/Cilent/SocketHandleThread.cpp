#include "SocketHandleThread.h"

unsigned int SocketHandleThread::threadCounter = 0;

PKTTimer::PKTTimer(unsigned char _id)
	: id(_id), timer(new QTimer(nullptr))
{
	connect(timer, SIGNAL(timeout()), this, SLOT(timeoutSlot()));
}

void PKTTimer::startTimer(const unsigned short ms)
{
	timer->start(ms);
}

void PKTTimer::stopTimer()
{
	timer->stop();
}

void PKTTimer::timeoutSlot(void)
{
	emit timeoutSignal(id);
}

SocketHandleThread::SocketHandleThread(QTcpSocket * _tcpSocket, unsigned int _id, bool _isServer)
	: tcpSocket(_tcpSocket), id(_id), isServer(_isServer)
{
	for (unsigned int i(0), j(sendingInfo.timers.size()); i != j; ++i)
	{
		sendingInfo.timers[i] = new PKTTimer(i);
	}
}

void SocketHandleThread::start()
{
	stopped = false;
	run();
}

void SocketHandleThread::run()
{
	connect(tcpSocket, SIGNAL(readyRead()), this, SLOT(dataReceived()));

	while (!stopped)
	{
		if (threadState == Public::ThreadState::Idle)
		{
			if (!sendingInfo.sendingData.empty())
			{
				// 转至准备发送态并发送SYN信号
				threadState = Public::ThreadState::WaitForSending;
				sendFrame(Public::RequestTypes::SYN, Public::countFrames(sendingInfo.sendingData.front()));
			}
		}
		else if (threadState == Public::ThreadState::Sending)
		{
			// 检查是否有需要发送的数据帧
			if (checkDataDeque())
				// 如果有，则发送
				sendFrames();
			// 否则等待ACK回复
		}
		else if (threadState == Public::ThreadState::WaitForSending)
		{
			// 等待ACK回复
		}
		else if (threadState == Public::ThreadState::Receieving)
		{
			// 等待PKT数据包发送
		}
	}
}

void SocketHandleThread::dataReceived()
{
	emit pushMsg(QString::fromLocal8Bit("收到数据帧，准备进行解析\n"));

	Public::State currFrameState(Public::getRandomFrameState());
	QDataStream in(tcpSocket);
	Public::DataFrame currFrame(in);

	std::ostringstream sout;
	sout << "当前随机得到的帧状态为" << Public::getFrameStateString(currFrameState) << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	if (currFrameState == Public::FrameState::Lose)
		return;
	if (currFrameState == Public::FrameState::Wrong
		|| !currFrame.isCorrect())
	{
		emit pushMsg(QString::fromLocal8Bit("帧校验出错，丢弃该帧\n"));
		return;
	}

	sout.clear();
	sout << "帧校验正确，数据为：" << currFrame.data << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
	emit pushMsg(QString::fromLocal8Bit("准备进行解密\n"));
	Public::decode(currFrame.data);
	emit pushMsg(QString::fromLocal8Bit("解密完成\n"));
	sout.clear();
	sout << "解密后数据为：" << currFrame.data << std::endl;
	emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

	switch (threadState)
	{
	case Public::ThreadState::Idle:
		dataReceivedForIdle(currFrame, currFrameState);
		break;
	case Public::ThreadState::Receieving:
		dataReceivedForReceiving(currFrame, currFrameState);
		break;
	case Public::ThreadState::Sending:
		dataReceivedForSending(currFrame, currFrameState);
		break;
	case Public::ThreadState::WaitForSending:
		dataReceivedForWaitSending(currFrame, currFrameState);
		break;
	default:
		break;
	}
}

void SocketHandleThread::dataReceivedForIdle(const Public::DataFrame &currFrame, Public::State frameState)
{
	if (currFrame.request == Public::RequestTypes::SYN)
	{
		// 初始化数据接收槽并转入接收状态
		recievingInfo.waitingFrameId = recievingInfo.currFrameNum = 0;
		recievingInfo.buffFrameId.clear();
		for (std::deque<Public::DataFrame> &currDeque : recievingInfo.recievingData)
			currDeque.clear();
		recievingInfo.totalFrameNum = Public::str2ui(currFrame.data);

		std::ostringstream sout;
		sout << "接收到数据发送请求，共" << recievingInfo.totalFrameNum << "个包，转入接收数据包状态" << std::endl;
		emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
		threadState = Public::ThreadState::Receieving;
		
		// 返回ACK(-1)
		if (frameState == Public::FrameState::NoReply)
			return;

		emit pushMsg(QString::fromLocal8Bit("向对方发送ACK信号，示意可发送数据包\n"));
		sendFrame(Public::RequestTypes::ACK, -1);
	}
	else if (currFrame.request == Public::RequestTypes::PKT)
	{
		// 错误帧，丢弃该帧，并返回ACK(-1)告知对方，所有帧已接收完毕
		std::ostringstream sout;
		sout << "接收到数据包，帧编号为" << currFrame.id << "，由于无等待接收数据包，将抛弃该帧" << std::endl;
		emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

		if (frameState == Public::FrameState::NoReply)
			return;

		emit pushMsg(QString::fromLocal8Bit("向对方发送ACK(-1)信号，示意所有数据已接收完毕\n"));
		sendFrame(Public::RequestTypes::ACK, -1);
	}
	else if (currFrame.request == Public::RequestTypes::ACK)
	{
		// 错误帧，丢弃该帧
		std::ostringstream sout;
		sout << "接收到ACK信号，由于处于空闲状态，不应受到ACK信号，将抛弃该帧" << std::endl;
		emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
	}
}

void SocketHandleThread::dataReceivedForReceiving(const Public::DataFrame &currFrame, Public::State frameState)
{
	if (currFrame.request == Public::RequestTypes::PKT)
	{
		// 帧轮盘接收数据，并返回对应的ACK
		// 如果收到帧编号为希望收到的帧编号，则更新希望收到的帧编号，并发送ACK信号，参数为新的希望收到的帧编号
		unsigned int currFrameId(currFrame.id);
		recievingInfo.recievingData[currFrameId].push_back(std::move(currFrame));
		std::ostringstream sout;
		sout << "已收到数据包帧编号为" << currFrameId << std::endl;
		pushMsg(QString::fromLocal8Bit(sout.str().c_str()));

		if (currFrameId == recievingInfo.waitingFrameId)
		{
			++recievingInfo.currFrameNum;
			recievingInfo.waitingFrameId = ++recievingInfo.waitingFrameId % Public::RouletteSize;
			sout.clear();
			while (!recievingInfo.buffFrameId.empty() && recievingInfo.buffFrameId.find(recievingInfo.waitingFrameId) != recievingInfo.buffFrameId.cend())
			{
				sout << "编号为" << recievingInfo.waitingFrameId << "的帧已在帧轮盘中" << std::endl;
				++recievingInfo.currFrameNum;
				recievingInfo.buffFrameId.erase(recievingInfo.waitingFrameId);
				recievingInfo.waitingFrameId = ++recievingInfo.waitingFrameId % Public::RouletteSize;
			}

			if (frameState == Public::FrameState::FrameNoError)
			{
				sout << "将向客户端发送ACK(" << (recievingInfo.waitingFrameId - 1) << ")信号" << std::endl;
				sendFrame(Public::RequestTypes::ACK, recievingInfo.waitingFrameId == 0 ? Public::RouletteSize - 1 : recievingInfo.waitingFrameId - 1);
			}

			pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
		}
		// 如果收到帧编号不是希望收到的帧编号，则装入帧轮盘中，并发送ACK信号，参数为希望收到的帧编号
		else
		{
			recievingInfo.buffFrameId.insert(currFrameId);
			sout.clear();
			sout << "希望收到的帧编号为" << recievingInfo.waitingFrameId << "，将该帧装入帧轮盘中" << std::endl;
			if (frameState == Public::FrameState::FrameNoError)
			{
				sout << "将向客户端发送ACK(" << recievingInfo.waitingFrameId << ")信号" << std::endl;
				sendFrame(Public::RequestTypes::ACK, recievingInfo.waitingFrameId);
			}
			pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
		}

		// 如果数据接收完毕，向上级发送数据并转移到空闲状态，并返回ACK(-1)示意对方发送完毕
		if (recievingInfo.currFrameNum == recievingInfo.totalFrameNum)
		{
			std::pair<Public::RequestType, std::string> data(Public::readDataRoulette<std::string>(recievingInfo.recievingData));
			pushMsg(QString::fromLocal8Bit("已收到所有数据包，向服务器推送数据，转入空闲状态\n"));
			emit pushData(std::move(data.second));
			threadState = Public::ThreadState::Idle;

			if (frameState == Public::FrameState::NoReply)
				return;

			pushMsg(QString::fromLocal8Bit("向客户端发送ACK(-1)信号，示意所有数据已接收完毕\n"));
			sendFrame(Public::RequestTypes::ACK, -1);
		}
	}
	else
	{
		// 错误帧，丢弃该帧
		std::ostringstream sout;
		sout << "接收到ACK信号或SYN信号，由于处于接收状态，不应受到PKT以外的信号，将抛弃该帧" << std::endl;
		emit pushMsg(QString::fromLocal8Bit(sout.str().c_str()));
	}
}

void SocketHandleThread::dataReceivedForWaitSending(const Public::DataFrame &currFrame, Public::State frameState)
{
	if (currFrame.request == Public::RequestTypes::ACK)
	{
		// 如果收到ACK(-1)转移到发送状态
	}
	else if (currFrame.request == Public::RequestTypes::SYN)
	{
		// 如果收到SYN表示对方也转移到了待发送状态
		if (isServer)
		{
			//	如果本线程是服务器线程，则转移到发送状态
		}
		else 
		{
			//	如果本线程是客户端线程，则转移到接收状态，并发送ACK(-1)示意对方可发送数据包
		}
	}
	else if (currFrame.request == Public::RequestTypes::PKT)
	{
		// 错误帧，丢弃该帧
	}
}

void SocketHandleThread::dataReceivedForSending(const Public::DataFrame &currFrame, Public::State frameState)
{
	// 如果发送完毕或收到ACK(-1)，则将当前数据轮盘出队并转移到空闲状态
	// 否则，停止刚Accept数据包对应的计时器，并移动发送窗口，并发送新的数据包
}
