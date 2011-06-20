#include "irc.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/foreach.hpp>
#include <iostream>
#include <sstream>
#include <string.h>

using boost::asio::ip::tcp;
const std::string CR_LF("\r\n");
#define READ_BUFFER_SIZE 512
#define foreach BOOST_FOREACH


namespace IRC_Client
{


static char* skip(char* s, char c)
{
	while (*s != c && *s != '\0')
		s++;
	if (*s != '\0')
		*s++ = '\0';
	return s;
}

static void trim(char *s) 
{
	char *e;

	e = s + strlen(s) - 1;
	while (isspace(*e) && e > s)
		e--;
	*(e + 1) = '\0';
}


Message::Message( 
		const std::string& _message,
		const std::string& _network/* = ""*/,
		const std::string& _channel/* = ""*/,
		const std::string& _command/* = ""*/,
		const std::string& _user/* = ""*/)

	:	network(_network), channel(_channel), command(_command),
		user(_user), message(_message),
		time(boost::posix_time::to_simple_string(
			boost::posix_time::ptime(boost::posix_time::second_clock::local_time())))
{}


Channel::Channel(
	Network* network,
	const std::string& name,
	boost::function<void(const Message&)> outputCallback)
	:	mName(name), mNetwork(network), mOutputCallback(outputCallback)
{
	mNetwork->write("JOIN :" + mName);
}


Channel::~Channel()
{
	mNetwork->write("PART :" + mName);
}


void Channel::write(const std::string& text)
{
	if (text.empty()) return;
	if (text[0] == '/')
		mNetwork->write(text.substr(1));
	else
		mNetwork->write("PRIVMSG " + mName + " :" + text);
}


Network::Network(
	Manager* client,
	boost::function<void(const Message&)> outputCallback,
	const std::string& name,
	int port,
	const std::string& nick,
	const std::string& password)
	:	mManager(client), mOutputCallback(outputCallback),
		mName(name), mNick(nick), mIsConnected(false), mIsShuttingDown(false), mDisableWrite(false)
{
	mReadBuffer.prepare(READ_BUFFER_SIZE);
	
	mSocket = new tcp::socket(*mManager->getIO_Service());
	mManager->getIO_Service()->post(boost::bind(&Network::startConnect, this, port));
	
	if (!password.empty())
		write(std::string("PASS " + password));

	write(std::string("NICK " + nick));
	write(std::string("USER " + nick + " localhost " + name + " :" + nick));
}


Network::~Network()
{
	mIsShuttingDown = true;
	foreach (auto it, mChannels)
		delete it.second;
	mChannels.clear();
	
	mDisableWrite = true;
	if (!mWriteQueue.empty())
	{
		boost::unique_lock<boost::mutex> lock(mWriteQueue_emptied_mutex);
		mWriteQueue_emptied_condition_variable.wait(lock); // TODO timed_wait
	}
	mSocket->close();
	if (mReadHandlerRunning)
	{
		boost::unique_lock<boost::mutex> lock(mReadHandlerRunning_mutex);
		mReadHandlerRunning_condition_variable.wait(lock);
	}
	delete mSocket;
}


void Network::write(const std::string& text)
{
	if (text.empty() || text[0] == '\n' || text[0] == '\r' || mDisableWrite) return;
	
	{
		boost::lock_guard<boost::mutex> lock(mWriteQueue_mutex);
		mWriteQueue.push_back(text + CR_LF);
	}

	if (mIsConnected)
		mManager->getIO_Service()->post(boost::bind(&Network::handleWrite, this, boost::system::error_code()));
}


Channel* Network::join(const std::string& channelName, boost::function<void(const Message&)> outputCallback)
{
	if (channelName.empty()) throw std::string("no name for the channel!");
	Channel* channel = new Channel(this,
		channelName[0] != '#' ? '#' + channelName : channelName,
		outputCallback);
	mChannels.insert(std::pair<std::string, Channel*>(channelName, channel));
	return channel;
}


void Network::leaves(const std::string& channelName)
{
	auto it = mChannels.find(channelName);
	if (it == mChannels.end()) return;
	delete it->second;
	mChannels.erase(it);
}


void Network::leaves(Channel* channel)
{
	leaves(channel->mName);
}


void Network::startConnect(int port)
{
	try
	{
		tcp::resolver resolver(*mManager->getIO_Service());
		std::stringstream ss_port;
		ss_port << port;
		tcp::resolver::query query(mName, ss_port.str());
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end_it;
		tcp::endpoint endpoint = *endpoint_iterator;
		
		mOutputCallback(Message("Connecting... " + endpoint.address().to_string(), mName));
		mSocket->async_connect(endpoint,
			boost::bind(
				&Network::handleConnect, this,
				boost::asio::placeholders::error, ++endpoint_iterator));
	}
	catch (std::exception& e)
	{
		mOutputCallback(Message(std::string("error connecting: ") + e.what(), mName));
		mManager->disconnect(this);
	}
}


void Network::handleConnect(
	const boost::system::error_code& error,
	tcp::resolver::iterator endpoint_iterator)
{
	if (!error)
	{
		setConnected();
		handleRead(boost::system::error_code());
	}
	else if (endpoint_iterator != tcp::resolver::iterator())
	{
		mSocket->close();
		tcp::endpoint endpoint = *endpoint_iterator;

		mOutputCallback(Message("Connecting... " + endpoint.address().to_string(), mName));
		mSocket->async_connect(endpoint,
			boost::bind(
				&Network::handleConnect, this,
				boost::asio::placeholders::error, ++endpoint_iterator));
	}
}


void Network::handleRead(const boost::system::error_code& error)
{
	if (!error)
	{
		boost::asio::async_read_until(*mSocket,
			mReadBuffer,
			CR_LF,
			boost::bind(
				&Network::handleReaded, this,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
	}
	else
	{	
		mReadHandlerRunning = false;
		mManager->disconnect(mName);
		mReadHandlerRunning_condition_variable.notify_one();
	}
}


void Network::handleReaded(const boost::system::error_code& error, size_t byteReceived)
{
	if (!error)
		parseReaded(byteReceived);
	handleRead(error);
}


void Network::parseReaded(size_t byteReceived)
{
	char c[READ_BUFFER_SIZE];
	memcpy(c, boost::asio::buffer_cast<const char*>(mReadBuffer.data()), byteReceived);
	c[byteReceived] = '\0';
	mReadBuffer.consume(byteReceived);

	char *cmd = &c[0];
	char *usr = (char*)mName.c_str(); // we won't change it
	char *par, *txt;

	if(!cmd || !*cmd)
		return;
	
	if(cmd[0] == ':')
	{
		usr = cmd + 1;
		cmd = skip(usr, ' ');
		if(cmd[0] == '\0')
			return;
		
		skip(usr, '!');
	}
	skip(cmd, '\r');
	par = skip(cmd, ' ');
	txt = skip(par, ':');
	trim(par);

	if(!strcmp("PONG", cmd))
		return;
	else if(!strcmp("PING", cmd))
		write("PONG :" + std::string(txt));
	else
	{
		Channel* channel = 0;
		std::string channelName("");
		auto outputCallback = mOutputCallback;
		auto it = mChannels.find(par);
		if (it != mChannels.end())
		{
			channel = it->second;
			outputCallback = channel->mOutputCallback;
			channelName = channel->mName;
		}
		outputCallback(Message(txt, mName, channelName, cmd, usr));
		
		if(!strcmp("NICK", cmd) && (usr == mNick))
			mNick.assign(txt);
	}
}


void Network::handleWrite(const boost::system::error_code& error)
{	
	if (!error)
	{
		if (!mWriteQueue.empty())
		{
			boost::asio::async_write(*mSocket,
				boost::asio::buffer(
					mWriteQueue.front().data(),
					mWriteQueue.front().size()),
				boost::bind(
					&Network::handleWrited, this,
					boost::asio::placeholders::error));
		}
		else mWriteQueue_emptied_condition_variable.notify_one();
	}
	else mManager->disconnect(this);
}


void Network::handleWrited(const boost::system::error_code& error)
{
	if (!error)
	{
		{
			boost::lock_guard<boost::mutex> lock(mWriteQueue_mutex);
			mWriteQueue.pop_front();
		}
		handleWrite();
	}
	else mManager->disconnect(this);
}


void Network::setConnected(bool connected/* = true*/)
{
	mIsConnected = connected;
	if (connected)
		if (!mWriteQueue.empty())
			handleWrite();
}


Manager::Manager(boost::function<void(const Message&)> outputCallback)
:	mOutputCallback(outputCallback), mIO_Service_ExitThread(false)
{
	mThread = new boost::thread(&Manager::run, this);
}


Manager::~Manager()
{
	while (!mNetworks.empty())
		disconnect(mNetworks.begin()->second);

	mIO_Service_ExitThread = true;
	mIO_Service.stop();
	notifyThread();
	mThread->join();
	delete mThread;
}


Network* Manager::connect(
	boost::function<void(const Message&)> outputCallback,
	const std::string& networkName,
	int port,
	const std::string& nick,
	const std::string& password/* = ""*/)
{
	Network* network = new Network(this, outputCallback, networkName, port, nick, password);
	notifyThread();
	mNetworks.insert(std::pair<std::string, Network*>(networkName, network));
	return network;
}


void Manager::disconnect(const std::string& networkName)
{
	auto it = mNetworks.find(networkName);
	if (it == mNetworks.end()) return;
	Network* network = it->second;
	mNetworks.erase(it);
	delete network;
}


void Manager::run()
{
	boost::unique_lock<boost::mutex> lock(mIO_Service_Mutex);

	while (!mIO_Service_ExitThread)
	{
		mIO_Service.run();
		mIO_Service.reset();
		mIO_Service_ConditionVariable.wait(lock);
	}
}


void Manager::notifyThread()
{
	mIO_Service_ConditionVariable.notify_one();
}


} // namespace IRC_Client

