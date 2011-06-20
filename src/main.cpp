#include "irc.h"

#include <iostream>


void callback(const IRC_Client::Message& msg)
{
	// date is in Y/M/D Hr:Min:Sec format, reduce it to Hr:Min
	std::cout << msg.time.substr(msg.time.find(' ') + 1, msg.time.find_last_of(':')) << " ";
	
	if (!msg.network.empty())
	{
		std::cout << msg.network << " ";
		if (!msg.channel.empty())
			std::cout << msg.channel << " ";
	}
	std::cout << msg.user << " ";
	std::cout << msg.message << std::endl;
}


int main(int argc, char **argv)
{
	IRC_Client::Manager irc(boost::bind(&callback, _1));
	IRC_Client::Network* network = irc.connect(boost::bind(&callback, _1), "irc.freenode.org", 6667, "testname101010");
	IRC_Client::Channel* channel = network->join("testchannel101010", boost::bind(&callback, _1));

	std::string input;
	while (std::getline(std::cin, input))
	{
		if (input == "/exit")
			break;

		channel->write(input);
	}

	return 0;
}
