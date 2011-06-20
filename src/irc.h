#pragma once

	#define BOOST_THREAD_USE_LIB 

#include <unordered_map>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>


namespace IRC_Client
{

class Channel;
class Network;
class Manager;

struct Message
{
	Message(	 
		const std::string& _message,
		const std::string& _network = "",
		const std::string& _channel = "",
		const std::string& _command = "",
		const std::string& _user = "");

	std::string network;
	std::string channel;
	std::string command;
	std::string user;
	std::string message;
	std::string time;
};


class Channel
{
public:
	Channel(
		Network* network,
		const std::string& name,
		boost::function<void(const Message&)> outputCallback);
	~Channel();

	void write(const std::string& text);

	std::string mName;
	Network* mNetwork;
	boost::function<void(const Message&)> mOutputCallback;
};


class Network
{
public:
	Network(
		Manager* client,
		boost::function<void(const Message&)> outputCallback,
		const std::string& name,
		int port,
		const std::string& nick,
		const std::string& password);
	~Network();

	void write(const std::string& text);
	Channel* join(const std::string& channelName, boost::function<void(const Message&)> outputCallback);
	void leaves(const std::string& channel);
	void leaves(Channel* channel);

	
	void startConnect(int port);
	void handleConnect(
		const boost::system::error_code& error,
		boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
	void handleRead(const boost::system::error_code& error = boost::system::error_code());
	void handleReaded(const boost::system::error_code& error, size_t byteReceived);
	void parseReaded(size_t byteReceived);
	void handleWrite(const boost::system::error_code& error = boost::system::error_code());
	void handleWrited(const boost::system::error_code& error = boost::system::error_code());

	void setConnected(bool connected = true);

	boost::asio::streambuf mReadBuffer;

	std::deque<std::string> mWriteQueue;
	boost::mutex mWriteQueue_mutex;
	boost::condition_variable mWriteQueue_emptied_condition_variable;
	boost::mutex mWriteQueue_emptied_mutex;
	
	bool mReadHandlerRunning;
	boost::condition_variable mReadHandlerRunning_condition_variable;
	boost::mutex mReadHandlerRunning_mutex;

	std::unordered_map<std::string, Channel*> mChannels;
	boost::asio::ip::tcp::socket* mSocket;
	
	Manager* mManager;
	boost::function<void(const Message&)> mOutputCallback;
	
	std::string mName;	
	std::string mNick;
	
	bool mIsConnected;
	bool mIsShuttingDown;
	bool mDisableWrite;
};


class Manager
{
public:
	Manager(boost::function<void(const Message&)> outputCallback);
	~Manager();

	Network* connect(
		boost::function<void(const Message&)> outputCallback,
		const std::string& networkName,
		int port,
		const std::string& nick,
		const std::string& password = "");

	void disconnect(const std::string& networkName);
	void disconnect(Network* network) {disconnect(network->mName);}
	
	boost::asio::io_service* getIO_Service() {return &mIO_Service;}

private:
	void doDisconnect();
	void notifyThread();
	void run();
	
	boost::function<void(const Message&)> mOutputCallback;
	std::unordered_map<std::string, Network*> mNetworks;
	boost::asio::io_service mIO_Service;
	boost::thread* mThread;
	
	bool mIO_Service_ExitThread;
	boost::mutex mIO_Service_Mutex;
	boost::condition_variable mIO_Service_ConditionVariable;
};

} // IRC_Client
